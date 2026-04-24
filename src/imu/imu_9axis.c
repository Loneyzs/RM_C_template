/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "imu_9axis.h"

#include <math.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(imu_9axis, CONFIG_SENSOR_LOG_LEVEL);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define IST8310_FETCH_RETRIES  5U
#define IST8310_FETCH_DELAY_MS 2U

#define IMU_DEFAULT_DT_NS 2000000ULL
#define IMU_MAX_DT_NS     100000000ULL
#define IMU_MIN_NORM_SQ   1.0e-6f
#define IMU_PI            3.14159265358979323846f

/*
 * The FreeRTOS reference project copies BMI088 Accel/Gyro X/Y/Z directly into
 * INS and leaves the mounting correction matrix at zero angles by default.
 * Keep the same board/body frame here.  The explicit mapper is retained so a
 * future board variant has a single place to change axis signs/order.
 */
#define IMU_AXIS_X 0
#define IMU_AXIS_Y 1
#define IMU_AXIS_Z 2

#define IMU_CALIB_SAMPLES                 400U
#define IMU_CALIB_INTERVAL_MS             2U
#define IMU_CALIB_MAX_ATTEMPTS            3U
#define IMU_CALIB_MAX_GYRO_RANGE_RAD_S    0.15f
#define IMU_CALIB_MAX_ACCEL_NORM_RANGE    0.50f

/* One-time flat calibration measured on 2026-04-24 with the board laid flat. */
#define IMU_ACCEL_FLAT_OFFSET_X_MPS2  0.02998547f
#define IMU_ACCEL_FLAT_OFFSET_Y_MPS2 -0.08040254f
#define IMU_ACCEL_FLAT_OFFSET_Z_MPS2 -0.04701789f

#define IMU_ACCEL_LPF_TAU_S        0.0085f
#define IMU_ACCEL_CORRECT_MIN_NORM 6.0f
#define IMU_ACCEL_CORRECT_MAX_NORM 13.0f
#define IMU_MAHONY_TWO_KP          2.0f
#define IMU_MAHONY_TWO_KI          0.02f
#define IMU_MAHONY_MAG_WEIGHT      0.6f

#define IMU_HEATER_TARGET_TEMP_C      40.0f
#define IMU_HEATER_PID_KP             1000.0f
#define IMU_HEATER_PID_KI             20.0f
#define IMU_HEATER_PID_KD             0.0f
#define IMU_HEATER_PID_DEADBAND_C     0.0f
#define IMU_HEATER_PID_INTEGRAL_LIMIT 300.0f
#define IMU_HEATER_PID_MAX_OUT        2000.0f
#define IMU_HEATER_PWM_FULL_SCALE     5000.0f

static const struct device *const bmi_accel = DEVICE_DT_GET(DT_NODELABEL(bmi088_accel));
static const struct device *const bmi_gyro  = DEVICE_DT_GET(DT_NODELABEL(bmi088_gyro));
static const struct device *const ist_mag   = DEVICE_DT_GET(DT_NODELABEL(ist8310));

static const struct pwm_dt_spec heater_pwm =
	PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER_NODE, heater);

struct imu_filter_state {
	bool attitude_valid;
	bool accel_lpf_valid;
	float q[4];
	float integral_error[3];
	float accel_lpf[3];
	uint64_t last_timestamp_ns;
};

static struct imu_filter_state filter_state;
static bool gyro_bias_calibrated;
static bool accel_flat_calibrated;
static float gyro_bias[3];
static float accel_offset[3];

static bool heater_available;
static bool heater_pid_enabled = true;
static float heater_pid_integral;
static float heater_pid_last_error;
static uint64_t heater_pid_last_ns;
static float heater_pid_output;

static uint8_t clamp_duty(uint8_t duty)
{
	return duty > 100U ? 100U : duty;
}

static float wrap_pi(float angle)
{
	while (angle > IMU_PI) {
		angle -= 2.0f * IMU_PI;
	}
	while (angle < -IMU_PI) {
		angle += 2.0f * IMU_PI;
	}

	return angle;
}

