/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "imu_9axis.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(imu_9axis, CONFIG_SENSOR_LOG_LEVEL);

#define ZEPHYR_USER_NODE  DT_PATH(zephyr_user)

static const struct device *const bmi_accel = DEVICE_DT_GET(DT_NODELABEL(bmi088_accel));
static const struct device *const bmi_gyro  = DEVICE_DT_GET(DT_NODELABEL(bmi088_gyro));
static const struct device *const ist_mag   = DEVICE_DT_GET(DT_NODELABEL(ist8310));

static const struct gpio_dt_spec ist_reset =
	GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, ist8310_reset_gpios);

static const struct pwm_dt_spec heater_pwm =
	PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER_NODE, heater);

static uint8_t clamp_duty(uint8_t duty)
{
	return duty > 100U ? 100U : duty;
}

static int apply_heater_duty(uint8_t duty)
{
	uint32_t pulse = (uint64_t)heater_pwm.period * duty / 100U;

	return pwm_set_dt(&heater_pwm, heater_pwm.period, pulse);
}

static int ist8310_hw_reset(void)
{
	int rc;

	if (!gpio_is_ready_dt(&ist_reset)) {
		LOG_ERR("IST8310 RSTN GPIO not ready");
		return -EIO;
	}

	rc = gpio_pin_configure_dt(&ist_reset, GPIO_OUTPUT_ACTIVE);
	if (rc < 0) {
		return rc;
	}
	/* 拉低（active）保持 ≥ 50ms 以兼顾 IST8310 内部上电时序。*/
	k_msleep(50);
	rc = gpio_pin_set_dt(&ist_reset, 0);
	if (rc < 0) {
		return rc;
	}
	k_msleep(50);

	return 0;
}

int imu_9axis_init(uint8_t heater_duty_percent)
{
	int rc;

	if (!device_is_ready(bmi_accel)) {
		LOG_ERR("bmi088-accel not ready");
		return -ENODEV;
	}
	if (!device_is_ready(bmi_gyro)) {
		LOG_ERR("bmi088-gyro not ready");
		return -ENODEV;
	}

	/* IST8310 的 probe 在 POST_KERNEL 时已完成，但我们的硬复位晚于 probe，
	 * 这对首次 sample_fetch 可能造成一次误读；实践上先复位再做一次 fetch 丢弃即可。
	 */
	rc = ist8310_hw_reset();
	if (rc < 0) {
		LOG_ERR("IST8310 reset failed: %d", rc);
		return rc;
	}

	if (!device_is_ready(ist_mag)) {
		LOG_ERR("ist8310 not ready");
		return -ENODEV;
	}

	if (!pwm_is_ready_dt(&heater_pwm)) {
		LOG_ERR("heater PWM not ready");
		return -EIO;
	}
	rc = apply_heater_duty(clamp_duty(heater_duty_percent));
	if (rc < 0) {
		LOG_ERR("heater PWM set failed: %d", rc);
		return rc;
	}

	LOG_INF("IMU 9-axis ready (heater duty = %u%%)", clamp_duty(heater_duty_percent));
	return 0;
}

int imu_9axis_set_heater_duty(uint8_t duty_percent)
{
	return apply_heater_duty(clamp_duty(duty_percent));
}

static int fetch_xyz(const struct device *dev, enum sensor_channel chan, float out[3])
{
	struct sensor_value v[3];
	int rc;

	rc = sensor_sample_fetch(dev);
	if (rc < 0) {
		return rc;
	}
	rc = sensor_channel_get(dev, chan, v);
	if (rc < 0) {
		return rc;
	}
	out[0] = (float)sensor_value_to_double(&v[0]);
	out[1] = (float)sensor_value_to_double(&v[1]);
	out[2] = (float)sensor_value_to_double(&v[2]);
	return 0;
}

int imu_9axis_sample(imu_9axis_sample_t *out)
{
	struct sensor_value t;
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}

	rc = fetch_xyz(bmi_accel, SENSOR_CHAN_ACCEL_XYZ, out->accel);
	if (rc < 0) {
		LOG_DBG("accel fetch failed: %d", rc);
		return rc;
	}
	/* die-temp 来自 accel，这里顺带取出；gyro 没有温度通道。*/
	if (sensor_channel_get(bmi_accel, SENSOR_CHAN_DIE_TEMP, &t) == 0) {
		out->temp_c = (float)sensor_value_to_double(&t);
	}

	rc = fetch_xyz(bmi_gyro, SENSOR_CHAN_GYRO_XYZ, out->gyro);
	if (rc < 0) {
		LOG_DBG("gyro fetch failed: %d", rc);
		return rc;
	}

	rc = fetch_xyz(ist_mag, SENSOR_CHAN_MAGN_XYZ, out->mag);
	if (rc < 0) {
		LOG_DBG("mag fetch failed: %d", rc);
		return rc;
	}

	out->timestamp_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	return 0;
}
