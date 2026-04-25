# IMU 参数与调试说明

本文只关注当前工程中 `BMI088 + IST8310` 九轴接口的参数、标定和调试边界。底层 SPI/I2C/devicetree 已通过独立最小测试验证，不再作为主要排障对象。

## 当前实现

入口文件：

- 接口定义：[include/drivers/imu/imu_9axis.h](</E:/zephyr_repo/RM_C_Template/include/drivers/imu/imu_9axis.h:1>)
- 实现文件：[src/drivers/imu/imu_9axis.c](</E:/zephyr_repo/RM_C_Template/src/drivers/imu/imu_9axis.c:1>)
- 测试入口：[src/board_package/unit_tests](</E:/zephyr_repo/RM_C_Template/src/board_package/unit_tests>) 保留 `test_imu_temp_start()` 与 `test_imu_justfloat_start()`。

当前融合链路：

- BMI088 accel/gyro 三轴坐标直接沿用参考 FreeRTOS 框架，不交换、不取反。
- 每次上电做 gyro 零偏标定，要求启动时保持静止。
- accel 平放校准已改为固定参数，不再每次上电自动估计。
- 姿态融合使用 Mahony 四元数更新，输出 `roll/pitch/yaw`，单位为 `rad`。
- IST8310 磁力计参与 yaw 修正；当前权重已经按实测优化，但未做硬铁/软铁校准前，不应把 yaw 绝对精度视为可靠。

## 参数区

主要参数集中在 [src/drivers/imu/imu_9axis.c](</E:/zephyr_repo/RM_C_Template/src/drivers/imu/imu_9axis.c:41>)。

### 上电 gyro 零偏

```c
#define IMU_CALIB_SAMPLES                 400U
#define IMU_CALIB_INTERVAL_MS             2U
#define IMU_CALIB_MAX_ATTEMPTS            3U
#define IMU_CALIB_MAX_GYRO_RANGE_RAD_S    0.15f
#define IMU_CALIB_MAX_ACCEL_NORM_RANGE    0.50f
```

含义：

- `IMU_CALIB_SAMPLES * IMU_CALIB_INTERVAL_MS` 决定单次标定耗时，当前约 `800 ms`。
- `IMU_CALIB_MAX_GYRO_RANGE_RAD_S` 越小，对静止要求越严格。
- `IMU_CALIB_MAX_ACCEL_NORM_RANGE` 用于判断标定期间是否有明显晃动。

调参建议：

- 如果上电经常提示不稳定，先确认板子确实静止，再把 gyro range 放宽到 `0.20f`。
- 如果静止 yaw/roll/pitch 仍缓慢漂，优先增加样本数到 `800U`，不要先调 Mahony。

### 固定 accel 平放校准

本次平放 400 点实测均值：

```text
raw avg = (0.029985, -0.080403, 9.759632) m/s^2
```

写入固定偏置：

```c
#define IMU_ACCEL_FLAT_OFFSET_X_MPS2  0.02998547f
#define IMU_ACCEL_FLAT_OFFSET_Y_MPS2 -0.08040254f
#define IMU_ACCEL_FLAT_OFFSET_Z_MPS2 -0.04701789f
```

修正逻辑是 `corrected = raw - offset`，因此平放时目标接近 `(0, 0, 9.80665)`。

注意：

- 这不是六面加速度标定，只修正“当前安装方向平放”的零点。
- 如果换板、拆装 IMU 或重新焊接，应重新测一次。
- 如果只是融合参数调试，不要反复改这三个值。

### Mahony 融合

```c
#define IMU_ACCEL_LPF_TAU_S        0.0085f
#define IMU_ACCEL_CORRECT_MIN_NORM 6.0f
#define IMU_ACCEL_CORRECT_MAX_NORM 13.0f
#define IMU_MAHONY_TWO_KP          2.0f
#define IMU_MAHONY_TWO_KI          0.02f
#define IMU_MAHONY_MAG_WEIGHT      0.6f
```

含义：

- `IMU_MAHONY_TWO_KP`：姿态误差比例修正，越大收敛越快，也越容易抖。
- `IMU_MAHONY_TWO_KI`：慢速积分修正，主要压低低频漂移，过大会拖出低频振荡。
- `IMU_MAHONY_MAG_WEIGHT`：磁力计 yaw 修正权重；当前值来自本板实测优化，若换环境或换板后 yaw 抖动，应优先回调该值。
- `IMU_ACCEL_CORRECT_MIN/MAX_NORM`：加速度模长门限，运动剧烈时跳过 accel 修正，避免把线加速度当重力。

建议调试顺序：

1. 静止平放，看 roll/pitch 是否接近 0 且不明显漂移。
2. 小角度快速倾斜，看回正是否慢；慢则略增 `IMU_MAHONY_TWO_KP`。
3. 静止数分钟看低频漂移；漂移明显再小幅增加 `IMU_MAHONY_TWO_KI`。
4. yaw 跳动或受电机/金属影响时，优先降低 `IMU_MAHONY_MAG_WEIGHT`，必要时临时置 `0.0f`。

## 加热参数

heater PID 仍保留参考框架参数：

```c
#define IMU_HEATER_TARGET_TEMP_C      40.0f
#define IMU_HEATER_PID_KP             1000.0f
#define IMU_HEATER_PID_KI             20.0f
#define IMU_HEATER_PID_KD             0.0f
#define IMU_HEATER_PID_MAX_OUT        2000.0f
#define IMU_HEATER_PWM_FULL_SCALE     5000.0f
```

当前建议：

- 先用 `imu_temp_test` 验证温升曲线，再恢复 PID 闭环。
- 如果温度稳定前 gyro 漂移明显，控制侧应等温度接近稳定后再进入高精度模式。

## 已知边界

- 当前不是完整工业级九轴 AHRS；磁力计未做椭球校准，yaw 只能作为弱修正。
- accel 只做一次平放固定偏置，没有做六面标定。
- 默认 `imu_justfloat` 周期为 `20 ms`，用于观察足够；进入控制闭环前建议提高到 `1~2 ms` 级任务周期。
