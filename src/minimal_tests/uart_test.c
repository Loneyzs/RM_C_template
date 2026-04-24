/*
 * UART minimal test: verify the USART1 / board UART2 / COM11 path.
 */

#include "minimal_test_serial.h"
#include "unit_tests.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_uart, LOG_LEVEL_INF);

int test_uart_start(void)
{
	int rc = minimal_test_serial_init();

	if (rc < 0) {
		LOG_ERR("UART minimal test init failed: %d", rc);
		return rc;
	}

	minimal_test_serial_write("UART_TEST_OK\r\n");
	LOG_INF("UART minimal test banner sent");
	return 0;
}
