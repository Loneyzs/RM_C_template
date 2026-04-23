/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "imu_9axis.h"
#include "unit_tests.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define HEATER_DEFAULT_DUTY  50U

int main(void)
{
	LOG_INF("RM_C_Template boot: STM32F407IGH6 @ 168 MHz, console via SEGGER RTT");

	if (imu_9axis_init(HEATER_DEFAULT_DUTY) < 0) {
		LOG_ERR("IMU 9-axis init failed — IMU-dependent tests will be skipped");
	} else {
		test_imu_uart_start();
	}

	test_can_loopback_start();
	test_pwm_led_start();

	LOG_INF("Unit-test threads launched; main returns to idle");
	return 0;
}
