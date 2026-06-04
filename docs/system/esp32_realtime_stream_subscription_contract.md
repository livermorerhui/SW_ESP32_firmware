# ESP32 Realtime Stream Subscription Contract

状态：当前正式
文档类型：合同 / 测试清单
适用范围：ESP32 固件、Android Demo APP、正式 SW APP 对实时测量流 `EVT:STREAM` 的订阅和回归验证
Owner：ESP32 firmware / Demo APP engineering tools
更新日期：2026-06-02
真相源：`docs/protocol.md`、`docs/ble-init-contract.md`、`src/core/ProtocolCodec.h`、`src/HubAckBuilder.h`、`src/modules/laser/LaserModule.*`
证据源：`reports/tasks/stream_set_realtime_subscription_closure/`、`tools/esp32_demo_telemetry_calibration_capture.sh`、`tools/esp32_demo_telemetry_calibration_audit.py`

## 1. 目的

本文固定实时测量流的正式订阅合同，避免再次出现“Demo APP 需要实时距离/体重，但固件默认不再持续上行”的断链问题。

这份合同回答：

- Demo APP 什么时候应收到实时距离和体重。
- 正式 SW APP 为什么可以默认不接收实时测量流。
- 后续验证时应该看哪些证据，而不是只凭体感或 MAX485 LED 闪烁判断。

## 2. 正式合同

### 2.1 能力发现

固件通过 `ACK:CAP` 暴露静态协议能力：

```text
ACK:CAP ... stream_control_supported=1
```

`stream_control_supported=1` 只表示固件支持显式实时流订阅，不表示当前 session 已经开启实时流。

禁止把以下可变状态塞进 `ACK:CAP`：

- `stream_enabled`
- `stream_rate_hz`
- 当前测量健康状态
- 当前距离 / 体重 / baseline 状态

### 2.2 订阅命令

APP 通过以下命令开启或关闭当前 BLE session 的实时流：

```text
STREAM:SET enabled=<0|1>,rate_hz=<1..20>
```

默认参数：

- `enabled=false`
- `rate_hz=10`
- 合法范围：`1..20`

固件确认：

```text
ACK:STREAM enabled=<0|1> supported=1 rate_hz=<rate>
```

### 2.3 生命周期

实时流订阅是 BLE session scoped：

- BLE 连接建立后，固件默认重置为关闭。
- BLE 断开后，固件默认重置为关闭。
- Demo APP 需要遥测曲线、实时距离、实时体重或校准调试时，必须显式发送 `STREAM:SET enabled=1,rate_hz=10`。
- 正式 SW APP 如不需要实时测量曲线，可以不发送 `STREAM:SET`。

### 2.4 不受影响的链路

关闭实时流只影响 BLE 上行 `EVT:STREAM`。

不得影响：

- MAX485 / Modbus 读取。
- 固件内部测量健康。
- 稳定体重。
- baseline / start gate 判断。
- safety / stop 语义。
- 校准算法和模型计算。

## 3. APP 消费边界

### Demo APP

Demo APP 是调试工具，应在连接并确认能力后打开实时流：

1. `CAP?`
2. `ACK:CAP ... stream_control_supported=1`
3. `STREAM:SET enabled=1,rate_hz=10`
4. `ACK:STREAM enabled=1 supported=1 rate_hz=10`
5. 连续接收 `EVT:STREAM`

Demo APP 不应靠设备名、APP 名称或连接来源猜测固件是否要发实时流。

### 正式 SW APP

正式 SW APP 当前主控制链路不需要持续接收实时距离/体重曲线。默认可不发送 `STREAM:SET`。

如果后续正式 SW APP 增加需要实时距离的功能，必须显式接入本合同，而不是要求固件恢复默认推流。

## 4. 验证入口

本地门禁：

```bash
git diff --check
python3 tools/run_evaluator_unit_tests.py
python3 -m platformio run -e esp32s3
cd tools/android_demo && ./gradlew :sonicwave-protocol:test
cd tools/android_demo && ./gradlew :app-demo:assembleDebug
python3 -m py_compile tools/esp32_demo_telemetry_calibration_audit.py
bash -n tools/esp32_demo_telemetry_calibration_capture.sh
```

真机专项入口：

```bash
tools/esp32_demo_telemetry_calibration_capture.sh start
tools/esp32_demo_telemetry_calibration_capture.sh stop --result pass --summary "STREAM:SET explicit subscription passed; telemetry curve and calibration realtime data restored"
tools/esp32_demo_telemetry_calibration_capture.sh audit <CAPTURE_DIR>
```

## 5. 通过标准

真机 evidence 至少应证明：

- `ACK:CAP` 含 `stream_control_supported=1`。
- `ACK:STREAM enabled=1 supported=1 rate_hz=10` 存在。
- ESP32 串口可见 `[STREAM_CONTROL] action=set enabled=1`，或 Android 侧有等价 ACK 证据。
- Android transport 连续收到 `EVT:STREAM`。
- `measurement_health=READY` 或测量链路有可解释的 transient/recovery 证据。
- 没有崩溃、ANR、Guru、Brownout、WDT 等致命异常。

如果没有采到 Demo TX 原始行，但已经有 `ACK:STREAM enabled=1` 和 ESP32 `action=set enabled=1`，可以判定订阅握手完成；ACK 是设备收到并执行请求后的正式证据。

## 6. 已知证据缺口

2026-06-02 的真机通过记录中，Demo APP 未输出结构化 `MEASUREMENT_CONSUME / CAL_*` 消费日志，因此 audit verdict 为：

```text
PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP
```

这表示：

- 实时流传输链路已由日志证明恢复。
- 曲线和校准 UI 通过由用户体感结论支撑。
- 后续如要把 Demo UI 消费链也完全自动化验收，应补 Demo APP 消费层结构化日志，而不是改固件协议或校准算法。

2026-06-02 后续 Demo APP 可观测性包已补齐以下结构化事件：

- `[STREAM_SUBSCRIPTION_RESULT]`
- `[MEASUREMENT_CONSUME_SUMMARY]`
- `[CAL_CAPTURE_ATTEMPT]`
- `[CAL_CAPTURE_RESULT]`

这些事件只用于 Demo APP capture / audit，不改变 BLE wire payload、校准算法、MAX485 参数或正式 SW APP 默认行为。

旧 capture 仍可能保持 `PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP`，因为当时 APP 未输出上述 summary。新版本 Demo APP 的真机验收应优先用这些结构化事件证明“传输 -> 消费 -> 曲线 -> 校准录点”。

## 7. 禁止事项

- 禁止靠 APP 名称、BLE 设备名或连接来源决定是否发送实时流。
- 禁止为了减少流量改写 `EVT:STREAM` wire payload。
- 禁止把实时流订阅关闭解释成停止测量或关闭安全判断。
- 禁止因 MAX485 LED 偶发停闪直接判定协议失败；必须以 capture 中的 RS485 读数、`measurement_health`、`ACK:STREAM` 和 `EVT:STREAM` 为证据。
- 禁止把 Demo APP 的调试需求倒推成正式 SW APP 必须持续接收实时流。
