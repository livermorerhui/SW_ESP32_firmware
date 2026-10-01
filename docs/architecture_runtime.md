# SonicWave Runtime Architecture

本文基于当前仓库源码（`src/`）提取真实运行结构，不使用假设模型。

## 1. 任务结构

### 1.1 `audioTask`（`WaveModule::audioTask`）
- 创建位置：`WaveModule::begin()`
- 创建方式：`xTaskCreatePinnedToCore(..., "I2S_Audio", ..., priority=4, core=1)`
- 源码：`src/modules/wave/WaveModule.cpp`
- 周期特征：无显式 `vTaskDelay`，持续循环生成 DDS 数据并 `i2s_write(..., portMAX_DELAY)` 阻塞等待 DMA
- 职责：WAVE LUT 正弦合成、频率/强度平滑、软停静音、独立 CONNECT/DISCONNECT CUE 合成、I2S DMA 输出
- 约束：WAVE 和 CUE 共用这一个 I2S 写入 owner；CUE 不改变 WAVE 参数，也不改变 `isOutputActive()` 的 WAVE-only 含义

### 1.2 `laserTask`（`LaserModule::taskLoop`）
- 创建位置：`LaserModule::startTask()`
- 创建方式：`xTaskCreatePinnedToCore(..., "LaserTask", ..., priority=2, core=1)`
- 源码：`src/modules/laser/LaserModule.cpp`
- 周期特征：每轮 `vTaskDelay(20ms)`，传感器读取带 `200ms` 节流门限
- 职责：Modbus 距离读取、重量换算、稳定判定、离开检测、故障触发、事件发布

### 1.3 BLE callbacks（`BleTransport`）
- 回调：`onConnect` / `onDisconnect` / `onWrite`
- 源码：`src/transport/ble/BleTransport.cpp`
- Core：源码未硬编码绑定 core（由 BLE 协议栈调度）
- 触发特征：事件驱动
- 职责：接收命令字符串、协议解析、命令分发、断连联锁触发、notify 回传

### 1.4 `main loop`（Arduino `loop()`）
- 源码：`src/main.cpp`
- Core：源码未显式 pin（Arduino 主循环上下文）
- 周期特征：`delay(20)`
- 职责：保持主循环轻量，实时工作由 FreeRTOS 任务与 BLE 回调承担

## 2. 状态机

### 2.1 真实状态（源码）
`TopState` 定义在 `src/core/Types.h`：
- `IDLE`
- `ARMED`
- `RUNNING`
- `FAULT_STOP`

> 说明：你要求的三态 `IDLE/RUNNING/FAULT_STOP` 均存在；当前实现还有 `ARMED` 作为启动前置状态。

### 2.2 转换条件（`src/core/SystemStateMachine.cpp`）
- `IDLE -> ARMED`
  - 条件：`onUserOn()`（重量超过 `MIN_WEIGHT` 且用户上台）
- `ARMED -> RUNNING`
  - 条件：`requestStart()` 且非故障锁
- `RUNNING -> IDLE`
  - 条件：`requestStop()`
- `ANY -> FAULT_STOP`
  - 条件之一：`onUserOff()` / `onSensorErr()` / `onFallSuspected()`
- `FAULT_STOP -> IDLE`
  - 条件：冷却窗口结束后，`onWeightSample()` 连续满足低于 `LEAVE_TH` 达到 `CLEAR_CONFIRM_MS`

### 2.3 输出闸门
- `SystemStateMachine::setState()` 是唯一启停波形输出的闸门：
  - 进入 `RUNNING` -> `WaveModule.start()`
  - 其他状态 -> `WaveModule.stopSoft()`

## 3. 四条关键调用路径

### 3.1 WAVE START
```text
APP
-> BLE Transport (RX write callback)
-> ProtocolCodec::parseCommand()
-> CommandBus::dispatch()
-> HubHandler::handle(WAVE_START)
-> SystemStateMachine::requestStart()
-> SystemStateMachine::setState(RUNNING)
-> WaveModule.start()
```

### 3.2 WAVE STOP
```text
APP
-> BLE Transport (RX write callback)
-> ProtocolCodec::parseCommand()
-> CommandBus::dispatch()
-> HubHandler::handle(WAVE_STOP)
-> SystemStateMachine::requestStop()
-> SystemStateMachine::setState(IDLE)
-> WaveModule.stopSoft()
```

### 3.3 USER LEAVE
```text
LaserModule::taskLoop()
-> weight < LEAVE_TH && userPresent
-> SystemStateMachine::onBleDisconnected()
-> SystemStateMachine 进入可恢复暂停并调用唯一停波 owner
-> WaveModule.stopSoft()
```

### 3.4 BLE DISCONNECT
```text
BleTransport::MyServerCallbacks::onDisconnect()
-> 捕获 sessionId/connId 后将 BLE_DISCONNECTED 投递到控制队列
-> BleDisconnectSink::onBleDisconnect(sessionId, connId, connIdKnown) (HubHandler)
-> SystemStateMachine::onUserOff()
-> SystemStateMachine::setState(FAULT_STOP)
-> WaveModule.stopSoft()
-> WaveModule::audioTask 等待 WAVE 静音并停止 I2S
-> 同一 audioTask 播放 DISCONNECT CUE
```

### 3.5 CONNECT CUE
```text
BleTransport::MyServerCallbacks::onConnect()
-> 捕获当前 sessionId/connId，并将 BLE_CONNECTED 投递到控制队列
-> BleTransport::controlTaskLoop() 校验当前连接生命周期
-> HubHandler::onBleConnected()
-> WaveModule::requestCue(CONNECT, 当前 sessionId)
-> WaveModule::audioTask 播放 CONNECT CUE
```

`AUDIO:CUE id=CONNECT` 仍保留为旧 APP 的兼容入口。旧 APP 可能在协议确认后发送该命令；固件按当前 `sessionId` 去重，返回 `DUPLICATE` 时不重复发声。正式 APP 不再发送连接提示音命令，CAP/SNAPSHOT/WAVE 等正常通讯仍按原协议运行。

## 4. Control Plane 与 Data Plane 对应

### 4.1 Control Plane
`APP -> BleTransport -> ProtocolCodec -> CommandBus -> SystemStateMachine -> WaveModule/LaserModule`

### 4.2 Data Plane
`WaveModule（WAVE/CUE） -> DDS/LUT -> I2S DMA -> PCM5102A -> TPA3255 -> Platform`

### 4.3 Event Flow
`LaserModule + SystemStateMachine -> EventBus -> BleTransport -> APP`

## 5. 关键接口索引
- `ProtocolCodec::parseCommand` / `ProtocolCodec::encodeEvent`
- `CommandBus::dispatch`
- `EventBus::publish`
- `SystemStateMachine::requestStart` / `requestStop` / `onUserOff` / `onSensorErr`
- `WaveModule::start` / `stopSoft`
- `LaserModule::taskLoop`（触发在位、离位、故障、稳定事件）