static float clamp_float(float value, float low, float high)
{
	if (value < low) {
		return low;
	}
	if (value > high) {
		return high;
	}

	return value;
}

static bool normalize_vec3(float v[3])
{
	const float norm_sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];

	if (norm_sq < IMU_MIN_NORM_SQ) {
		return false;
	}

	const float inv_norm = 1.0f / sqrtf(norm_sq);

	v[0] *= inv_norm;
	v[1] *= inv_norm;
	v[2] *= inv_norm;
	return true;
}

static void normalize_quaternion(float q[4])
{
	const float norm_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];

	if (norm_sq < IMU_MIN_NORM_SQ) {
		q[0] = 1.0f;
		q[1] = 0.0f;
		q[2] = 0.0f;
		q[3] = 0.0f;
		return;
	}

	const float inv_norm = 1.0f / sqrtf(norm_sq);

	q[0] *= inv_norm;
	q[1] *= inv_norm;
	q[2] *= inv_norm;
	q[3] *= inv_norm;
}

static void axis_align_vec3(const float raw[3], float aligned[3])
{
	aligned[0] = raw[IMU_AXIS_X];
	aligned[1] = raw[IMU_AXIS_Y];
	aligned[2] = raw[IMU_AXIS_Z];
}

static int apply_heater_duty(uint8_t duty)
{
	uint32_t pulse = (uint32_t)(((uint64_t)heater_pwm.period * duty) / 100U);

	heater_pid_output = (IMU_HEATER_PWM_FULL_SCALE * (float)clamp_duty(duty)) / 100.0f;
	return pwm_set_dt(&heater_pwm, heater_pwm.period, pulse);
}

static int apply_heater_pid_output(float output)
{
	float clamped = output;

	if (clamped < 0.0f) {
		clamped = 0.0f;
	}
	if (clamped > IMU_HEATER_PID_MAX_OUT) {
		clamped = IMU_HEATER_PID_MAX_OUT;
	}

	heater_pid_output = clamped;

	uint32_t pulse =
		(uint32_t)((heater_pwm.period * (uint64_t)heater_pid_output) / IMU_HEATER_PWM_FULL_SCALE);

	return pwm_set_dt(&heater_pwm, heater_pwm.period, pulse);
}

static void heater_pid_reset(void)
{
	heater_pid_integral = 0.0f;
	heater_pid_last_error = 0.0f;
	heater_pid_last_ns = 0U;
	heater_pid_output = 0.0f;
}

static void heater_pid_update(float temp_c, uint64_t now_ns)
{
	float dt_s;
	float error;
	float p_out;
	float d_out;
	float i_term;
	float temp_integral;
	float unclamped_output;
	int rc;

	if (!heater_available || isnan(temp_c)) {
		return;
	}
	if (!heater_pid_enabled) {
		return;
	}

	if (heater_pid_last_ns == 0U || now_ns <= heater_pid_last_ns) {
		heater_pid_last_ns = now_ns;
		heater_pid_last_error = IMU_HEATER_TARGET_TEMP_C - temp_c;
		rc = apply_heater_pid_output(IMU_HEATER_PID_MAX_OUT);
		if (rc < 0) {
			LOG_WRN("heater PWM set failed in warmup: %d", rc);
			heater_available = false;
		}
		return;
	}

	dt_s = (float)(now_ns - heater_pid_last_ns) / 1000000000.0f;
	if (dt_s <= 0.0f || dt_s > 1.0f) {
		dt_s = 0.02f;
	}

	error = IMU_HEATER_TARGET_TEMP_C - temp_c;
	if (fabsf(error) <= IMU_HEATER_PID_DEADBAND_C) {
		error = 0.0f;
	}

	p_out = IMU_HEATER_PID_KP * error;
	i_term = IMU_HEATER_PID_KI * error * dt_s;
	d_out = IMU_HEATER_PID_KD * (error - heater_pid_last_error) / dt_s;

	temp_integral = heater_pid_integral + i_term;
	if (temp_integral > IMU_HEATER_PID_INTEGRAL_LIMIT) {
		temp_integral = IMU_HEATER_PID_INTEGRAL_LIMIT;
	}
	if (temp_integral < -IMU_HEATER_PID_INTEGRAL_LIMIT) {
		temp_integral = -IMU_HEATER_PID_INTEGRAL_LIMIT;
	}

	unclamped_output = p_out + temp_integral + d_out;
	if (unclamped_output > IMU_HEATER_PID_MAX_OUT && error * heater_pid_integral > 0.0f) {
		i_term = 0.0f;
		temp_integral = heater_pid_integral;
		unclamped_output = p_out + temp_integral + d_out;
	}

	heater_pid_integral = temp_integral;
	heater_pid_last_error = error;
	heater_pid_last_ns = now_ns;

	rc = apply_heater_pid_output(unclamped_output);
	if (rc < 0) {
		LOG_WRN("heater PWM set failed in PID update: %d", rc);
		heater_available = false;
	}
}

