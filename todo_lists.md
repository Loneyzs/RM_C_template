### 项目说明

RoboMaster C 型开发板 Zephyr 硬件适配工作。

### 已完成

* ✅ 板级设备树：SEGGER RTT 作为 console/shell；USART1（丝印 UART2）释放给应用层
* ✅ BMI088 + IST8310 已切换到 Zephyr 上游驱动路径
* ✅ `app.overlay` 已打开 SPI1 / I2C3 / IMU 节点及加热 PWM
* ✅ 九轴接口层基础代码已接入：`include/imu_9axis.h` + `src/imu/imu_9axis.c`
* ✅ 最小测试模块已整理到 `src/minimal_tests/`
* ✅ BMI088 独立最小测试已通过
* ✅ IST8310 独立最小测试已通过
* ✅ UART2(COM11) 命令行串口观测链路已打通
* ✅ heater PWM 通道号错误已修复（`TIM10_CH1` 使用通道 `1`）
* ✅ 当前已切换到“heater 全开 + 温度最小测试”模式

### 当前判断

* 可以确认硬件连接、板级配置、上游 Zephyr BMI088 / IST8310 驱动均无根本问题。
* 当前剩余问题只在九轴聚合/融合的软件层输出精度，以及 heater PID 参数整定。

### 下一阶段

* IMU加热部分先保持全开，暂时不变。
* 完善九轴IMU软件层。

### 通用规则

* 严禁改动 Zephyr 工作空间源码下的任何内容
* 涉及硬件层面但文档未明确说明的问题，先确认再继续
