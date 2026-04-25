/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(imu_board_init, CONFIG_SENSOR_LOG_LEVEL);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define RM_C_IMU_INIT_PRIORITY 80

static const struct gpio_dt_spec ist_reset =
	GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, ist8310_reset_gpios);

static int rm_c_imu_board_init(void)
{
	int rc;

	if (!gpio_is_ready_dt(&ist_reset)) {
		LOG_ERR("IST8310 RSTN GPIO not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&ist_reset, GPIO_OUTPUT_ACTIVE);
	if (rc < 0) {
		LOG_ERR("failed to drive IST8310 reset low: %d", rc);
		return rc;
	}

	k_msleep(50);

	rc = gpio_pin_set_dt(&ist_reset, 0);
	if (rc < 0) {
		LOG_ERR("failed to release IST8310 reset: %d", rc);
		return rc;
	}

	k_msleep(50);
	LOG_INF("IST8310 hardware reset completed before sensor init");
	return 0;
}

SYS_INIT(rm_c_imu_board_init, POST_KERNEL, RM_C_IMU_INIT_PRIORITY);