static int fetch_xyz_once(const struct device *dev, enum sensor_channel chan, float out[3])
{
	struct sensor_value values[3];
	int rc;

	rc = sensor_sample_fetch(dev);
	if (rc < 0) {
		return rc;
	}

	rc = sensor_channel_get(dev, chan, values);
	if (rc < 0) {
		return rc;
	}

	out[0] = (float)sensor_value_to_double(&values[0]);
	out[1] = (float)sensor_value_to_double(&values[1]);
	out[2] = (float)sensor_value_to_double(&values[2]);
	return 0;
}

static int fetch_xyz_retry(const struct device *dev, enum sensor_channel chan, float out[3],
			   uint8_t retries, uint32_t delay_ms)
{
	int rc = -EIO;

	for (uint8_t attempt = 0; attempt < retries; attempt++) {
		rc = fetch_xyz_once(dev, chan, out);
		if (rc == 0) {
			return 0;
		}

		if ((attempt + 1U) < retries) {
			k_msleep(delay_ms);
		}
	}

	return rc;
}

static int collect_startup_calibration(float gyro_avg[3], float accel_avg[3],
				       float *gyro_range_max, float *accel_norm_range)
{
	float gyro_sum[3] = {0.0f, 0.0f, 0.0f};
	float accel_sum[3] = {0.0f, 0.0f, 0.0f};
	float gyro_min[3] = {0.0f, 0.0f, 0.0f};
	float gyro_max[3] = {0.0f, 0.0f, 0.0f};
	float accel_norm_min = 0.0f;
	float accel_norm_max = 0.0f;

	for (uint16_t i = 0U; i < IMU_CALIB_SAMPLES; i++) {
		float accel_raw[3];
		float gyro_raw[3];
		float accel[3];
		float gyro[3];
		int rc;

		rc = fetch_xyz_once(bmi_accel, SENSOR_CHAN_ACCEL_XYZ, accel_raw);
		if (rc < 0) {
			return rc;
		}

		rc = fetch_xyz_once(bmi_gyro, SENSOR_CHAN_GYRO_XYZ, gyro_raw);
		if (rc < 0) {
			return rc;
		}

		axis_align_vec3(accel_raw, accel);
		axis_align_vec3(gyro_raw, gyro);

		const float accel_norm =
			sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);

		for (uint8_t axis = 0U; axis < 3U; axis++) {
			gyro_sum[axis] += gyro[axis];
			accel_sum[axis] += accel[axis];

			if (i == 0U) {
				gyro_min[axis] = gyro[axis];
				gyro_max[axis] = gyro[axis];
			} else {
				gyro_min[axis] = fminf(gyro_min[axis], gyro[axis]);
				gyro_max[axis] = fmaxf(gyro_max[axis], gyro[axis]);
			}
		}

		if (i == 0U) {
			accel_norm_min = accel_norm;
			accel_norm_max = accel_norm;
		} else {
			accel_norm_min = fminf(accel_norm_min, accel_norm);
			accel_norm_max = fmaxf(accel_norm_max, accel_norm);
		}

		k_msleep(IMU_CALIB_INTERVAL_MS);
	}

	*gyro_range_max = 0.0f;
	for (uint8_t axis = 0U; axis < 3U; axis++) {
		gyro_avg[axis] = gyro_sum[axis] / (float)IMU_CALIB_SAMPLES;
		accel_avg[axis] = accel_sum[axis] / (float)IMU_CALIB_SAMPLES;
		*gyro_range_max = fmaxf(*gyro_range_max, gyro_max[axis] - gyro_min[axis]);
	}

	*accel_norm_range = accel_norm_max - accel_norm_min;
	return 0;
}

