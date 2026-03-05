# SonicWave 系统架构文档

## 1. 系统总体架构

SonicWave 由 Android App、BLE 通信链路、ESP32 固件、Wave 输出链路、Laser 测重链路组成。

```text
Android App
    |
    | BLE (GATT Write/Notify)
    v
BleTransport
    |
    v
CommandBus
    |
    v
SystemStateMachine
   |               \
   |                \
   v                 v
WaveModule        LaserModule
   |                 |
   v                 v
I2S Wave Out     Modbus Distance/Weight
```

系统职责分层：
- App：用户交互、发送控制命令、展示状态与事件。
- BLE：命令与事件通道。
- Firmware：状态控制、安全联锁、实时任务执行。
- Wave 输出：根据状态机许可输出声波振动。
- Laser 测重：感知用户在位、离位与异常趋势。

## 2. 固件模块架构

### 2.1 模块职责

- `LaserModule`
  - 周期读取 Modbus 距离寄存器。
  - 进行重量换算、稳定体重判定、离位判定。
  - 在异常时触发状态机故障路径。
  - 发布 `STABLE/PARAM/STREAM` 事件。

- `WaveModule`
  - 初始化 I2S 与 DMA。
  - 查表合成 + 参数平滑（频率/强度）。
  - 根据 `enable` 输出波形或静音。

- `BleTransport`
  - 建立 BLE Service / RX / TX。
  - 接收 App 指令，调用 `ProtocolCodec::parseCommand`。
  - 通过 `CommandBus` 分发命令。
  - 通过 `EventBus` sink 上报事件给 App。

- `SystemStateMachine`
  - 管理 `IDLE/ARMED/RUNNING/FAULT_STOP`。
  - 统一处理启动条件、故障锁定和恢复窗口。
  - 保证安全策略先于输出执行。

- `EventBus`
  - 事件发布通道（当前 sink 为 `BleTransport`）。

- `CommandBus`
  - 命令派发通道（handler 为 `HubHandler`）。

### 2.2 模块关系与调用链

命令调用链：
```text
App -> BleTransport(onWrite)
    -> ProtocolCodec::parseCommand
    -> CommandBus::dispatch
    -> HubHandler(main.cpp)
    -> {WaveModule, LaserModule, SystemStateMachine}
```

事件调用链：
```text
LaserModule/SystemStateMachine -> EventBus::publish
    -> BleTransport::onEvent
    -> ProtocolCodec::encodeEvent
    -> BLE notify -> App
```

## 3. 线程架构

### 3.1 FreeRTOS 任务

- 主任务（Arduino `setup/loop`）
  - 初始化模块。
  - 执行断连与非 RUNNING 停波兜底。

- `I2S_Audio`（`WaveModule`）
  - `xTaskCreatePinnedToCore(..., "I2S_Audio", ..., priority=4, core=1)`
  - 连续生成并写入 I2S 缓冲。

- `LaserTask`（`LaserModule`）
  - `xTaskCreatePinnedToCore(..., "LaserTask", ..., priority=2, core=1)`
  - 周期采样 Modbus，驱动安全状态与体重事件。

- BLE callbacks
  - `onConnect/onDisconnect/onWrite` 由 BLE 协议栈触发。
  - 不应假设固定核心，按回调上下文编写线程安全代码。

### 3.2 Core0 / Core1 说明

- Core1：当前业务任务集中（主任务、I2S_Audio、LaserTask）。
- Core0：主要由系统/协议栈任务使用（取决于 Arduino-ESP32 与 BLE 栈调度）。
- 设计建议：避免跨上下文直接共享可变数据，必要时用队列或互斥。

## 4. 数据流

### 4.1 Command Flow

```text
App Command String
    -> BLE RX Write
    -> parseCommand
    -> dispatch(CommandBus)
    -> HubHandler
    -> 状态机校验 + 模块执行
    -> ACK/NACK 回发
```

### 4.2 Event Flow

```text
状态变化/测重结果/故障
    -> EventBus::publish
    -> BleTransport::onEvent
    -> encodeEvent
    -> BLE TX Notify
    -> App 更新 UI
```

### 4.3 Audio Data Flow

```text
freq/intensity/enable
    -> WaveModule shared targets
    -> I2S_Audio task 平滑
    -> PCM buffer
    -> i2s_write(DMA)
    -> 外设输出
```

## 5. Safety Interlock 架构

振动控制必须经过 `SystemStateMachine`，原因：
- 启动前必须满足在位和无故障前提，防止误触发输出。
- 运行中一旦出现离位、断连、传感器异常，必须立即切换到安全态。
- 状态机提供统一“可运行性判定”，避免各模块各自判断导致规则不一致。

当前实现的关键约束：
- `WAVE:START` 与 Legacy `E:1` 启动都走 `requestStart()`。
- 主循环中非 `RUNNING` 状态会强制 `g_wave.setEnable(false)` 作为兜底。
- BLE 断连路径触发 `onUserOff()` + 停波。

## 6. 扩展架构

未来模块（EEG / HeartRate / SpO2）接入建议：

1. 新增 `src/modules/<module>/` 模块，封装采样与本地算法。
2. 通过 `EventBus` 发布标准化事件，不直接耦合 BLE 细节。
3. 若影响安全联锁，新增状态机输入事件并集中处理。
4. 在 `ProtocolCodec` 中定义事件编码，在 App 端扩展解码。

建议扩展模式：
```text
NewSensorModule
    -> EventBus
    -> BleTransport
    -> App

NewSensorModule (Safety Related)
    -> SystemStateMachine event API
    -> state/fault transition
    -> EventBus fault/state notify
```

