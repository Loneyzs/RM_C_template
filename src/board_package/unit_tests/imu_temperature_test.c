/*
 * IMU 温度最小测试。
 * 通过 UART2 输出 BMI088 die-temp，便于快速确认温度链路是否正确。
 */

#include "drivers/imu/imu_9axis.h"
#include "board_package/unit_tests/unit_tests.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(test_imu_temp, LOG_LEVEL_INF);

#define UART2_NODE DT_NODELABEL(usart1)
#define IMU_TEMP_TEST_PERIOD_MS 500U
#define IMU_TEMP_HEATER_DUTY    50U
#define IMU_TEMP_TEST_FORCE_FULL_ON 1

static const struct device *const uart2_dev = DEVICE_DT_GET(UART2_NODE);
static K_MUTEX_DEFINE(imu_temp_uart_lock);

K_THREAD_STACK_DEFINE(imu_temp_test_stack, 1536);
static struct k_thread imu_temp_test_thread_data;
static bool started;

static int uart2_init(void)
{
	if (!device_is_ready(uart2_dev)) {
		LOG_ERR("UART2 (USART1) not ready");
		return -ENODEV;
	}

	return 0;
}

static void uart2_write(const char *text)
{
	if (text == NULL) {
		return;
	}

	k_mutex_lock(&imu_temp_uart_lock, K_FOREVER);
	for (size_t i = 0; text[i] != '\0'; i++) {
		uart_poll_out(uart2_dev, text[i]);
	}
	k_mutex_unlock(&imu_temp_uart_lock);
}

static void uart2_printf(const char *fmt, ...)
{
	char buf[192];
	va_list args;

	if (fmt == NULL) {
		return;
	}

	va_start(args, fmt);
	vsnprintk(buf, sizeof(buf), fmt, args);
	va_end(args);

	uart2_write(buf);
}

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
			uart2_printf(
				"IMU_TEMP seq=%lu temp=%.2f heater=%.1f roll=%.3f pitch=%.3f yaw=%.3f\r\n",
				(unsigned long)seq,
				(double)sample.temp_c,
				(double)imu_9axis_get_heater_output(),
				(double)sample.euler[IMU_EULER_ROLL],
				(double)sample.euler[IMU_EULER_PITCH],
				(double)sample.euler[IMU_EULER_YAW]);
		} else {
			uart2_printf("IMU_TEMP_ERR seq=%lu rc=%d\r\n", (unsigned long)seq, rc);
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

	rc = uart2_init();
	if (rc < 0) {
		LOG_ERR("UART2 not ready: %d", rc);
		return rc;
	}

	rc = imu_9axis_init(IMU_TEMP_HEATER_DUTY);
	if (rc < 0) {
		LOG_ERR("imu_9axis_init failed: %d", rc);
		return rc;
	}

#if IMU_TEMP_TEST_FORCE_FULL_ON
	imu_9axis_enable_heater_pid(false);
	rc = imu_9axis_set_heater_duty(100U);
	if (rc < 0) {
		LOG_ERR("set heater full on failed: %d", rc);
		return rc;
	}
#else
	imu_9axis_enable_heater_pid(true);
#endif

	k_thread_create(&imu_temp_test_thread_data, imu_temp_test_stack,
			K_THREAD_STACK_SIZEOF(imu_temp_test_stack),
			imu_temp_test_thread, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	k_thread_name_set(&imu_temp_test_thread_data, "test_imu_temp");

	started = true;
	LOG_INF("IMU temp test started @ %u ms", IMU_TEMP_TEST_PERIOD_MS);
	return 0;
}