static int calibrate_startup_biases(void)
{
	float last_gyro_avg[3] = {0.0f, 0.0f, 0.0f};
	float last_accel_avg[3] = {0.0f, 0.0f, 0.0f};
	float gyro_range = 0.0f;
	float accel_norm_range = 0.0f;
	bool stable = false;
	int rc = 0;

	for (uint8_t attempt = 0U; attempt < IMU_CALIB_MAX_ATTEMPTS; attempt++) {
		rc = collect_startup_calibration(last_gyro_avg, last_accel_avg,
						 &gyro_range, &accel_norm_range);
		if (rc < 0) {
			return rc;
		}

		stable = gyro_range <= IMU_CALIB_MAX_GYRO_RANGE_RAD_S &&
			 accel_norm_range <= IMU_CALIB_MAX_ACCEL_NORM_RANGE;
		if (stable) {
			break;
		}

		LOG_WRN("IMU calibration attempt %u unstable: gyro_range=%.4f accel_norm_range=%.4f",
			(uint32_t)attempt + 1U, (double)gyro_range, (double)accel_norm_range);
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		gyro_bias[axis] = last_gyro_avg[axis];
	}
	gyro_bias_calibrated = true;

	if (!stable) {
		LOG_WRN("IMU gyro bias uses last average from unstable startup samples");
	}

	accel_offset[0] = IMU_ACCEL_FLAT_OFFSET_X_MPS2;
	accel_offset[1] = IMU_ACCEL_FLAT_OFFSET_Y_MPS2;
	accel_offset[2] = IMU_ACCEL_FLAT_OFFSET_Z_MPS2;
	accel_flat_calibrated = true;

	LOG_INF("IMU gyro bias=(%.5f, %.5f, %.5f), fixed accel offset=(%.5f, %.5f, %.5f)",
		(double)gyro_bias[0], (double)gyro_bias[1], (double)gyro_bias[2],
		(double)accel_offset[0], (double)accel_offset[1], (double)accel_offset[2]);
	LOG_DBG("IMU startup accel avg=(%.5f, %.5f, %.5f), norm_range=%.5f",
		(double)last_accel_avg[0], (double)last_accel_avg[1],
		(double)last_accel_avg[2], (double)accel_norm_range);

	return 0;
}

static bool compute_acc_mag_attitude(const imu_9axis_sample_t *sample,
				     float *roll, float *pitch, float *yaw)
{
	const float ax = sample->accel[0];
	const float ay = sample->accel[1];
	const float az = sample->accel[2];
	const float mx = sample->mag[0];
	const float my = sample->mag[1];
	const float mz = sample->mag[2];

	const float acc_norm_sq = ax * ax + ay * ay + az * az;
	const float mag_norm_sq = mx * mx + my * my + mz * mz;

	if (acc_norm_sq < IMU_MIN_NORM_SQ) {
		return false;
	}

	*roll = atan2f(ay, az);
	*pitch = atan2f(-ax, sqrtf(ay * ay + az * az));

	if (mag_norm_sq < IMU_MIN_NORM_SQ) {
		*yaw = 0.0f;
		return true;
	}

	const float sr = sinf(*roll);
	const float cr = cosf(*roll);
	const float sp = sinf(*pitch);
	const float cp = cosf(*pitch);

	const float yh = -my * cr + mz * sr;
	const float xh = mx * cp + my * sp * sr + mz * sp * cr;

	*yaw = atan2f(yh, xh);
	return true;
}

