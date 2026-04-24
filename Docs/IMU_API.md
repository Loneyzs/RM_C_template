# IMU 适配状态与接口说明（BMI088 + IST8310）

本文说明 RoboMaster C 型板在当前 Zephyr 工程中的 IMU 适配状态、最小测试结论，以及九轴接口层与加热控制的当前边界。

## 1. 当前结论

截至当前版本，已经可以确认以下事实：

* `BMI088 accel` / `BMI088 gyro`：基于 Zephyr 上游 `bosch,bmi08x-*` 驱动的独立最小测试已通过。
* `IST8310`：基于 Zephyr 上游 `isentek,ist8310` 驱动的独立最小测试已通过。
* 板级硬件连接、devicetree 配置、SPI1 / I2C3 / UART2(COM11) 观测链路均已验证可用。
* 当前剩余问题不在底层硬件配置，也不在上游传感器驱动本身，而在九轴聚合/融合的软件层，以及加热 PID 参数整定。

这意味着后续排障重点应放在：

1. `src/imu/imu_9axis.c` 的初始化与采样聚合逻辑。
2. 九轴姿态/融合输出链，而不是底层 BMI088 / IST8310 驱动。
3. heater 当前以“全开直驱”模式验证，PID 逻辑保留但视为待调状态。

## 2. 当前工程入口

当前 [src/main.c](</E:/zephyr_repo/RM_C_template/src/main.c:1>) 默认只启动温度最小测试：

* `test_imu_temp_start()`

这一步是为了优先确认两件事：

1. `BMI088` 温度读取链路正常；
2. heater 全开时是否存在可观测温升。

串口输出走 `USART1`（板上丝印 `UART2`），连接电脑 `COM11`，使用 `plink` 等命令行串口工具即可直接观察 ASCII 输出。

## 3. 传感器节点与配置

### 3.1 Devicetree

| 节点 | Compatible | 总线 | 当前状态 |
|---|---|---|---|
| `&bmi088_accel` | `bosch,bmi08x-accel` | `SPI1` | `okay` |
| `&bmi088_gyro`  | `bosch,bmi08x-gyro`  | `SPI1` | `okay` |
| `&ist8310`      | `isentek,ist8310`    | `I2C3` | `okay` |

板级默认定义在 [boards/st/RM_C/rm_c.dts](</E:/zephyr_repo/RM_C_template/boards/st/RM_C/rm_c.dts:1>)，项目启用与覆盖在 [app.overlay](</E:/zephyr_repo/RM_C_template/app.overlay:1>)。

`zephyr,user` 额外暴露了应用层需要直接控制的资源：

```dts
zephyr,user {
    ist8310-reset-gpios = <&gpiog 6 GPIO_ACTIVE_LOW>;
    ist8310-drdy-gpios  = <&gpiog 3 GPIO_ACTIVE_HIGH>;
    pwms = <&pwm_heater 1 5000000 PWM_POLARITY_NORMAL>,
           <&pwm1 1 1000000000 PWM_POLARITY_NORMAL>;
    pwm-names = "heater", "blink";
};
```

注意：

* `TIM10_CH1` 的 PWM 通道号必须为 `1`，不能写成 `0`。
* 当前 heater PWM 周期已对齐到 `5 ms`（`200 Hz`），以匹配 FreeRTOS 参考工程。

### 3.2 Kconfig

当前与 IMU 相关的有效配置以 [prj.conf](</E:/zephyr_repo/RM_C_Template/prj.conf:1>) 为准：

```kconfig
CONFIG_SENSOR=y
CONFIG_SENSOR_ASYNC_API=y
CONFIG_BMI08X=y
CONFIG_BMI08X_ACCEL_TRIGGER_NONE=y
CONFIG_BMI08X_GYRO_TRIGGER_NONE=y
CONFIG_IST8310=y
CONFIG_SPI=y
CONFIG_I2C=y
CONFIG_PWM=y
CONFIG_GPIO=y
CONFIG_CBPRINTF_FP_SUPPORT=y
CONFIG_FPU=y
```

