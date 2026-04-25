# RM_C_Template

RoboMaster C 型开发板 Zephyr 最小模板工程。当前模板已完成板级设备树、BMI088 + IST8310 IMU、UART2/COM11、PWM、CAN1 等基础验证。

## 目录

- `boards/st/RM_C/`：RoboMaster C 型板的 Zephyr board 定义。
- `src/drivers/imu/`：IMU 驱动聚合、Mahony 姿态融合和 IMU 参数文档。
- `include/drivers/imu/`：IMU 对外接口。
- `src/board_package/`：板级最小测试与最小测试文档。
- `include/board_package/`：板级测试入口头文件。
- `Docs/`：原理图、手册、引脚说明和项目状态记录。

## 快速上手

构建：

```powershell
west build -b rm_c .
```

烧录：

```powershell
west flash
```

如果烧录后目标保持 halt，可手动运行：

```powershell
E:/zephyr-sdk-1.0.1/hosttools/openocd/bin/openocd.exe -s E:/zephyr-sdk-1.0.1/hosttools/openocd/share/openocd/scripts -f boards/st/RM_C/support/openocd.cfg -c "init" -c "reset run" -c "shutdown"
```

查看 RTT 日志：

```powershell
west rtt --no-halt
```

查看 UART2（芯片外设为 `USART1`，当前 PC 侧 COM11）：

```powershell
plink -serial COM11 -sercfg 115200,8,n,1,N
```

## 运行最小测试

`main` 当前保持最小模板形态。需要运行某个板级测试时，在 [src/main.c](</E:/zephyr_repo/RM_C_Template/src/main.c:1>) 中包含测试入口并调用对应函数：

```c
#include <zephyr/kernel.h>

#include "board_package/unit_tests/unit_tests.h"

int main(void)
{
    (void)test_imu_justfloat_start();

    while (1) {
        k_msleep(1000);
    }

    return 0;
}
```

可用入口见 [src/board_package/unit_tests/UNIT_TESTS.md](</E:/zephyr_repo/RM_C_Template/src/board_package/unit_tests/UNIT_TESTS.md:1>)。

## IMU 参数

IMU 参数、固定 accel 平放偏置、gyro 上电标定和 Mahony 调参说明见 [src/drivers/imu/IMU_API.md](</E:/zephyr_repo/RM_C_Template/src/drivers/imu/IMU_API.md:1>)。

