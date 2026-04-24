/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 当前主程序只启动 IMU 温度最小测试。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "unit_tests.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	int rc;

	LOG_INF("=== IMU temperature minimal test ===");
	k_msleep(200);

	rc = test_imu_temp_start();
	if (rc < 0) {
		LOG_ERR("IMU temp test start failed: %d", rc);
	}

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
