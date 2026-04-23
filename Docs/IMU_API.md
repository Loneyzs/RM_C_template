# 九轴 IMU 接口使用指南（BMI088 + IST8310）

本文面向应用层开发者，说明如何在 RoboMaster C 型板的 Zephyr 工程里读取九轴 IMU 数据。

## 1. 总体架构

```
 ┌────────────────────────────────────────────────────────────────┐
 │  application  (src/main.c …)                                   │
 │          ▲                                                     │
 │          │ imu_9axis_sample_t                                  │
 │          │                                                     │
 │  ┌──────────────────────────────────────────────────┐          │
 │  │  imu_9axis  (src/imu_9axis.c)                    │          │
 │  │   · IST8310 RSTN 硬复位序列                      │          │
 │  │   · 加热 PWM 恒定占空比（后续可替换为 PID）     │          │
 │  │   · 汇总 3 个 sensor API 设备到一个采样帧        │          │
 │  └──────────────────────────────────────────────────┘          │
 │          ▲                  ▲                   ▲              │
 │          │ sensor API       │ sensor API        │ sensor API   │
 │  ┌──────────────┐   ┌──────────────┐    ┌────────────────┐     │
 │  │bosch,bmi08x- │   │bosch,bmi08x- │    │isentek,        │     │
 │  │accel (upstream)│ │gyro  (upstream)│  │ist8310(upstream)│    │
 │  └──────────────┘   └──────────────┘    └────────────────┘     │
 │       SPI1 / INT1        SPI1 / INT3           I2C3            │
 └────────────────────────────────────────────────────────────────┘
```

* BMI088 accel / gyro：复用 Zephyr 上游驱动 `drivers/sensor/bosch/bmi08x/`，支持 trigger（DRDY 中断）、async / stream。
* IST8310：复用 Zephyr 上游驱动 `drivers/sensor/ist8310/`，I2C 单次采样模式（sample_fetch 内部轮询 DRDY 位）。
* 板级 `rm_c.dts` 中三个节点默认 `status = "disabled"`；打开和参数调整统一在项目级 overlay（本仓库为 `app.overlay`）完成。

## 2. Devicetree 属性清单

| 节点 | Compatible | 必需属性 | 板级默认 |
|---|---|---|---|
| `&bmi088_accel` | `bosch,bmi08x-accel` | `accel-hz`、`accel-fs`、`int1-map-io`、`int1-conf-io`、`int2-map-io`、`int2-conf-io` | `800 Hz` / `±6 g` / INT1=DRDY, 推挽高有效 |
| `&bmi088_gyro`  | `bosch,bmi08x-gyro`  | `gyro-hz`、`gyro-fs`、`int3-4-map-io`、`int3-4-conf-io` | `1000_116` / `±2000 °/s` / INT3=DRDY, 推挽高有效 |
| `&ist8310`      | `isentek,ist8310`    | 无（仅 `reg`） | `0x0E` |

应用层需要、但不在 binding 内的信号通过 overlay 的 `zephyr,user` 节点暴露：

```dts
zephyr,user {
    ist8310-reset-gpios = <&gpiog 6 GPIO_ACTIVE_LOW>;   /* 必需：RSTN */
    ist8310-drdy-gpios  = <&gpiog 3 GPIO_ACTIVE_HIGH>;  /* 可选：DRDY，应用按需轮询 */
    pwms = <&pwm_heater 0 1000000 PWM_POLARITY_NORMAL>; /* 必需：加热 PWM，period=1 ms */
    pwm-names = "heater";
};
```

## 3. Kconfig 必需项

`prj.conf` 中与 IMU 相关的开关：

```kconfig
CONFIG_SENSOR=y
CONFIG_SENSOR_ASYNC_API=y   # bmi08x 驱动要求（结构体含 rtio_bus）
CONFIG_BMI08X=y
CONFIG_BMI08X_ACCEL_TRIGGER_GLOBAL_THREAD=y
CONFIG_BMI08X_GYRO_TRIGGER_GLOBAL_THREAD=y
CONFIG_IST8310=y
CONFIG_SPI=y
CONFIG_I2C=y
CONFIG_PWM=y
CONFIG_GPIO=y
# 浮点打印（sensor_value_to_double → %f）
CONFIG_CBPRINTF_FP_SUPPORT=y
CONFIG_FPU=y
```

## 4. 应用层 API

头文件：`include/imu_9axis.h`。

