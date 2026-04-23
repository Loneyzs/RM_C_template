### 项目说明

RoboMaster C型开发板 Zephyr 硬件适配工作

### 已完成

* ✅ 板级设备树：SEGGER RTT 作为 console/shell；USART1 (丝印 UART2) 释放
  - `boards/st/RM_C/rm_c.dts` 的 chosen 节点移除 zephyr,console/shell-uart
  - `boards/st/RM_C/rm_c_defconfig` / `prj.conf` 启用 `CONFIG_USE_SEGGER_RTT` + `CONFIG_RTT_CONSOLE`，关闭 `CONFIG_UART_CONSOLE`
* ✅ BMI088 + IST8310 九轴 IMU 接入（路径 A：复用 Zephyr 上游驱动）
  - 板级 dts 切换 compatible 到 `bosch,bmi08x-accel` / `bosch,bmi08x-gyro` / `isentek,ist8310`，节点默认 disabled
  - 删除冗余自建 bindings (`bosch,bmi088-*.yaml`、`robomaster,ist8310.yaml`)
  - 新建 `app.overlay`：打开 SPI1/I2C3/IMU 节点、TIM10 加热 PWM，通过 `zephyr,user` 暴露 IST8310 RSTN/DRDY GPIO + 加热 PWM
  - 九轴接口层：`include/imu_9axis.h` + `src/imu_9axis.c`（初始化、采样、占空比调节）
  - IMU 恒温加热暂用恒定占空比（50% 默认，0.29 W），待后续 PID
  - 接口使用文档：`Docs/IMU_API.md`

### 待办（下一阶段）

* 板到手后联调：逐个验证 accel/gyro/mag 读数、RTT 日志输出、加热 PWM 波形
* 温控 PID 闭环（替换恒定占空比）
* 在线标定（重力矢量归一化 + 陀螺零偏），可在 `imu_9axis_sample()` 之上构建
* 如需 DRDY 中断直触发处理，改用 `sensor_trigger_set()` 或 async/stream API

### 通用规则

* 严禁改动 Zephyr 工作空间源码下的任何内容！
* 硬件开发的特殊性：涉及任何硬件层面没有具体文档资料说明的问题，请向我询问后再继续。