/*
 * PWM1 + LED_G 最小测试。
 */

#include "unit_tests.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_pwm_led, LOG_LEVEL_INF);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define LED_G_NODE DT_NODELABEL(led_g)

static const struct pwm_dt_spec blink_pwm =
	PWM_DT_SPEC_GET_BY_NAME(ZEPHYR_USER_NODE, blink);
static const struct gpio_dt_spec led_g =
	GPIO_DT_SPEC_GET(LED_G_NODE, gpios);

K_THREAD_STACK_DEFINE(pwm_led_stack, 768);
static struct k_thread pwm_led_thread_data;
static bool started;

static atomic_t period_ns_atomic = ATOMIC_INIT(1000000000);

static void pwm_led_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	bool on = false;

	while (1) {
		uint32_t period_ns = (uint32_t)atomic_get(&period_ns_atomic);
		uint32_t pulse_ns = period_ns / 2U;

		int rc = pwm_set_dt(&blink_pwm, period_ns, pulse_ns);
		if (rc < 0) {
			LOG_WRN("pwm_set_dt failed: %d (period=%u ns)", rc, period_ns);
		}

		on = !on;
		gpio_pin_set_dt(&led_g, on ? 1 : 0);
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
