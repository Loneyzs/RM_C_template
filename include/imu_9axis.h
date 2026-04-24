/*
 * Copyright (c) 2026 RoboMaster C-Type Zephyr Adaptation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_IMU_9AXIS_H_
#define APP_IMU_9AXIS_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief 九轴 IMU 接口层（BMI088 + IST8310）
 *
 * 该层直接使用 Zephyr 上游 sensor API（bosch,bmi08x-accel/gyro、isentek,ist8310），
 * 只做三件事：
 *   1. 将三个子设备的 accel(m/s^2) / gyro(rad/s) / mag(G) / die-temp 汇总到
 *      一个 `imu_9axis_sample_t` 结构中，带单调时间戳；
 *   2. 对 IST8310 的 single-shot 采样做有限重试，屏蔽首次转换未就绪窗口；
 *   3. 以恒定占空比启动 IMU 恒温加热 PWM（后续可替换为 PID 闭环）。
 *
 * 坐标系保持各传感器的出厂坐标系，不做板级旋转对齐——若后续需要机体坐标系，
 * 建议在 fusion/姿态层统一处理。
 */

/** 九轴采样帧。单位严格遵循 Zephyr sensor API：
 *  - accel: m/s^2      (SENSOR_CHAN_ACCEL_XYZ)
 *  - gyro : rad/s      (SENSOR_CHAN_GYRO_XYZ)
 *  - mag  : Gauss      (SENSOR_CHAN_MAGN_XYZ, IST8310 驱动内部按灵敏度换算)
 *  - temp : 摄氏度     (SENSOR_CHAN_DIE_TEMP, 来自 BMI088 accel die-temp)
 */
typedef struct {
	float accel[3];
	float gyro[3];
	float mag[3];
	float temp_c;
	uint64_t timestamp_ns; /* k_uptime_ticks() 对应的单调时间戳 */
} imu_9axis_sample_t;

/**
 * @brief 初始化九轴 IMU 子系统。
 *
 * 流程：
 *   - 等待 bmi088-accel / bmi088-gyro / ist8310 三个设备 ready；
 *   - 预热一次 IST8310 采样链路（硬复位由更早的 board init 阶段完成）；
 *   - 以 @p heater_duty_percent 启动恒温加热 PWM（0 表示关闭，100 为满功率 0.58W）。
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

#ifdef __cplusplus
}
#endif

#endif /* APP_IMU_9AXIS_H_ */
