#include "board_package/unit_tests/unit_tests.h"

#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_uart, LOG_LEVEL_INF);

#define UART2_NODE DT_NODELABEL(usart1)

static const struct device *const uart2_dev = DEVICE_DT_GET(UART2_NODE);
static K_MUTEX_DEFINE(serial_lock);

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
	if (uart2_init() < 0 || text == NULL) {
		return;
	}

	k_mutex_lock(&serial_lock, K_FOREVER);
	for (size_t i = 0; text[i] != '\0'; i++) {
		uart_poll_out(uart2_dev, text[i]);
	}
	k_mutex_unlock(&serial_lock);
}

int test_uart_start(void)
{
	int rc = uart2_init();

	if (rc < 0) {
		LOG_ERR("UART2 minimal test init failed: %d", rc);
		return rc;
	}

	uart2_write("UART2_TEST_OK\r\n");
	LOG_INF("UART2 minimal test banner sent");
	return 0;
}

