# 最小测试说明

最小测试代码统一放在 [src/board_package/unit_tests](</E:/zephyr_repo/RM_C_Template/src/board_package/unit_tests>)。命名规则：

- 文件：`<name>_test.c`
- 入口：`test_<name>_start()`
- 串口输出：需要串口的测试均在本文件内直接使用板上 `UART2`，芯片外设为 `USART1`，不依赖其它测试文件。

## 运行方式

[src/main.c](</E:/zephyr_repo/RM_C_Template/src/main.c:1>) 默认保持最小模板形态。需要运行某个测试时，手动包含入口头文件并调用一个测试入口：

```c
#include "board_package/unit_tests/unit_tests.h"

int main(void)
{
	(void)test_imu_justfloat_start();

	return 0;
}
```

UART、IMU Temp、IMU JustFloat 的串口输出统一走板上 `UART2`，芯片外设为 `USART1`，当前 PC 侧为 `COM11`。BMI088、IST8310、PWM、CAN 测试只使用 RTT 日志输出。

```powershell
plink -serial COM11 -sercfg 115200,8,n,1,N
```

## 测试列表

| 文件 | 入口 | 目的 |
|---|---|---|
| `uart_test.c` | `test_uart_start()` | 验证 UART2/COM11 基础输出 |
| `pwm_led_test.c` | `test_pwm_led_start()` | 验证 PWM1 与 LED_G 闪烁 |
| `can_loopback_test.c` | `test_can_loopback_start()` | 验证 CAN1 loopback |
| `bmi088_test.c` | `test_bmi088_start()` | 直接读取 BMI088 accel/gyro/temp |
| `ist8310_test.c` | `test_ist8310_start()` | 直接读取 IST8310 mag |
| `imu_temperature_test.c` | `test_imu_temp_start()` | 观察 IMU 温度与 heater 输出 |
| `imu_justfloat_test.c` | `test_imu_justfloat_start()` | 输出融合后的 roll/pitch/yaw，JustFloat 格式 |

## 输出格式

### UART

```text
UART2_TEST_OK
```

### BMI088

```text
BMI088 OK seq=28 acc=(x,y,z) |a|=9.80 gyro=(x,y,z) |g|=0.01 temp=26.75
```

判断：

- 平放时 `acc` 应接近 `(0, 0, 9.8)`。
- 静止时 `|g|` 应接近 `0`。

### IST8310

```text
IST8310 OK seq=20 mag=(x,y,z) |m|=...
```

判断：

- 三轴不应长期为 0。
- 转动板子时三轴应变化。

### IMU Temp

```text
IMU_TEMP seq=22 temp=27.50 heater=5000.0 roll=-0.006 pitch=0.007 yaw=-2.059
```

判断：

- `temp` 应为合理环境温度并随加热变化。
- `heater=5000.0` 表示全量 PWM 标尺，不等同 PID 输出上限。

### IMU JustFloat

二进制帧格式：

```text
float roll
float pitch
float yaw
tail = 00 00 80 7F
```

单位：

- `roll/pitch/yaw` 均为 `rad`。
- 上位机按 3 通道 float + JustFloat 帧尾解析。

