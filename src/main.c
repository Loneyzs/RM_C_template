/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "imu_9axis.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* 默认恒定占空比：50% ≈ 0.29W，后续接 PID 时此处会被覆盖。*/
#define HEATER_DEFAULT_DUTY  50U

int main(void)
{
	LOG_INF("RM_C_Template boot: STM32F407IGH6 @ 168 MHz, console via SEGGER RTT");

	if (imu_9axis_init(HEATER_DEFAULT_DUTY) < 0) {
		LOG_ERR("IMU 9-axis init failed, halting sampling loop");
		return 0;
	}

	imu_9axis_sample_t s;

	while (1) {
		if (imu_9axis_sample(&s) == 0) {
			LOG_INF("a=% .2f,% .2f,% .2f m/s^2  g=% .3f,% .3f,% .3f rad/s  "
				"m=% .2f,% .2f,% .2f G  T=%.1f C",
				(double)s.accel[0], (double)s.accel[1], (double)s.accel[2],
				(double)s.gyro[0],  (double)s.gyro[1],  (double)s.gyro[2],
				(double)s.mag[0],   (double)s.mag[1],   (double)s.mag[2],
				(double)s.temp_c);
		}
		k_msleep(100);
	}

	return 0;
}
