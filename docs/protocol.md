# SonicWave BLE 协议文档

## 1. GATT 服务结构

- Service UUID
  - `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic（App -> Firmware）
  - UUID: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  - 属性: `Write`
- TX Characteristic（Firmware -> App）
  - UUID: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
  - 属性: `Notify`

说明：
- App 通过 RX 写入字符串命令。
- 固件通过 TX 通知 ACK/NACK 和事件。

## 2. 控制指令

### 2.1 `CAP?`
- 格式：`CAP?`
- 作用：查询固件版本与协议版本。
- 示例：
  - 请求：`CAP?`
  - 响应：`ACK:CAP fw=SW-HUB-1.0.0 proto=1`
- 错误情况：一般无参数错误；若链路异常可能无响应。

### 2.2 `WAVE:SET`
- 格式：`WAVE:SET freq=<float> amp=<int>`
- 参数：
  - `freq`: 0 ~ 50
  - `amp`: 0 ~ 120
- 示例：
  - 请求：`WAVE:SET freq=40 amp=80`
  - 响应：`ACK:OK`
- 错误情况：
  - 缺失参数或越界：`NACK:INVALID_PARAM`

### 2.3 `WAVE:START`
- 格式：`WAVE:START`
- 作用：请求进入运行状态并启用输出。
- 示例：
  - 请求：`WAVE:START`
  - 响应：`ACK:OK`
- 错误情况：
  - 未满足启动条件：`NACK:NOT_ARMED`
  - 故障锁定窗口：`NACK:FAULT_LOCKED`

### 2.4 `WAVE:STOP`
- 格式：`WAVE:STOP`
- 作用：请求停止输出。
- 示例：
  - 请求：`WAVE:STOP`
  - 响应：`ACK:OK`

### 2.5 `SCALE:ZERO`
- 格式：`SCALE:ZERO`
- 作用：触发零点校准。
- 示例：
  - 请求：`SCALE:ZERO`
  - 响应：`ACK:OK`

### 2.6 `SCALE:CAL`
- 格式：`SCALE:CAL z=<float> k=<float>`
- 参数：
  - `z`: zeroDistance
  - `k`: scaleFactor
- 示例：
  - 请求：`SCALE:CAL z=-22.0 k=1.0`
  - 响应：`ACK:OK`
- 错误情况：
  - 缺失 `z` 或 `k`：`NACK:INVALID_PARAM`

## 3. Legacy 协议

兼容命令：
- `ZERO`
- `SET_PS:<zero>,<factor>`
- `F:<freq>,I:<intensity>,E:<0|1>`（字段可部分出现）

`F/I/E` 字段说明：
- `F:` 频率
- `I:` 强度
- `E:` 启停位（`1` 启动，`0` 停止）

兼容原因：
- 旧版 App/调试工具仍使用历史文本协议。
- 固件保留兼容解析，降低现场升级切换成本。
- Legacy 启停已接入状态机联锁，避免绕过安全策略。

## 4. ACK / NACK

### 4.1 ACK
- `ACK:OK`
  - 命令执行成功。
- `ACK:CAP fw=<ver> proto=<ver>`
  - 能力查询成功返回。

### 4.2 NACK
- `NACK:INVALID_PARAM`
  - 参数缺失、格式错误或越界。
- `NACK:FAULT_LOCKED`
  - 当前处于故障锁定窗口。
- 常见补充：
  - `NACK:NOT_ARMED`
  - `NACK:UNKNOWN_CMD`
  - `NACK:UNSUPPORTED`
  - `NACK:EMPTY`

## 5. 事件

### 5.1 `EVT:STATE`
- 格式：`EVT:STATE IDLE|ARMED|RUNNING|FAULT_STOP`
- 含义：系统主状态变化。

### 5.2 `EVT:FAULT`
- 格式：`EVT:FAULT <code>`
- 含义：故障触发，`code` 对应故障枚举（如用户离位、跌倒怀疑、传感器异常）。

### 5.3 `EVT:STABLE`
- 格式：`EVT:STABLE:<weight>`
- 含义：稳定体重判定结果。

### 5.4 `EVT:PARAM`
- 格式：`EVT:PARAM:<zero>,<factor>`
- 含义：当前校准参数上报。

### 5.5 `EVT:STREAM`
- 格式：`EVT:STREAM:<dist>,<weight>`
- 含义：实时调试流（距离、重量）。

## 6. App 与固件通信流程

### 6.1 App -> Command -> ESP32

```text
App 生成命令字符串
    -> 写入 BLE RX Characteristic
    -> 固件 parseCommand
    -> CommandBus dispatch
    -> HubHandler 调用状态机/模块
    -> TX Notify ACK/NACK
```

### 6.2 ESP32 -> Event -> App

```text
模块或状态机产生事件
    -> EventBus::publish
    -> BleTransport::onEvent
    -> encodeEvent
    -> TX Notify 发送事件
    -> App 更新状态/显示数据
```

通信建议：
- App 侧为命令设置超时重试。
- 对 `EVT:STREAM` 采用节流显示，避免 UI 线程拥塞。
- 对 `FAULT_STOP` 事件使用高优先级提醒并禁止继续启动。

