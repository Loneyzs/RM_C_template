### 项目说明

RoboMaster C 型开发板 Zephyr 最小模板工程。

### 当前状态

* 板级设备树已覆盖主要板载外设：RTT console、USART1（丝印 UART2）、SPI1、I2C3、PWM、CAN1。
* BMI088 accel/gyro 与 IST8310 已走 Zephyr 上游 sensor 驱动。
* IMU 九轴聚合层已接入：`include/imu_9axis.h` + `src/imu/imu_9axis.c`。
* IMU 姿态融合已从互补滤波升级为 Mahony。
* 每次上电执行 gyro 零偏标定。
* accel 平放校准已完成一次实测，并固化为固定偏置参数。
* 默认 main 入口运行 `test_imu_justfloat_start()`，通过 USART1/COM11 输出 JustFloat 三通道姿态。
* 最小测试已统一整理到 `src/minimal_tests/`，命名为 `<name>_test.c` + `test_<name>_start()`。
* 当前各项外设/IMU 最小功能测试已通过，可作为后续应用开发模板。

### 保留测试入口

* `test_uart_start()`
* `test_pwm_led_start()`
* `test_can_loopback_start()`
* `test_bmi088_start()`
* `test_ist8310_start()`
* `test_imu_temp_start()`
* `test_imu_justfloat_start()`

### 后续优化方向

* 若进入控制闭环，建议把 IMU 采样周期从当前 JustFloat 观察用的 `20 ms` 提高到 `1~2 ms`。
* 若 yaw 在不同场地明显跳动，优先做 IST8310 硬铁/软铁磁校准，或降低 `IMU_MAHONY_MAG_WEIGHT`。
* 若温升后静止姿态仍漂移，优先建立 gyro 温度零偏补偿，而不是继续调大 Mahony 增益。
* 六面 accel 标定暂未做；当前仅适合本板当前安装方向的平放偏置修正。

### 通用规则

* 不改动 Zephyr 工作空间源码，只修改本工程。
* 涉及硬件连接或板级定义但文档未明确说明的问题，先确认再继续。
