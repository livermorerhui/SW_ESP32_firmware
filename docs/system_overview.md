# SonicWave System Overview

## 1. 系统整体结构

```text
Android App
│
│ BLE
▼
ESP32 Firmware
│
├── Wave Engine
│
└── Laser Monitoring
```

系统分层说明：
- Android App：发送控制命令，接收状态/故障/测量事件。
- ESP32 Firmware：执行协议解析、状态机联锁、实时任务调度。
- Wave Engine：负责振动波形合成与输出链路驱动。
- Laser Monitoring：负责传感采样、测重计算、安全触发条件检测。

## 2. 软件架构

核心模块：

- `BleTransport`
  - BLE GATT 入口/出口，负责命令接收与事件发送。
- `CommandBus`
  - 命令分发总线，将解析后的命令交给统一处理器。
- `EventBus`
  - 事件发布总线，将内部状态/测量事件转发到通信层。
- `SystemStateMachine`
  - 安全状态治理核心，控制可运行性与故障锁定。
- `WaveModule`
  - I2S 音频生成与输出执行。
- `LaserModule`
  - Modbus 采样、重量计算、离位/异常检测。

模块关系（简化）：

```text
BleTransport -> CommandBus -> Handler -> {SystemStateMachine, WaveModule, LaserModule}
LaserModule/SystemStateMachine -> EventBus -> BleTransport -> App
```

## 3. 数据流

### 3.1 App Command Flow

```text
App 指令
  -> BLE RX
  -> ProtocolCodec parse
  -> CommandBus dispatch
  -> Handler 执行
  -> ACK/NACK 返回
```

### 3.2 Event Flow

```text
状态/测量事件
  -> EventBus publish
  -> BleTransport onEvent
  -> BLE TX Notify
  -> App 展示
```

### 3.3 Audio Data Flow

```text
参数(freq/intensity/enable)
  -> WaveModule
  -> DDS/平滑
  -> I2S DMA
  -> DAC/功放
  -> 振动平台
```

## 4. 安全机制

- Safety Interlock
  - 启动必须通过 `SystemStateMachine::requestStart`。
  - 非运行态由主循环停波兜底。

- Fault Lock
  - 进入 `FAULT_STOP` 后存在冷却锁定窗口（禁止立即重启）。

- Auto Stop
  - 用户离开、BLE 断连、传感器异常等场景触发自动停机。

设计原则：
- 安全相关决策集中在状态机，避免模块各自判断造成行为分裂。

## 5. 扩展能力

未来扩展能力：
- EEG
- Heart Rate
- SpO2

接入方式建议：
1. 新增模块在 `src/modules/` 中独立实现采样任务。
2. 常规数据通过 `EventBus` 上报。
3. 涉及安全风险的信号通过 `SystemStateMachine` 增加事件入口。
4. 在 BLE 协议层扩展事件编码，App 同步增加解码与展示。