static void quaternion_from_euler(float roll, float pitch, float yaw, float q[4])
{
	const float cr = cosf(roll * 0.5f);
	const float sr = sinf(roll * 0.5f);
	const float cp = cosf(pitch * 0.5f);
	const float sp = sinf(pitch * 0.5f);
	const float cy = cosf(yaw * 0.5f);
	const float sy = sinf(yaw * 0.5f);

	q[0] = cr * cp * cy + sr * sp * sy;
	q[1] = sr * cp * cy - cr * sp * sy;
	q[2] = cr * sp * cy + sr * cp * sy;
	q[3] = cr * cp * sy - sr * sp * cy;
	normalize_quaternion(q);
}

static void quaternion_to_euler(const float q[4], float euler[3])
{
	const float sinr_cosp = 2.0f * (q[0] * q[1] + q[2] * q[3]);
	const float cosr_cosp = 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]);
	const float sinp = 2.0f * (q[0] * q[2] - q[3] * q[1]);
	const float siny_cosp = 2.0f * (q[0] * q[3] + q[1] * q[2]);
	const float cosy_cosp = 1.0f - 2.0f * (q[2] * q[2] + q[3] * q[3]);

	euler[IMU_EULER_ROLL] = atan2f(sinr_cosp, cosr_cosp);
	euler[IMU_EULER_PITCH] = asinf(clamp_float(sinp, -1.0f, 1.0f));
	euler[IMU_EULER_YAW] = wrap_pi(atan2f(siny_cosp, cosy_cosp));
}

static void update_accel_lpf(const float accel[3], float dt_s, float out[3])
{
	if (!filter_state.accel_lpf_valid) {
		filter_state.accel_lpf[0] = accel[0];
		filter_state.accel_lpf[1] = accel[1];
		filter_state.accel_lpf[2] = accel[2];
		filter_state.accel_lpf_valid = true;
	} else {
		const float alpha = dt_s / (IMU_ACCEL_LPF_TAU_S + dt_s);

		for (uint8_t axis = 0U; axis < 3U; axis++) {
			filter_state.accel_lpf[axis] +=
				alpha * (accel[axis] - filter_state.accel_lpf[axis]);
		}
	}

	out[0] = filter_state.accel_lpf[0];
	out[1] = filter_state.accel_lpf[1];
	out[2] = filter_state.accel_lpf[2];
}

