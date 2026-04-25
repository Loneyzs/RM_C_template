/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_IMU_9AXIS_H_
#define APP_IMU_9AXIS_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief 九轴 IMU 聚合与姿态接口（BMI088 + IST8310）
 *
 * 该层直接使用 Zephyr 上游 sensor API（bosch,bmi08x-accel/gyro、isentek,ist8310），
 * 在单独 IMU 目录下完成：
 *   1. 读取 accel / gyro / mag / die-temp 原始物理量；
 *   2. 管理 IST8310 单次采样重试与加热 PWM；
 *   3. 基于 accel + gyro + mag 做轻量姿态融合，输出 roll / pitch / yaw。
 *
 * 当前输出姿态仍处于“工程可验证”阶段，优先目标是保证链路闭环和数值连续，
 * 而不是追求最终控制精度。
 */

/** 九轴采样帧。单位严格遵循 Zephyr sensor API：
 *  - accel: m/s^2      (SENSOR_CHAN_ACCEL_XYZ)
 *  - gyro : rad/s      (SENSOR_CHAN_GYRO_XYZ)
 *  - mag  : Gauss      (SENSOR_CHAN_MAGN_XYZ, IST8310 驱动内部按灵敏度换算)
 *  - temp : 摄氏度     (SENSOR_CHAN_DIE_TEMP, 来自 BMI088 accel die-temp)
 */
enum {
	IMU_EULER_ROLL = 0,
	IMU_EULER_PITCH = 1,
	IMU_EULER_YAW = 2,
};

typedef struct {
	float accel[3];
	float gyro[3];
	float mag[3];
	float euler[3];         /* rad: [roll, pitch, yaw] */
	float temp_c;
	uint64_t timestamp_ns; /* k_uptime_ticks() 对应的单调时间戳 */
} imu_9axis_sample_t;

/**
 * @brief 初始化九轴 IMU 子系统。
 *
 * 流程：
 *   - 等待 bmi088-accel / bmi088-gyro / ist8310 三个设备 ready；
 *   - 预热一次 IST8310 采样链路（硬复位由更早的 board init 阶段完成）；
 *   - 以 @p heater_duty_percent 启动恒温加热 PWM（0 表示关闭，100 为满功率 0.58W）；
 *   - 清零融合状态，等待首帧建立姿态基准。
 *
 * @param heater_duty_percent 加热 PWM 占空比百分比，0~100，超过 100 按 100 处理。
 * @retval 0          全部子设备就绪；
 * @retval -ENODEV    任一子设备未 ready；
 * @retval -EIO       RSTN GPIO 或加热 PWM 配置失败；
 * @retval 其它负值   直接来自底层驱动。
 */
int imu_9axis_init(uint8_t heater_duty_percent);

/**
 * @brief 采样一次 9 轴数据。
 *
 * 内部对 accel / gyro / mag 各调用一次 sensor_sample_fetch()+channel_get()，
 * 然后执行一次轻量姿态融合，将 roll / pitch / yaw 写入 `out->euler[]`。
 * 任一子设备失败则整体失败并保留 @p out 内容不变。
 *
 * @param[out] out 输出采样帧，不可为 NULL。
 * @retval 0       成功；
 * @retval -EINVAL out 为 NULL；
 * @retval 其它    来自底层驱动的错误码。
 */
int imu_9axis_sample(imu_9axis_sample_t *out);

/**
 * @brief 动态调整加热占空比。
 *
 * 用于后续接入温控闭环（目前上层以恒定占空比运行）。
 *
 * @param duty_percent 0~100；越界按就近钳位。
 * @retval 0 成功；其它为底层 pwm_set() 错误码。
 */
int imu_9axis_set_heater_duty(uint8_t duty_percent);
void imu_9axis_enable_heater_pid(bool enable);
float imu_9axis_get_heater_output(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_IMU_9AXIS_H_ */
