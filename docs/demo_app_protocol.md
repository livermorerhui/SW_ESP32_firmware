## SonicWave Demo APP 协议（与固件对齐）

本文件以 `src/` 当前实现为准，作为 Android demo 联调协议说明。

### 1. BLE 基本信息

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic（APP -> ESP32）: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`（Write）
- TX Characteristic（ESP32 -> APP）: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`（Notify）
- 设备名: `SonicWave_Hub`

### 2. 主协议（Primary）

#### 2.1 能力探测
- 请求: `CAP?`
- 预期回包: `ACK:CAP fw=SW-HUB-1.0.0 proto=1`

#### 2.2 波形控制
- 设置参数: `WAVE:SET f=<freq>,i=<intensity>`
  - 兼容输入键：`freq` 和 `amp`
- 启动: `WAVE:START`
- 停止: `WAVE:STOP`

#### 2.3 测重与校准
- 归零: `SCALE:ZERO`
- 校准: `SCALE:CAL z=<zeroDistance>,k=<scaleFactor>`

### 3. Legacy 兼容协议

当 `CAP?` 无响应时，可降级 legacy：

- 波形：`F:<freq>,I:<intensity>`（字段可部分出现）
- 启停：`E:1`（start） / `E:0`（stop）
- 归零：`ZERO`
- 标定：`SET_PS:<zeroDistance>,<scaleFactor>`

说明：若 `CAP?` 收到 `NACK/ERR/不可识别响应`，说明链路可用但协议不匹配，不应静默降级。

### 4. 固件回包与事件

#### 4.1 ACK / NACK / ERR
- `ACK:OK`
- `ACK:CAP fw=... proto=...`
- `NACK:<reason>`（例如 `NACK:INVALID_PARAM` / `NACK:NOT_ARMED` / `NACK:FAULT_LOCKED` / `NACK:UNKNOWN_CMD`）

#### 4.2 事件（固件输出）
- `EVT:STATE IDLE|ARMED|RUNNING|FAULT_STOP`
- `EVT:FAULT <code>`（例如 `100` 表示 USER_OFF）
- `EVT:STABLE:<weightKg>`
- `EVT:PARAM:<zeroDistance>,<scaleFactor>`
- `EVT:STREAM:<distance>,<weightKg>`

Demo 还兼容解析裸 CSV：`<distance>,<weightKg>`。

### 5. Primary / Legacy 判定策略

- Primary: 在超时窗口内收到 `ACK:CAP ...`
- Unknown(协议不匹配): 收到 `NACK/ERR/其他无法识别响应`
- Legacy: 超时窗口内完全无响应（无任何 notify）

### 6. 最小联调流程（建议）

1. 连接并订阅 notify。
2. `CAP?`（确认 primary）。
3. `WAVE:SET f=20,i=80`。
4. `WAVE:START`。
5. `WAVE:STOP`。
6. `SCALE:ZERO`。
7. （可选）观察 `EVT:STREAM` 或裸 CSV。

断连时固件会安全停机；断连瞬间可能收不到完整 `EVT`，以固件停机行为为准。