static void mahony_update(const imu_9axis_sample_t *sample, float dt_s)
{
	float q0 = filter_state.q[0];
	float q1 = filter_state.q[1];
	float q2 = filter_state.q[2];
	float q3 = filter_state.q[3];
	float gx = sample->gyro[0];
	float gy = sample->gyro[1];
	float gz = sample->gyro[2];
	float accel[3];
	float mag[3] = {sample->mag[0], sample->mag[1], sample->mag[2]};
	float half_ex = 0.0f;
	float half_ey = 0.0f;
	float half_ez = 0.0f;
	bool correction_valid = false;

	update_accel_lpf(sample->accel, dt_s, accel);

	const float accel_norm =
		sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
	if (accel_norm >= IMU_ACCEL_CORRECT_MIN_NORM &&
	    accel_norm <= IMU_ACCEL_CORRECT_MAX_NORM &&
	    normalize_vec3(accel)) {
		const float half_vx = q1 * q3 - q0 * q2;
		const float half_vy = q0 * q1 + q2 * q3;
		const float half_vz = q0 * q0 - 0.5f + q3 * q3;

		half_ex += accel[1] * half_vz - accel[2] * half_vy;
		half_ey += accel[2] * half_vx - accel[0] * half_vz;
		half_ez += accel[0] * half_vy - accel[1] * half_vx;
		correction_valid = true;
	}

	if (normalize_vec3(mag)) {
		const float q0q1 = q0 * q1;
		const float q0q2 = q0 * q2;
		const float q0q3 = q0 * q3;
		const float q1q1 = q1 * q1;
		const float q1q2 = q1 * q2;
		const float q1q3 = q1 * q3;
		const float q2q2 = q2 * q2;
		const float q2q3 = q2 * q3;
		const float q3q3 = q3 * q3;

		const float hx = 2.0f * (mag[0] * (0.5f - q2q2 - q3q3) +
					 mag[1] * (q1q2 - q0q3) +
					 mag[2] * (q1q3 + q0q2));
		const float hy = 2.0f * (mag[0] * (q1q2 + q0q3) +
					 mag[1] * (0.5f - q1q1 - q3q3) +
					 mag[2] * (q2q3 - q0q1));
		const float bx = sqrtf(hx * hx + hy * hy);
		const float bz = 2.0f * (mag[0] * (q1q3 - q0q2) +
					 mag[1] * (q2q3 + q0q1) +
					 mag[2] * (0.5f - q1q1 - q2q2));

		const float half_wx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
		const float half_wy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
		const float half_wz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

		half_ex += IMU_MAHONY_MAG_WEIGHT * (mag[1] * half_wz - mag[2] * half_wy);
		half_ey += IMU_MAHONY_MAG_WEIGHT * (mag[2] * half_wx - mag[0] * half_wz);
		half_ez += IMU_MAHONY_MAG_WEIGHT * (mag[0] * half_wy - mag[1] * half_wx);
		correction_valid = true;
	}

	if (correction_valid) {
		if (IMU_MAHONY_TWO_KI > 0.0f) {
			filter_state.integral_error[0] += IMU_MAHONY_TWO_KI * half_ex * dt_s;
			filter_state.integral_error[1] += IMU_MAHONY_TWO_KI * half_ey * dt_s;
			filter_state.integral_error[2] += IMU_MAHONY_TWO_KI * half_ez * dt_s;

			gx += filter_state.integral_error[0];
			gy += filter_state.integral_error[1];
			gz += filter_state.integral_error[2];
		}

		gx += IMU_MAHONY_TWO_KP * half_ex;
		gy += IMU_MAHONY_TWO_KP * half_ey;
		gz += IMU_MAHONY_TWO_KP * half_ez;
	} else {
		filter_state.integral_error[0] = 0.0f;
		filter_state.integral_error[1] = 0.0f;
		filter_state.integral_error[2] = 0.0f;
	}

	gx *= 0.5f * dt_s;
	gy *= 0.5f * dt_s;
	gz *= 0.5f * dt_s;

	filter_state.q[0] += -q1 * gx - q2 * gy - q3 * gz;
	filter_state.q[1] +=  q0 * gx + q2 * gz - q3 * gy;
	filter_state.q[2] +=  q0 * gy - q1 * gz + q3 * gx;
	filter_state.q[3] +=  q0 * gz + q1 * gy - q2 * gx;
	normalize_quaternion(filter_state.q);
}

static void update_attitude(imu_9axis_sample_t *sample)
{
	uint64_t dt_ns = IMU_DEFAULT_DT_NS;
	float dt_s;

	if (filter_state.last_timestamp_ns != 0U &&
	    sample->timestamp_ns > filter_state.last_timestamp_ns) {
		dt_ns = sample->timestamp_ns - filter_state.last_timestamp_ns;
		if (dt_ns > IMU_MAX_DT_NS) {
			dt_ns = IMU_DEFAULT_DT_NS;
			filter_state.accel_lpf_valid = false;
		}
	}

	dt_s = (float)dt_ns / 1000000000.0f;

	if (!filter_state.attitude_valid) {
		float roll = 0.0f;
		float pitch = 0.0f;
		float yaw = 0.0f;

		if (compute_acc_mag_attitude(sample, &roll, &pitch, &yaw)) {
			quaternion_from_euler(roll, pitch, yaw, filter_state.q);
			filter_state.attitude_valid = true;
		} else {
			filter_state.q[0] = 1.0f;
			filter_state.q[1] = 0.0f;
			filter_state.q[2] = 0.0f;
			filter_state.q[3] = 0.0f;
			filter_state.attitude_valid = true;
		}
	} else {
		mahony_update(sample, dt_s);
	}

	filter_state.last_timestamp_ns = sample->timestamp_ns;
	quaternion_to_euler(filter_state.q, sample->euler);
}

