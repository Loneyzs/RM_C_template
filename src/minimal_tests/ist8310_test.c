/*
 * 基于 Zephyr 上游 sensor API 的 IST8310 最小测试。
 * 直接读取磁力计输出，不经过九轴融合层。
 */

#include "minimal_test_serial.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "unit_tests.h"

LOG_MODULE_REGISTER(test_ist8310, LOG_LEVEL_INF);

#define IST8310_TEST_PERIOD_MS 300
#define IST8310_FETCH_RETRIES 5
#define IST8310_FETCH_DELAY_MS 5

static const struct device *const ist_mag = DEVICE_DT_GET(DT_NODELABEL(ist8310));

K_THREAD_STACK_DEFINE(ist8310_test_stack, 1280);
static struct k_thread ist8310_test_thread_data;
static bool started;

static int fetch_mag_xyz(float mag[3])
{
	struct sensor_value values[3];
	int rc = -EIO;

	for (uint8_t attempt = 0; attempt < IST8310_FETCH_RETRIES; attempt++) {
		rc = sensor_sample_fetch(ist_mag);
		if (rc == 0) {
			rc = sensor_channel_get(ist_mag, SENSOR_CHAN_MAGN_XYZ, values);
		}

		if (rc == 0) {
			mag[0] = (float)sensor_value_to_double(&values[0]);
			mag[1] = (float)sensor_value_to_double(&values[1]);
			mag[2] = (float)sensor_value_to_double(&values[2]);
			return 0;
		}

		if ((attempt + 1U) < IST8310_FETCH_RETRIES) {
			k_msleep(IST8310_FETCH_DELAY_MS);
		}
	}

	return rc;
}

static void ist8310_test_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint32_t seq = 0;

	while (1) {
		float mag[3] = {0.0f, 0.0f, 0.0f};
		int rc = fetch_mag_xyz(mag);

		if (rc < 0) {
			minimal_test_serial_printf("IST8310 ERR seq=%lu rc=%d\r\n",
						   (unsigned long)seq, rc);
			k_msleep(IST8310_TEST_PERIOD_MS);
			seq++;
			continue;
		}

		float mnorm = sqrtf(mag[0] * mag[0] + mag[1] * mag[1] + mag[2] * mag[2]);

		minimal_test_serial_printf(
			"IST8310 OK seq=%lu mag=(%.3f,%.3f,%.3f) |m|=%.3f\r\n",
			(unsigned long)seq,
			(double)mag[0], (double)mag[1], (double)mag[2], (double)mnorm);
		k_msleep(IST8310_TEST_PERIOD_MS);
		seq++;
	}
}

int test_ist8310_start(void)
{
	if (started) {
		return 0;
	}

	if (!device_is_ready(ist_mag)) {
		LOG_ERR("ist8310 not ready");
		return -ENODEV;
	}
	if (minimal_test_serial_init() < 0) {
		return -ENODEV;
	}

	minimal_test_serial_write("IST8310_TEST_START\r\n");

	k_thread_create(&ist8310_test_thread_data, ist8310_test_stack,
			K_THREAD_STACK_SIZEOF(ist8310_test_stack),
			ist8310_test_thread, NULL, NULL, NULL,
			8, 0, K_NO_WAIT);
	k_thread_name_set(&ist8310_test_thread_data, "test_ist8310");

	started = true;
	LOG_INF("IST8310 minimal test started");
	return 0;
}
