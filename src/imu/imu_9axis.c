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

#define IMU_DEFAULT_DT_NS      2000000ULL
#define IMU_MAX_DT_NS          100000000ULL
#define IMU_RP_ALPHA           0.98f
#define IMU_YAW_ALPHA          0.95f
#define IMU_MIN_NORM_SQ        1.0e-6f
#define IMU_PI                 3.14159265358979323846f

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
	float roll;
	float pitch;
	float yaw;
	uint64_t last_timestamp_ns;
};

static struct imu_filter_state filter_state;
static bool heater_available;
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

static int apply_heater_duty(uint8_t duty)
{
	uint32_t pulse = (uint32_t)(((uint64_t)heater_pwm.period * duty) / 100U);

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

	if (acc_norm_sq < IMU_MIN_NORM_SQ || mag_norm_sq < IMU_MIN_NORM_SQ) {
		return false;
	}

	*roll = atan2f(ay, az);
	*pitch = atan2f(-ax, sqrtf(ay * ay + az * az));

	const float sr = sinf(*roll);
	const float cr = cosf(*roll);
	const float sp = sinf(*pitch);
	const float cp = cosf(*pitch);

	const float yh = -my * cr + mz * sr;
	const float xh = mx * cp + my * sp * sr + mz * sp * cr;

	*yaw = atan2f(yh, xh);
	return true;
}

static void update_attitude(imu_9axis_sample_t *sample)
{
	float roll_meas = 0.0f;
	float pitch_meas = 0.0f;
	float yaw_meas = 0.0f;
	bool attitude_measurement_valid;

	attitude_measurement_valid =
		compute_acc_mag_attitude(sample, &roll_meas, &pitch_meas, &yaw_meas);

	if (!filter_state.attitude_valid || !attitude_measurement_valid) {
		if (attitude_measurement_valid) {
			filter_state.roll = roll_meas;
			filter_state.pitch = pitch_meas;
			filter_state.yaw = yaw_meas;
			filter_state.attitude_valid = true;
		}
	} else {
		uint64_t dt_ns = IMU_DEFAULT_DT_NS;

		if (filter_state.last_timestamp_ns != 0U &&
		    sample->timestamp_ns > filter_state.last_timestamp_ns) {
			dt_ns = sample->timestamp_ns - filter_state.last_timestamp_ns;
			if (dt_ns > IMU_MAX_DT_NS) {
				dt_ns = IMU_DEFAULT_DT_NS;
			}
		}

		const float dt_s = (float)dt_ns / 1000000000.0f;
		const float pred_roll = filter_state.roll + sample->gyro[0] * dt_s;
		const float pred_pitch = filter_state.pitch + sample->gyro[1] * dt_s;
		const float pred_yaw = wrap_pi(filter_state.yaw + sample->gyro[2] * dt_s);

		filter_state.roll = IMU_RP_ALPHA * pred_roll + (1.0f - IMU_RP_ALPHA) * roll_meas;
		filter_state.pitch =
			IMU_RP_ALPHA * pred_pitch + (1.0f - IMU_RP_ALPHA) * pitch_meas;

		const float yaw_err = wrap_pi(yaw_meas - pred_yaw);
		filter_state.yaw = wrap_pi(pred_yaw + (1.0f - IMU_YAW_ALPHA) * yaw_err);
	}

	filter_state.last_timestamp_ns = sample->timestamp_ns;

	sample->euler[IMU_EULER_ROLL] = filter_state.roll;
	sample->euler[IMU_EULER_PITCH] = filter_state.pitch;
	sample->euler[IMU_EULER_YAW] = filter_state.yaw;
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
	filter_state.roll = 0.0f;
	filter_state.pitch = 0.0f;
	filter_state.yaw = 0.0f;
	filter_state.last_timestamp_ns = 0U;
	heater_pid_reset();

	LOG_INF("IMU 9-axis ready (heater duty = %u%%)", clamp_duty(heater_duty_percent));
	return 0;
}

int imu_9axis_set_heater_duty(uint8_t duty_percent)
{
	if (!heater_available) {
		return -ENODEV;
	}

	return apply_heater_duty(clamp_duty(duty_percent));
}

int imu_9axis_sample(imu_9axis_sample_t *out)
{
	struct sensor_value temp;
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}

	rc = fetch_xyz_once(bmi_accel, SENSOR_CHAN_ACCEL_XYZ, out->accel);
	if (rc < 0) {
		LOG_DBG("accel fetch failed: %d", rc);
		return rc;
	}

	if (sensor_channel_get(bmi_accel, SENSOR_CHAN_DIE_TEMP, &temp) == 0) {
		out->temp_c = (float)sensor_value_to_double(&temp);
	} else {
		out->temp_c = NAN;
	}

	rc = fetch_xyz_once(bmi_gyro, SENSOR_CHAN_GYRO_XYZ, out->gyro);
	if (rc < 0) {
		LOG_DBG("gyro fetch failed: %d", rc);
		return rc;
	}

	rc = fetch_xyz_retry(ist_mag, SENSOR_CHAN_MAGN_XYZ, out->mag,
			     IST8310_FETCH_RETRIES, IST8310_FETCH_DELAY_MS);
	if (rc < 0) {
		LOG_DBG("mag fetch failed: %d", rc);
		return rc;
	}

	out->timestamp_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	heater_pid_update(out->temp_c, out->timestamp_ns);
	update_attitude(out);
	return 0;
}
