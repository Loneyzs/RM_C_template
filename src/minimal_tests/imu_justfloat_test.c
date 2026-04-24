/*
 * 整合后的九轴 IMU 驱动最小测试。
 * 通过 UART2 以 justfloat 格式输出 roll / pitch / yaw。
 */

#include "imu_9axis.h"
#include "unit_tests.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(test_imu_justfloat, LOG_LEVEL_INF);

#define UART_NODE            DT_NODELABEL(usart1)
#define IMU_JF_PERIOD_MS     20U
#define IMU_HEATER_DUTY      50U

static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

K_THREAD_STACK_DEFINE(imu_justfloat_stack, 1792);
static struct k_thread imu_justfloat_thread_data;
static bool started;

static const uint8_t justfloat_tail[4] = {0x00, 0x00, 0x80, 0x7F};

static void uart_write_bytes(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

static void send_justfloat3(float a, float b, float c)
{
	const float values[3] = {a, b, c};

	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		uint32_t raw;
		uint8_t bytes[4];

		memcpy(&raw, &values[i], sizeof(raw));
		sys_put_le32(raw, bytes);
		uart_write_bytes(bytes, sizeof(bytes));
	}

	uart_write_bytes(justfloat_tail, sizeof(justfloat_tail));
}

static void imu_justfloat_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	imu_9axis_sample_t sample;
	uint32_t ok_count = 0U;
	uint32_t fail_count = 0U;

	send_justfloat3(1.0f, 2.0f, 3.0f);

	while (1) {
		int rc = imu_9axis_sample(&sample);
		if (rc == 0) {
			send_justfloat3(sample.euler[IMU_EULER_ROLL],
					sample.euler[IMU_EULER_PITCH],
					sample.euler[IMU_EULER_YAW]);
			ok_count++;
			if ((ok_count % 50U) == 0U) {
				LOG_INF("imu justfloat ok=%u fail=%u euler=(%.3f,%.3f,%.3f)",
					ok_count, fail_count,
					(double)sample.euler[IMU_EULER_ROLL],
					(double)sample.euler[IMU_EULER_PITCH],
					(double)sample.euler[IMU_EULER_YAW]);
			}
		} else {
			fail_count++;
			send_justfloat3(0.0f, 0.0f, 0.0f);
			if ((fail_count % 10U) == 0U) {
				LOG_WRN("imu justfloat sample failed %u times (rc=%d)",
					fail_count, rc);
			}
		}

		k_msleep(IMU_JF_PERIOD_MS);
	}
}

int test_imu_justfloat_start(void)
{
	int rc;

	if (started) {
		return 0;
	}

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("USART1 not ready");
		return -ENODEV;
	}

	rc = imu_9axis_init(IMU_HEATER_DUTY);
	if (rc < 0) {
		LOG_ERR("imu_9axis_init failed: %d", rc);
		return rc;
	}

	k_thread_create(&imu_justfloat_thread_data, imu_justfloat_stack,
			K_THREAD_STACK_SIZEOF(imu_justfloat_stack),
			imu_justfloat_thread, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&imu_justfloat_thread_data, "test_imu_jf");

	started = true;
	LOG_INF("IMU justfloat test started @ %u ms", IMU_JF_PERIOD_MS);
	return 0;
}
