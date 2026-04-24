/*
 * 旧的九轴融合 UART 回环测试，保留但默认不在 main 中启用。
 */

#include "unit_tests.h"
#include "imu_9axis.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(test_imu_uart, LOG_LEVEL_INF);

#define UART_NODE DT_NODELABEL(usart1)
#define SAMPLE_PERIOD_MS 20

static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

K_THREAD_STACK_DEFINE(imu_uart_stack, 1536);
static struct k_thread imu_uart_thread_data;
static bool started;

static const uint8_t justfloat_tail[4] = {0x00, 0x00, 0x80, 0x7F};

static void uart_write_bytes(const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

static void send_justfloat(const float *channels, size_t n_channels)
{
	for (size_t i = 0; i < n_channels; i++) {
		uint8_t bytes[4];
		uint32_t raw;

		memcpy(&raw, &channels[i], sizeof(raw));
		sys_put_le32(raw, bytes);
		uart_write_bytes(bytes, 4);
	}
	uart_write_bytes(justfloat_tail, sizeof(justfloat_tail));
}

static void compute_euler(const imu_9axis_sample_t *s,
			  float *roll, float *pitch, float *yaw)
{
	float ax = s->accel[0];
	float ay = s->accel[1];
	float az = s->accel[2];

	*roll = atan2f(ay, az);
	*pitch = atan2f(-ax, sqrtf(ay * ay + az * az));

	float mx = s->mag[0];
	float my = s->mag[1];
	float mz = s->mag[2];

	float sr = sinf(*roll);
	float cr = cosf(*roll);
	float sp = sinf(*pitch);
	float cp = cosf(*pitch);

	float yh = -my * cr + mz * sr;
	float xh = mx * cp + my * sp * sr + mz * sp * cr;
	*yaw = atan2f(yh, xh);
}

static void imu_uart_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	imu_9axis_sample_t s;
	uint32_t fail_count = 0;
	uint32_t ok_count = 0;

	float test[3] = {1.0f, 2.0f, 3.0f};
	send_justfloat(test, ARRAY_SIZE(test));

	while (1) {
		int rc = imu_9axis_sample(&s);
		if (rc == 0) {
			float roll, pitch, yaw;

			compute_euler(&s, &roll, &pitch, &yaw);
			float chans[3] = {roll, pitch, yaw};

			send_justfloat(chans, ARRAY_SIZE(chans));
			ok_count++;
			if ((ok_count % 50U) == 0U) {
				LOG_INF("justfloat ok=%u fail=%u last=(%.3f,%.3f,%.3f)",
					ok_count, fail_count,
					(double)roll, (double)pitch, (double)yaw);
			}
		} else {
			fail_count++;
			float zeros[3] = {0.0f, 0.0f, 0.0f};
			send_justfloat(zeros, ARRAY_SIZE(zeros));

			if ((fail_count % 10U) == 0U) {
				LOG_WRN("IMU sample failed %u times (rc=%d)", fail_count, rc);
			}
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}
}

int test_imu_uart_start(void)
{
	if (started) {
		return 0;
	}
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("USART1 not ready");
		return -ENODEV;
	}

	k_thread_create(&imu_uart_thread_data, imu_uart_stack,
			K_THREAD_STACK_SIZEOF(imu_uart_stack),
			imu_uart_thread, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&imu_uart_thread_data, "test_imu_uart");

	started = true;
	LOG_INF("IMU->USART1 JustFloat test started @ %d ms", SAMPLE_PERIOD_MS);
	return 0;
}
