/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UNIT_TESTS_H_
#define APP_UNIT_TESTS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 最小测试入口。
 *
 * 当前 main 只启动 BMI088 和 IST8310 的独立最小测试。
 * 其余接口继续保留，便于后续单独回归。
 */

int test_can_loopback_start(void);
int test_pwm_led_start(void);
int test_imu_uart_start(void);
int test_bmi088_minimal_start(void);
int test_ist8310_minimal_start(void);

/* 最小 UART 测试：只验证 COM11 串口链路。 */
void uart_minimal_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UNIT_TESTS_H_ */
