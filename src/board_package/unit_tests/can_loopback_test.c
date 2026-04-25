/*
 * CAN1 硬件回环最小测试。
 */

#include "board_package/unit_tests/unit_tests.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_can_loopback, LOG_LEVEL_INF);

#define CAN_NODE DT_NODELABEL(can1)
#define TEST_CAN_ID 0x123U
#define TX_PERIOD_MS 500

static const struct device *const can_dev = DEVICE_DT_GET(CAN_NODE);

K_THREAD_STACK_DEFINE(can_tx_stack, 1024);
static struct k_thread can_tx_thread_data;
static bool started;

CAN_MSGQ_DEFINE(rx_msgq, 4);

static void can_test_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct can_frame tx_frame = {
		.flags = 0,
		.id = TEST_CAN_ID,
		.dlc = 8,
	};
	struct can_frame rx_frame;
	uint32_t tx_count = 0;
	uint32_t rx_ok = 0;
	uint32_t rx_mismatch = 0;

	while (1) {
		for (int i = 0; i < 8; i++) {
			tx_frame.data[i] = (uint8_t)(tx_count + i);
		}

		int rc = can_send(can_dev, &tx_frame, K_MSEC(100), NULL, NULL);
		if (rc < 0) {
			LOG_WRN("can_send failed: %d", rc);
		}

		if (k_msgq_get(&rx_msgq, &rx_frame, K_MSEC(200)) == 0) {
			bool ok = (rx_frame.id == TEST_CAN_ID) && (rx_frame.dlc == 8);

			for (int i = 0; ok && i < 8; i++) {
				ok = rx_frame.data[i] == (uint8_t)(tx_count + i);
			}

			if (ok) {
				rx_ok++;
			} else {
				rx_mismatch++;
				LOG_WRN("CAN payload mismatch (tx_count=%u)", tx_count);
			}
		} else {
			LOG_WRN("CAN rx timeout (tx_count=%u)", tx_count);
		}

		if ((tx_count % 4U) == 0U) {
			LOG_INF("CAN loopback: tx=%u rx_ok=%u mismatch=%u",
				tx_count + 1U, rx_ok, rx_mismatch);
		}

		tx_count++;
		k_msleep(TX_PERIOD_MS);
	}
}

int test_can_loopback_start(void)
{
	if (started) {
		return 0;
	}

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN1 not ready");
		return -ENODEV;
	}

	int rc = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
	if (rc < 0) {
		LOG_ERR("can_set_mode LOOPBACK failed: %d", rc);
		return rc;
	}

	rc = can_start(can_dev);
	if (rc < 0) {
		LOG_ERR("can_start failed: %d", rc);
		return rc;
	}

	const struct can_filter filter = {
		.id = TEST_CAN_ID,
		.mask = CAN_STD_ID_MASK,
		.flags = 0,
	};

	rc = can_add_rx_filter_msgq(can_dev, &rx_msgq, &filter);
	if (rc < 0) {
		LOG_ERR("can_add_rx_filter_msgq failed: %d", rc);
		return rc;
	}

	k_thread_create(&can_tx_thread_data, can_tx_stack,
			K_THREAD_STACK_SIZEOF(can_tx_stack),
			can_test_thread, NULL, NULL, NULL,
			5, 0, K_NO_WAIT);
	k_thread_name_set(&can_tx_thread_data, "test_can_loop");

	started = true;
	LOG_INF("CAN1 loopback test started @ 500 kbit/s");
	return 0;
}

