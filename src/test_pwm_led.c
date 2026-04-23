/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM1 + LED_G 测试线程：
 *   · PWM1 (TIM1_CH1 @ PE9) 以 1 Hz、占空比 50% 输出方波，可示波器验证；
 *   · 同频率翻转板载绿色 LED (PH11)，以肉眼指示线程在运行。
 *
 * PWM 周期可通过 test_pwm_led_set_period_ns() 动态更改。
 */

#include "unit_tests.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_pwm_led, LOG_LEVEL_INF);

#define ZEPHYR_USER_NODE   DT_PATH(zephyr_user)
#define LED_G_NODE         DT_NODELABEL(led_g)

static const struct pwm_dt_spec blink_pwm =
	PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER_NODE, blink);
static const struct gpio_dt_spec led_g =
	GPIO_DT_SPEC_GET(LED_G_NODE, gpios);

K_THREAD_STACK_DEFINE(pwm_led_stack, 768);
static struct k_thread pwm_led_thread_data;
static bool started;

static atomic_t period_ns_atomic = ATOMIC_INIT(1000000000); /* 1 Hz 默认 */

static void pwm_led_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	bool on = false;

	while (1) {
		uint32_t period_ns = (uint32_t)atomic_get(&period_ns_atomic);
		uint32_t pulse_ns  = period_ns / 2U;

		int rc = pwm_set_dt(&blink_pwm, period_ns, pulse_ns);
		if (rc < 0) {
			LOG_WRN("pwm_set_dt failed: %d (period=%u ns)", rc, period_ns);
		}

		on = !on;
		gpio_pin_set_dt(&led_g, on ? 1 : 0);

		/* LED 翻转一次 = PWM 半周期；两次翻转对应 PWM 一个完整周期。 */
		k_usleep(period_ns / 2000U);
	}
}

int test_pwm_led_start(void)
{
	if (started) {
		return 0;
	}

	if (!pwm_is_ready_dt(&blink_pwm)) {
		LOG_ERR("PWM1 (blink) not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&led_g)) {
		LOG_ERR("LED_G GPIO not ready");
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	if (rc < 0) {
		LOG_ERR("LED_G configure failed: %d", rc);
		return rc;
	}

	k_thread_create(&pwm_led_thread_data, pwm_led_stack,
			K_THREAD_STACK_SIZEOF(pwm_led_stack),
			pwm_led_thread, NULL, NULL, NULL,
			6, 0, K_NO_WAIT);
	k_thread_name_set(&pwm_led_thread_data, "test_pwm_led");

	started = true;
	LOG_INF("PWM1 blink test started (1 Hz default, PE9 + LED_G)");
	return 0;
}
