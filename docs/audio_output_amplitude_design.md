# ESP32 I2S 到 PCM5102A 输出幅值设计说明

> 文档状态：正式设计说明
>
> 适用范围：ESP32 固件的 `WaveModule`、PCM5102A 模拟输出和后级 TPA3255 输入之间的幅值关系。
>
> 这份文档记录的是源码已经实现的规则、计算公式和测量边界。它不把尚未做过的硬件实测写成已验证结论。

## 1. 先看结论

| 项目 | 正式规则 |
| --- | --- |
| APP 用户可调强度 | 1 ~ 120 |
| ESP32 协议强度范围 | 0 ~ 120；`0` 表示静音，属于合法设备值 |
| 禁止使用的旧范围 | `0 ~ 255` 不属于本产品强度合同，不能用于换算或发送 |
| 固件正弦查表峰值 | `8826`，对应 16 位有符号样本的受控幅值 |
| 强度 120 的稳态数字峰值 | `+8826 / -8826` |
| PCM5102A 标称满幅 | 2.1 V RMS，单端、以模拟地为中心 |
| 强度 120 的理论 PCM5102A 输出 | 约 `0.800 Vpk`、`0.566 Vrms`、`1.600 Vpp` |
| 0.8V 的单位 | 设计推导值是峰值 `Vpk`，不是 `Vrms` |

这里的 0.8V 是根据固件数字幅值和 PCM5102A 芯片标称满幅推导出的**理论名义值**。它不是对每一块 DAC 模块、每种负载和每个测量点的绝对硬件保证。实际硬件还会受到芯片增益误差、模块电源、负载、输出滤波和测量方式影响。

## 2. 完整输出链路

```text
APP
  │ BLE WAVE:SET(freq, intensity)
  ▼
ESP32 ProtocolCodec
  ▼
WaveModule
  ├─ 强度范围校验：0 ~ 120
  ├─ 正弦查表：sin(相位) × 8826
  ├─ 强度包络：当前强度 ÷ 120
  └─ I2S DMA
       ▼
PCM5102A OUTL / OUTR 模拟输出
       ▼
TPA3255 功放
       ▼
振动平台
```