int imu_9axis_init(uint8_t heater_duty_percent)
{
	float dummy_mag[3] = {0.0f, 0.0f, 0.0f};
	int rc;

	if (!device_is_ready(bmi_accel)) {
		LOG_ERR("bmi088-accel not ready");
		return -ENODEV;
	}
	if (!device_is_ready(bmi_gyro)) {
		LOG_ERR("bmi088-gyro not ready");
		return -ENODEV;
	}
	if (!device_is_ready(ist_mag)) {
		LOG_ERR("ist8310 not ready");
		return -ENODEV;
	}

	rc = fetch_xyz_retry(ist_mag, SENSOR_CHAN_MAGN_XYZ, dummy_mag,
			     IST8310_FETCH_RETRIES, IST8310_FETCH_DELAY_MS);
	if (rc < 0) {
		LOG_WRN("ist8310 first sample not ready yet: %d", rc);
	}

	rc = calibrate_startup_biases();
	if (rc < 0) {
		LOG_ERR("IMU startup calibration failed: %d", rc);
		return rc;
	}

	heater_available = pwm_is_ready_dt(&heater_pwm);
	if (!heater_available) {
		LOG_WRN("heater PWM not ready, continue without heater");
	} else {
		rc = apply_heater_duty(clamp_duty(heater_duty_percent));
		if (rc < 0) {
			LOG_WRN("heater PWM set failed: %d, continue without heater", rc);
			heater_available = false;
		}
	}

	filter_state.attitude_valid = false;
	filter_state.accel_lpf_valid = false;
	filter_state.q[0] = 1.0f;
	filter_state.q[1] = 0.0f;
	filter_state.q[2] = 0.0f;
	filter_state.q[3] = 0.0f;
	filter_state.integral_error[0] = 0.0f;
	filter_state.integral_error[1] = 0.0f;
	filter_state.integral_error[2] = 0.0f;
	filter_state.last_timestamp_ns = 0U;
	heater_pid_reset();

	LOG_INF("IMU 9-axis ready (heater duty = %u%%, gyro_cal=%d, accel_flat_cal=%d)",
		clamp_duty(heater_duty_percent), gyro_bias_calibrated ? 1 : 0,
		accel_flat_calibrated ? 1 : 0);
	return 0;
}

int imu_9axis_set_heater_duty(uint8_t duty_percent)
{
	if (!heater_available) {
		return -ENODEV;
	}

	return apply_heater_duty(clamp_duty(duty_percent));
}

void imu_9axis_enable_heater_pid(bool enable)
{
	heater_pid_enabled = enable;
	if (!enable) {
		heater_pid_reset();
	}
}

float imu_9axis_get_heater_output(void)
{
	return heater_pid_output;
}

int imu_9axis_sample(imu_9axis_sample_t *out)
{
	struct sensor_value temp;
	float raw[3];
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}

	rc = fetch_xyz_once(bmi_accel, SENSOR_CHAN_ACCEL_XYZ, raw);
	if (rc < 0) {
		LOG_DBG("accel fetch failed: %d", rc);
		return rc;
	}
	axis_align_vec3(raw, out->accel);
	for (uint8_t axis = 0U; axis < 3U; axis++) {
		out->accel[axis] -= accel_offset[axis];
	}

	if (sensor_channel_get(bmi_accel, SENSOR_CHAN_DIE_TEMP, &temp) == 0) {
		out->temp_c = (float)sensor_value_to_double(&temp);
	} else {
		out->temp_c = NAN;
	}

	rc = fetch_xyz_once(bmi_gyro, SENSOR_CHAN_GYRO_XYZ, raw);
	if (rc < 0) {
		LOG_DBG("gyro fetch failed: %d", rc);
		return rc;
	}
	axis_align_vec3(raw, out->gyro);
	for (uint8_t axis = 0U; axis < 3U; axis++) {
		out->gyro[axis] -= gyro_bias[axis];
	}

	rc = fetch_xyz_retry(ist_mag, SENSOR_CHAN_MAGN_XYZ, raw,
			     IST8310_FETCH_RETRIES, IST8310_FETCH_DELAY_MS);
	if (rc < 0) {
		LOG_DBG("mag fetch failed: %d", rc);
		return rc;
	}
	axis_align_vec3(raw, out->mag);

	out->timestamp_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	heater_pid_update(out->temp_c, out->timestamp_ns);
	update_attitude(out);
	return 0;
}
