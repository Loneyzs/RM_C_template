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

/**
 * @file
 * @brief 三个快速单元测试线程启动入口。
 *
 * 调用者（通常是 main）在完成板级硬件初始化后依次触发：
 *   - test_can_loopback_start()：启动 CAN1 硬件回环收发线程
 *   - test_pwm_led_start()     ：启动 PWM1 频率输出 + LED_G 软件翻转线程
 *   - test_imu_uart_start()    ：启动 IMU 欧拉角 → USART1 (JustFloat) 线程
 *
 * 每个 start 函数都是幂等的：同一进程内重复调用只会创建一个线程。
 */

int test_can_loopback_start(void);
int test_pwm_led_start(void);
int test_imu_uart_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UNIT_TESTS_H_ */