项目文档中的正式数据链也是 `WaveModule -> DDS/LUT -> I2S DMA -> PCM5102A -> TPA3255 -> Platform`：[architecture_runtime.md](architecture_runtime.md#42-data-plane)。硬件拓扑说明见 [hardware.md](hardware.md#4-音频链路)。

### 2.1 本文的测量点

本文的电压公式只适用于：

```text
PCM5102A 的 OUTL 或 OUTR 到模拟地 AGND
```

如果测量点在 TPA3255 后面，已经经过功率放大，不能使用本文的电压结果。

APP 中的 `AUX` 是本机音频 Local LUT 路径，不经过 ESP32 的 PCM5102A。本文说的输出是 PCM5102A 板上的模拟线路输出。

## 3. 参数真相源

| 参数 | 当前源码位置 | 当前值和作用 |
| --- | --- | --- |
| I2S 采样率 | `src/config/GlobalConfig.h` | `16000 Hz` |
| I2S 样本位宽 | `src/config/GlobalConfig.h` | `16 bit` |
| I2S 声道 | `src/modules/wave/WaveModule.cpp` | 左右声道发送相同样本 |
| 固件强度上限 | `src/config/GlobalConfig.h` | `WAVE_INTENSITY_MAX = 120` |
| 正弦查表峰值 | `src/modules/wave/WaveModule.h` | `MAX_AMPLITUDE = 8826` |
| 启动渐变时间 | `src/config/GlobalConfig.h` | `800 ms` |
| 停止渐变时间 | `src/config/GlobalConfig.h` | `500 ms` |
| 协议校验范围 | `src/core/ProtocolCodec.h` | `amp` 必须是 `0 ~ 120` |

APP 当前页面的用户输入约束是 1 ~ 120；Android 页面约束在 `apps/android/src/main/java/com/example/sonicwavev4/feature/sine/ui/manual/HomeParameterConstraints.kt`，Android 设备写入链在 `apps/android/src/main/java/com/example/sonicwavev4/feature/home/domain/HomeHardwareRepository.kt`。固件协议保留 0 作为静音值。因此不能把“APP 页面范围”和“设备协议范围”混写成同一个范围。

## 4. 原始版本的输出公式

初始固件提交 `8ee5863` 的输出逻辑是：

```text
base[n] = trunc(sin(2πn / 256) × 8826)

intensity_smoothed：
  启动时每个缓冲区最多增加 2
  降低时每个缓冲区最多减少 2
  最终收敛到目标强度

sample16[n] = trunc(base[n] × intensity_smoothed / 120)
sample32[n] = sample16[n] << 16
```

原始版本使用 32 位 I2S 数据，把 16 位样本放到 32 位数据的高 16 位。强度 120 收敛后：

```text
sample16[n] = base[n]
峰值 = ±8826
```

## 5. 当前版本的输出公式

当前版本把渐变改成时间控制，但稳态最大幅值保持不变：

```text
target_amplitude = clamp(intensity, 0, 120) / 120

current_amplitude：
  按 10 ms 更新一次
  启动在 800 ms 内接近 target_amplitude
  停止在 500 ms 内回到 0

envelope_q15 = round(current_amplitude × 32767)

sample16[n] = trunc(base[n] × envelope_q15 / 32767)
```

当强度为 120 并且启动渐变完成后：

```text
target_amplitude = 120 / 120 = 1
envelope_q15 = 32767
sample16[n] = base[n]
峰值 = ±8826
```

因此，原始版本和当前版本在强度 120 的稳态幅值上是一致的。当前版本与原始版本确实存在两个实现差异，需要修改时不能忽略：

1. 原始版本是 32 位 I2S、样本左移 16 位；当前版本是 16 位 I2S、样本直接发送。
2. 原始版本按缓冲区以整数步长渐变；当前版本按时间以浮点包络渐变。

这两个差异改变了 I2S 封装和过渡过程，但没有改变强度 120 时的目标样本峰值 `8826`。PCM5102A 数据手册明确列出其接受 16、24 和 32 位音频数据，因此当前 16 位输入格式属于芯片支持范围。[PCM5102A 数据手册](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf)

## 6. 从数字样本推导模拟电压

### 6.1 数字幅值比例

```text
digital_ratio = 8826 / 32767
              = 0.269356
```

### 6.2 PCM5102A 标称满幅

TI 数据手册给出的 PCM5102A 满幅单端输出是：

```text
V_FS_RMS = 2.1 Vrms
```

这是以模拟地为中心的线路输出。对应的满幅峰值为：

```text
V_FS_PK = 2.1 × √2
        ≈ 2.9698 Vpk
```

### 6.3 任意强度的名义输出公式

稳态、正弦波、未经过 TPA3255 放大的情况下：

```text
V_RMS(I) = 2.1 × (8826 / 32767) × (I / 120)
         ≈ 0.56565 × (I / 120) Vrms

V_PK(I)  = V_RMS(I) × √2
         ≈ 0.79995 × (I / 120) Vpk

V_PP(I)  = 2 × V_PK(I)
         ≈ 1.59990 × (I / 120) Vpp
```

### 6.4 常用强度结果

| 强度 `I` | 数字峰值 | 名义 RMS | 名义峰值 | 名义峰峰值 |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 0 V | 0 V | 0 V |
| 60 | ±4413 | 0.283 V | 0.400 V | 0.800 V |
| 120 | ±8826 | 0.566 V | 0.800 V | 1.600 V |

`8826` 与 0.8V 峰值目标的反推关系是：

```text
0.8 / (2.1 × √2) × 32767 ≈ 8826.6
```

所以可以确认：`MAX_AMPLITUDE = 8826` 与“强度 120 时约 0.8V 峰值”的设计目标在数值上吻合。但仓库历史中没有找到一份更早的正式文档直接写出这条推导，本文件将它补充为正式可查阅说明。

## 7. “不超过 0.8V”的边界

这里必须区分“软件名义设计值”和“硬件绝对上限”：

- 软件公式把强度 120 的理想名义峰值设为约 0.8V。
- TI 数据手册列出的 DAC 增益误差最高约为 ±6%，因此实际芯片的名义输出可能上下偏离。
- PCM5102A 模块的电源、负载、输出滤波和板级增益也会影响测量结果。
- 当前固件项目文档仍把 PCM5102A 实际输出路径列为待验证项：[current_hardware_validation_boundary.md](system/current_hardware_validation_boundary.md#not-yet-validated-in-current-setup)。

因此，当前文档可以确认“设计计算约为 0.8Vpk”，不能把它表述成“所有硬件实测都绝对不超过 0.8Vpk”。如果产品要求硬件在最坏情况下也必须不超过 0.8Vpk，需要预留增益误差余量，或对实际模块逐台测量后校准 `MAX_AMPLITUDE`。

## 8. 修改输出代码前必须重新核对

任何人修改以下内容，都必须重新计算第 6 节公式，并重新做 PCM5102A 实测：

1. `MAX_AMPLITUDE`。
2. 强度归一化分母 `120`。
3. I2S 样本位宽、数据对齐方式或声道格式。
4. PCM5102A 的供电、输出负载或后级模拟电路。
5. TPA3255 前的衰减、增益或线路连接。

推荐的硬件验证步骤：

1. 在 ESP32 运行状态稳定后发送 `WAVE:SET f=40,i=120`。
2. 发送 `WAVE:START`，等待启动渐变完成至少 800 ms。
3. 在 PCM5102A 的 `OUTL` 或 `OUTR` 对 `AGND` 测量，并同时记录 `Vpk`、`Vrms`、`Vpp`。
4. 发送 `WAVE:STOP`，确认输出回到静音。
5. 不要把 TPA3255 后的功率输出结果当成 PCM5102A 的线路输出结果。

## 9. 代码与资料索引

- 强度上限、I2S 位宽、采样率和引脚：[`src/config/GlobalConfig.h`](../src/config/GlobalConfig.h)
- 正弦查表和最大幅值：[`src/modules/wave/WaveModule.h`](../src/modules/wave/WaveModule.h)、[`src/modules/wave/WaveModule.cpp`](../src/modules/wave/WaveModule.cpp)
- 协议强度范围：[`src/core/ProtocolCodec.h`](../src/core/ProtocolCodec.h)
- 固件硬件拓扑：[`docs/hardware.md`](hardware.md)
- I2S 验证方法：[`docs/firmware_developer_guide.md`](firmware_developer_guide.md)
- PCM5102A 官方数据手册：[Texas Instruments PCM5102A datasheet](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf)
