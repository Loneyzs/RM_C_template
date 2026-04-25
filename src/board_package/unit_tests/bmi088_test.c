/*
 * 基于 Zephyr 上游 sensor API 的 BMI088 最小测试。
 * 直接分别读取 accel/gyro，不经过九轴融合层。
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "board_package/unit_tests/unit_tests.h"

LOG_MODULE_REGISTER(test_bmi088, LOG_LEVEL_INF);

#define BMI088_TEST_PERIOD_MS 200

static const struct device *const bmi_accel = DEVICE_DT_GET(DT_NODELABEL(bmi088_accel));
static const struct device *const bmi_gyro = DEVICE_DT_GET(DT_NODELABEL(bmi088_gyro));

K_THREAD_STACK_DEFINE(bmi088_test_stack, 1536);
static struct k_thread bmi088_test_thread_data;
static bool started;

static int fetch_vec3(const struct device *dev, enum sensor_channel chan, float out[3])
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

static void bmi088_test_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint32_t seq = 0;

	while (1) {
		float accel[3];
		float gyro[3];
		float temp_c = NAN;
		float anorm;
		float gnorm;
		struct sensor_value temp;

		int rc_acc = fetch_vec3(bmi_accel, SENSOR_CHAN_ACCEL_XYZ, accel);
		int rc_gyr = fetch_vec3(bmi_gyro, SENSOR_CHAN_GYRO_XYZ, gyro);

		if (rc_acc == 0 && sensor_channel_get(bmi_accel, SENSOR_CHAN_DIE_TEMP, &temp) == 0) {
			temp_c = (float)sensor_value_to_double(&temp);
		}

		if (rc_acc < 0 || rc_gyr < 0) {
			LOG_WRN("BMI088 ERR seq=%lu acc_rc=%d gyr_rc=%d",
				(unsigned long)seq, rc_acc, rc_gyr);
			k_msleep(BMI088_TEST_PERIOD_MS);
			seq++;
			continue;
		}

		anorm = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
		gnorm = sqrtf(gyro[0] * gyro[0] + gyro[1] * gyro[1] + gyro[2] * gyro[2]);

		LOG_INF("BMI088 OK seq=%lu acc=(%.3f,%.3f,%.3f) |a|=%.3f gyro=(%.3f,%.3f,%.3f) |g|=%.3f temp=%.2f",
			(unsigned long)seq,
			(double)accel[0], (double)accel[1], (double)accel[2], (double)anorm,
			(double)gyro[0], (double)gyro[1], (double)gyro[2], (double)gnorm,
			(double)temp_c);
		k_msleep(BMI088_TEST_PERIOD_MS);
		seq++;
	}
}

int test_bmi088_start(void)
{
	if (started) {
		return 0;
	}

	if (!device_is_ready(bmi_accel)) {
		LOG_ERR("bmi088-accel not ready");
		return -ENODEV;
	}
	if (!device_is_ready(bmi_gyro)) {
		LOG_ERR("bmi088-gyro not ready");
		return -ENODEV;
	}

	k_thread_create(&bmi088_test_thread_data, bmi088_test_stack,
			K_THREAD_STACK_SIZEOF(bmi088_test_stack),
			bmi088_test_thread, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&bmi088_test_thread_data, "test_bmi088");

	started = true;
	LOG_INF("BMI088 minimal test started");
	return 0;
}

