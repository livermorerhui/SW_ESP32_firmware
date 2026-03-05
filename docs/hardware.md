# SonicWave Hardware Documentation

## 1. 硬件系统组成

SonicWave 硬件系统由以下核心器件构成：

- ESP32-S3
  - 系统主控，负责 BLE 通信、状态机控制、I2S 音频数据输出、UART 传感采样。
- PCM5102A DAC
  - 接收 ESP32 I2S 数字音频流，转换为模拟音频信号。
- TPA3255 功放
  - 对 DAC 输出信号进行功率放大，驱动振动执行端。
- 振动平台
  - 最终执行器，输出理疗振动。
- 激光测距传感器
  - 通过 UART/Modbus 提供距离数据，固件据此换算重量并判断用户状态。

## 2. 硬件连接拓扑

```text
Android App
│
│ BLE
▼
ESP32-S3
│
├── I2S -> PCM5102A -> TPA3255 -> 振动平台
│
└── UART -> 激光传感器
```

说明：
- App 与 ESP32 仅通过 BLE 交互命令和事件。
- ESP32 通过 I2S 音频链路驱动振动输出。
- ESP32 通过 UART(Modbus)采集激光传感器数据进行安全联锁判断。

## 3. ESP32 引脚定义

引脚定义来源：`src/config/GlobalConfig.h`

- `I2S_BCLK_PIN = 4`
  - I2S 位时钟（BCLK）。
- `I2S_LRCK_PIN = 5`
  - I2S 声道/字时钟（LRCK/WS）。
- `I2S_DOUT_PIN = 6`
  - I2S 数据输出（DIN 到 PCM5102A）。
- `RX_PIN = 15`
  - UART 接收（ESP32 接收激光传感器数据）。
- `TX_PIN = 14`
  - UART 发送（ESP32 发送 Modbus 请求）。

## 4. 音频链路

音频与振动输出链路如下：

1. `WaveModule` 接收频率/强度/启停参数。
2. 在 `I2S_Audio` 任务内使用 DDS 思路生成正弦波（查表 + 相位累加）。
3. 生成 PCM 缓冲后调用 `i2s_write` 送入 I2S DMA。
4. PCM5102A 将数字信号转换为模拟信号。
5. TPA3255 对模拟信号放大。
6. 振动平台输出机械振动。

关键点：
- 非 RUNNING 状态下系统会强制停波，避免异常输出。
- 频率与强度在任务内有平滑过渡，降低突变冲击。

## 5. 测重链路

测重与用户状态判定链路如下：

1. `LaserModule` 周期调用 Modbus 读取距离寄存器。
2. 距离值通过零点/比例系数转换为重量值。
3. 使用滑窗 (`WINDOW_N`) 与标准差阈值 (`STD_TH`) 判定稳定体重。
4. 当重量低于 `LEAVE_TH` 判定用户离开。
5. 在运行中若检测到异常变化率可触发跌倒怀疑。

链路输出：
- 稳定体重事件：`EVT:STABLE`
- 参数事件：`EVT:PARAM`
- 实时流事件：`EVT:STREAM`

## 6. 安全联锁

安全联锁由 `SystemStateMachine` 统一治理：

- 关键状态：`IDLE` / `ARMED` / `RUNNING` / `FAULT_STOP`
- 触发 `FAULT_STOP` 的主要场景：
  - 用户离开检测（`onUserOff`）
  - 传感器读取异常（`onSensorErr`）
  - 跌倒怀疑（`onFallSuspected`）
- `FAULT_STOP` 期间禁止启动输出，且主循环含停波兜底逻辑。

设计结论：
- 振动输出权限不应由单模块直接决定，必须通过状态机裁决，确保安全行为一致。