```c
typedef struct {
    float    accel[3];     /* m/s^2           */
    float    gyro[3];      /* rad/s           */
    float    mag[3];       /* Gauss           */
    float    temp_c;       /* ℃（BMI088 accel die-temp） */
    uint64_t timestamp_ns; /* 单调时间戳     */
} imu_9axis_sample_t;

int imu_9axis_init(uint8_t heater_duty_percent);     /* 0~100 */
int imu_9axis_sample(imu_9axis_sample_t *out);
int imu_9axis_set_heater_duty(uint8_t duty_percent); /* 0~100 */
```

### 4.1 初始化

```c
#include "imu_9axis.h"

if (imu_9axis_init(50) < 0) {        /* 50% 恒温占空比 ≈ 0.29 W */
    LOG_ERR("IMU init failed");
    return;
}
```

`imu_9axis_init()` 依次完成：

1. 校验 `bmi088-accel`、`bmi088-gyro` 设备 ready；
2. 对 `PG6 / RSTN` 拉低 50 ms → 拉高 50 ms，完成 IST8310 硬复位；
3. 校验 `ist8310` 设备 ready（上游驱动内部已在 POST_KERNEL 做过软复位 + 配置）；
4. 以给定占空比启动加热 PWM。

### 4.2 周期采样

```c
imu_9axis_sample_t s;

while (1) {
    if (imu_9axis_sample(&s) == 0) {
        /* 处理 s.accel / s.gyro / s.mag / s.temp_c / s.timestamp_ns */
    }
    k_msleep(1);    /* BMI088 设为 800 Hz，1 ms 不会错过数据；按需调整 */
}
```

> **触发模式说明**：`CONFIG_BMI08X_*_TRIGGER_GLOBAL_THREAD=y` 会让上游驱动在 DRDY 中断发生时把数据搬到内部缓冲区，随后 `sensor_sample_fetch()` 只是把缓冲区值交给 `sensor_channel_get()`。若进一步希望直接由 DRDY 中断驱动上层处理，可改用 `sensor_trigger_set()` 注册回调；本接口层目前未封装。

### 4.3 加热占空比调节

```c
imu_9axis_set_heater_duty(80);   /* 临时拉高到 80% */
```

恒定占空比只是最小可用方案，不保证陀螺仪温漂收敛。后续引入温控 PID 时，仅需在外部循环中读取 `s.temp_c`、运行 PID、调用本接口即可。

## 5. 关键硬件假设与限制

* BMI088 出厂以 I²C 模式启动，上游驱动 probe 过程中已做过一次 fake write 切 SPI，无需人工处理。
* IST8310 的 DRDY 在本实现中**未使用**：上游驱动走单次采样模式，`sample_fetch` 内轮询 STATUS_REG.DRDY。若后续切到 trigger，需要另外封装（上游未支持）。
* `accel-hz` / `gyro-hz` / `accel-fs` / `gyro-fs` 变更后务必同步确认 FreeRTOS 参考代码里 `BMI088_ACCEL_*_SEN` 的尺度与上游驱动一致——上游驱动内部按 fs 自动选取 LSB/unit，应用层无需手动乘系数。
* 加热 PWM 频率 1 kHz（`period = 1 000 000 ns`）；需要更低频率时在 overlay 中改 `pwms` 第三参数。

## 6. 未移植的 FreeRTOS 参考代码能力

如后续需要，建议在本接口层内增量补齐：

| 功能 | 状态 | 备注 |
|---|---|---|
| 在线重力标定 / 陀螺零偏 | ⏳ | 可利用 `imu_9axis_sample()` 在 6 s 静止期采样求平均 |
| 温控 PID | ⏳ | 输入 `temp_c`，输出 `imu_9axis_set_heater_duty()` |
| DRDY 中断直触发 + async API | ⏳ | 上游 `sensor_read_async_mempool` 可替代 |
| DMA 零拷贝读取 | ⏳ | 上游 SPI+RTIO 已铺好底层基础 |

## 7. 调试建议（待板上验证）

1. 串口被换成 RTT：`west debugserver` → `JLinkRTTViewer`（或 `west debug` + rtt）读取 `LOG_INF` 输出；
2. 未上板时可先 `west build -b rm_c` 确认编译产物地址布局，之后逐个打开 LOG 模块：
   `CONFIG_SENSOR_LOG_LEVEL_DBG=y`、`CONFIG_LOG_DEFAULT_LEVEL=4`；
3. 若 `imu_9axis_init()` 返回 `-ENODEV`，按顺序排查：pinctrl 是否被 overlay 打开 → SPI/I²C 总线是否 okay → 设备节点是否 okay → 电源是否就绪。