注意：当前工程**不是** `GLOBAL_THREAD trigger` 模式，而是 `TRIGGER_NONE`。  
也就是说，`BMI088` 最小测试依赖的是上游驱动的同步 `sensor_sample_fetch()` / `sensor_channel_get()` 路径。

## 4. 最小测试说明

独立最小测试代码位于 [src/minimal_tests](</E:/zephyr_repo/RM_C_template/src/minimal_tests>)。

### 4.1 当前默认测试：温度最小测试

文件：[imu_temp_test.c](</E:/zephyr_repo/RM_C_template/src/minimal_tests/imu_temp_test.c:1>)

测试内容：

* 初始化整合后的 `imu_9axis`
* 关闭 heater PID
* 直接将 heater 设为全开
* 周期输出 `temp_c`、heater 当前输出、以及姿态三轴

示例输出：

```text
IMU_TEMP seq=22 temp=27.50 heater=5000.0 roll=-0.006 pitch=0.007 yaw=-2.059
```

结论解释：

* `temp` 非空且稳定，说明温度读取链路正常
* `heater` 当前应表现为满输出
* PID 逻辑保留在驱动中，但当前不作为验证基准，状态为“待调”

### 4.2 BMI088 最小测试

文件：[bmi088_minimal_test.c](</E:/zephyr_repo/RM_C_template/src/minimal_tests/bmi088_minimal_test.c:1>)

测试内容：

* 分别读取 `SENSOR_CHAN_ACCEL_XYZ`
* 分别读取 `SENSOR_CHAN_GYRO_XYZ`
* 附带读取 `SENSOR_CHAN_DIE_TEMP`
* 通过 UART2 输出 ASCII 诊断行

典型结果特征：

* 加速度模长约 `9.8 m/s^2`
* 静止时陀螺输出接近 `0 rad/s`
* 温度输出稳定变化

### 4.3 IST8310 最小测试

文件：[ist8310_minimal_test.c](</E:/zephyr_repo/RM_C_template/src/minimal_tests/ist8310_minimal_test.c:1>)

测试内容：

* 独立读取 `SENSOR_CHAN_MAGN_XYZ`
* 对上游单次采样模式做有限重试
* 通过 UART2 输出 ASCII 诊断行

典型结果特征：

* 三轴磁场值稳定
* 模长随朝向变化，但不会恒定为零

## 5. 九轴接口层的当前边界

九轴接口定义当前位于 [include/imu_9axis.h](</E:/zephyr_repo/RM_C_template/include/imu_9axis.h:1>) 和 [src/imu/imu_9axis.c](</E:/zephyr_repo/RM_C_template/src/imu/imu_9axis.c:1>)，它的职责仍然是：

* 聚合 accel / gyro / mag / die-temp 到一个采样帧
* 管理加热 PWM
* 对 IST8310 首次单次采样做有限重试

但需要特别注意：

* 当前它还不是“已验证正确”的最终九轴融合实现。
* 既然最小测试已经证明底层链路可用，后续若 `imu_9axis_*` 路径仍异常，应优先检查聚合层本身，而不是再怀疑底层硬件或 Zephyr 上游驱动。

## 6. 复位策略

当前工程中，IST8310 的硬复位在更早的板级初始化阶段完成，代码位于 [src/imu/imu_board_init.c](</E:/zephyr_repo/RM_C_template/src/imu/imu_board_init.c:1>)。

而 [src/imu/imu_9axis.c](</E:/zephyr_repo/RM_C_template/src/imu/imu_9axis.c:1>) 当前明确假设：

* 不在应用层初始化里再次执行 IST8310 硬复位。
* 仅依赖上游驱动在 `POST_KERNEL` 完成 probe 和软配置。

后续如果需要重新引入“运行期硬复位”，必须同时补齐“硬复位后的寄存器重配置”，否则会把已配置好的 IST8310 拉回未初始化状态。

## 7. 下一步建议

既然底层最小测试已经通过，下一阶段的合理路径应是：

1. 继续观测 heater 全开状态下的温度趋势。
2. heater PID 参数暂保留 FreeRTOS 参考值，但明确视为待调状态。
3. 在确认温升趋势后，再决定是否恢复 PID 闭环测试。
