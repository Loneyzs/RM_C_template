# 最小测试说明

本目录对应的最小测试代码位于 [src/minimal_tests](</E:/zephyr_repo/RM_C_Template/src/minimal_tests>)。

## 1. 当前默认行为

当前 [src/main.c](</E:/zephyr_repo/RM_C_Template/src/main.c:1>) 默认只启动：

* `uart_minimal_test()`
* `test_bmi088_minimal_start()`
* `test_ist8310_minimal_start()`

这样做的目的，是先把“底层设备是否工作”单独验证清楚，再处理九轴聚合/融合逻辑。

## 2. 串口观测

输出串口：

* 板上：`UART2`
* 芯片外设：`USART1`
* PC 侧：当前接到 `COM11`

示例命令：

```powershell
plink -serial COM11 -sercfg 115200,8,n,1,N
```

## 3. 各最小测试模块

### 3.1 `uart_minimal_test.c`

只验证 UART2 到电脑串口链路是否正常，输出固定文本：

* `UART_TEST_OK`

### 3.2 `bmi088_minimal_test.c`

独立验证 BMI088：

* accel 三轴
* gyro 三轴
* die temperature

示例输出：

```text
BMI088 OK seq=28 acc=(-3.536,2.434,8.734) |a|=9.732 gyro=(-0.002,0.009,0.004) |g|=0.010 temp=26.75
```

判定要点：

* `|a|` 应接近 `9.8`
* 静止时 `|g|` 应接近 `0`
* 温度应为合理环境值

### 3.3 `ist8310_minimal_test.c`

独立验证 IST8310：

* mag 三轴
* 磁场模长

示例输出：

```text
IST8310 OK seq=20 mag=(0.272,-0.163,0.814) |m|=0.873
```

判定要点：

* 三轴值非零
* 朝向变化时数值应变化
* 不应持续报 `ERR`

### 3.4 其他保留模块

以下模块已整理到 `src/minimal_tests/`，但当前 `main` 默认不启动：

* `test_can_loopback.c`
* `test_pwm_led.c`
* `test_imu_uart.c`

它们保留的目的是后续单独回归，不参与当前 IMU 底层验证闭环。
