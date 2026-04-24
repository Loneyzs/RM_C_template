#include "minimal_test_serial.h"

#include <stdarg.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(minimal_test_serial, LOG_LEVEL_INF);

#define UART_NODE DT_NODELABEL(usart1)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);
static K_MUTEX_DEFINE(serial_lock);

int minimal_test_serial_init(void)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("USART1 not ready");
		return -ENODEV;
	}

	return 0;
}

void minimal_test_serial_write(const char *text)
{
	if (minimal_test_serial_init() < 0 || text == NULL) {
		return;
	}

	k_mutex_lock(&serial_lock, K_FOREVER);
	for (size_t i = 0; text[i] != '\0'; i++) {
		uart_poll_out(uart_dev, text[i]);
	}
	k_mutex_unlock(&serial_lock);
}

void minimal_test_serial_printf(const char *fmt, ...)
{
	char buf[192];
	va_list args;

	if (minimal_test_serial_init() < 0 || fmt == NULL) {
		return;
	}

	va_start(args, fmt);
	vsnprintk(buf, sizeof(buf), fmt, args);
	va_end(args);

	minimal_test_serial_write(buf);
}
