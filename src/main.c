/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 当前主程序只启动 BMI088 和 IST8310 的独立最小测试。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "unit_tests.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	int rc;

	LOG_INF("=== IMU minimal tests: BMI088 + IST8310 ===");
	k_msleep(200);

	uart_minimal_test();

	rc = test_bmi088_minimal_start();
	if (rc < 0) {
		LOG_ERR("BMI088 minimal test start failed: %d", rc);
	}

	rc = test_ist8310_minimal_start();
	if (rc < 0) {
		LOG_ERR("IST8310 minimal test start failed: %d", rc);
	}

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
