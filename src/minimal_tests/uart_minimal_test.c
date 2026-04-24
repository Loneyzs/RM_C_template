/*
 * 最小 UART 测试：只验证 UART2(USART1) 到上位机串口链路是否通。
 */

#include "minimal_test_serial.h"

#include <zephyr/logging/log.h>

#include "unit_tests.h"

LOG_MODULE_REGISTER(uart_minimal_test, LOG_LEVEL_INF);

void uart_minimal_test(void)
{
	int rc = minimal_test_serial_init();

	if (rc < 0) {
		LOG_ERR("UART minimal test init failed: %d", rc);
		return;
	}

	minimal_test_serial_write("UART_TEST_OK\r\n");
	LOG_INF("UART minimal test banner sent");
}
