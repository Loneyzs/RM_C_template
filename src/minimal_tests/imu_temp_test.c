/*
 * IMU 温度最小测试。
 * 通过 UART2 输出 BMI088 die-temp，便于快速确认温度链路是否正确。
 */

#include "imu_9axis.h"
#include "unit_tests.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "minimal_test_serial.h"

LOG_MODULE_REGISTER(test_imu_temp, LOG_LEVEL_INF);

#define IMU_TEMP_TEST_PERIOD_MS 500U
#define IMU_TEMP_HEATER_DUTY    50U

K_THREAD_STACK_DEFINE(imu_temp_test_stack, 1536);
static struct k_thread imu_temp_test_thread_data;
static bool started;

static void imu_temp_test_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	imu_9axis_sample_t sample;
	uint32_t seq = 0U;

	while (1) {
		int rc = imu_9axis_sample(&sample);
		if (rc == 0) {
			minimal_test_serial_printf(
				"IMU_TEMP seq=%lu temp=%.2f roll=%.3f pitch=%.3f yaw=%.3f\r\n",
				(unsigned long)seq,
				(double)sample.temp_c,
				(double)sample.euler[IMU_EULER_ROLL],
				(double)sample.euler[IMU_EULER_PITCH],
				(double)sample.euler[IMU_EULER_YAW]);
		} else {
			minimal_test_serial_printf("IMU_TEMP_ERR seq=%lu rc=%d\r\n",
						   (unsigned long)seq, rc);
		}

		seq++;
		k_msleep(IMU_TEMP_TEST_PERIOD_MS);
	}
}

int test_imu_temp_start(void)
{
	int rc;

	if (started) {
		return 0;
	}

	rc = minimal_test_serial_init();
	if (rc < 0) {
		LOG_ERR("UART2 not ready: %d", rc);
		return rc;
	}

	rc = imu_9axis_init(IMU_TEMP_HEATER_DUTY);
	if (rc < 0) {
		LOG_ERR("imu_9axis_init failed: %d", rc);
		return rc;
	}

	k_thread_create(&imu_temp_test_thread_data, imu_temp_test_stack,
			K_THREAD_STACK_SIZEOF(imu_temp_test_stack),
			imu_temp_test_thread, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&imu_temp_test_thread_data, "test_imu_temp");

	started = true;
	LOG_INF("IMU temp test started @ %u ms", IMU_TEMP_TEST_PERIOD_MS);
	return 0;
}
