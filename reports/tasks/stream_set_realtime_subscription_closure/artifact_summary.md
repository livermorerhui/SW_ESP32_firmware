# STREAM:SET 实时流订阅收口摘要

状态：阶段交付记录
文档类型：历史快照 / 交付摘要
适用范围：2026-06-02 ESP32 Demo APP 遥测曲线和校准实时数据修复包
Owner：ESP32 firmware / Android Demo APP engineering tools
更新日期：2026-06-02
当前替代入口：`docs/system/esp32_realtime_stream_subscription_contract.md`
证据源：`main` merge commit `bc18a74`、capture `20260602_152844__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-demo-telemetry-calibration__manual__ESP32_RUNTIME__esp32_demo_telemetry_calibration`

> 本文记录本轮交付过程和证据，不作为后续协议修改的唯一入口。长期合同看 `docs/system/esp32_realtime_stream_subscription_contract.md`。

## 1. 本轮实际完成

- 已恢复 Demo APP 遥测曲线和校准工具所需的实时距离 / 体重流。
- 已把实时流从“固件默认持续上行”改为“APP 显式订阅”：
  - `ACK:CAP stream_control_supported=1`
  - `STREAM:SET enabled=1,rate_hz=10`
  - `ACK:STREAM enabled=1 supported=1 rate_hz=10`
  - `EVT:STREAM ...`
- 已保持正式 SW APP 默认不接收实时测量流的优化空间。
- 已补专项 capture/audit，使真机验证能区分：
  - 固件是否支持订阅。
  - Demo 是否完成订阅握手。
  - `EVT:STREAM` 是否恢复。
  - RS485 / Modbus 是否仍在运行。

## 2. 改动点

### 固件

- `src/core/CommandBus.h`
  - 新增 `CmdType::STREAM_SET` 和 `StreamSubscriptionCommand`。
- `src/core/ProtocolCodec.h`
  - 新增 `STREAM:SET` 解析。
  - 新增默认频率和范围常量：`10Hz`，`1..20Hz`。
- `src/HubAckBuilder.h`
  - `ACK:CAP` 增加静态能力 `stream_control_supported=1`。
  - 新增 `ACK:STREAM ...`。
- `src/main.cpp`
  - 分发 `STREAM_SET` 到 `LaserModule`。
  - BLE connect/disconnect 时重置实时流订阅。
- `src/modules/laser/LaserModule.*`
  - 新增 session scoped stream subscription state。
  - `EVT:STREAM` 只在订阅开启后上行。
  - 测量、baseline、安全判断继续运行。

### Demo APP / SDK

- `sonicwave-protocol`
  - 新增 `Command.StreamSet`。
  - 新增 `Event.StreamSubscription`。
  - 支持编码 `STREAM:SET` 和解析 `ACK:STREAM`。
- `sonicwave-sdk`
  - 新增 `setStreamSubscriptionAndAwaitAck()`。
- `app-demo`
  - 连接后在能力支持时自动开启 `STREAM:SET enabled=1,rate_hz=10`。
  - 记录 `[STREAM_CONTROL]` 系统日志。

### 工具与文档

- `tools/esp32_demo_telemetry_calibration_capture.sh`
  - 作为本功能专项真机采集入口。
- `tools/esp32_demo_telemetry_calibration_audit.py`
  - 增加 `ACK:CAP / ACK:STREAM / EVT:STREAM / ESP32 STREAM_CONTROL` 证据判定。
  - 修复“未采到 Demo TX 原始行但已经有 ACK/ESP32 set”导致的假阴性。
- `docs/protocol.md`
- `docs/ble-init-contract.md`
- `docs/system/esp32_app_communication_semantics.md`
- `tools/android_demo/README.md`

## 3. 抽象点

- 实时流接收从隐式行为改成显式 session scoped subscription。
- APP 类型差异不靠名称猜测，而靠正式协议命令表达。
- `ACK:CAP` 只暴露短静态能力；当前订阅状态通过 `ACK:STREAM` 表达。
- capture/audit 以“设备已 ACK + 固件 set + APP 收到 stream”为握手证据，不依赖单一路日志。

## 4. 参数点

| 参数 | 值 | 说明 |
| --- | --- | --- |
| 默认订阅状态 | 关闭 | BLE connect / disconnect 后重置 |
| Demo APP 订阅频率 | `10Hz` | 当前遥测曲线和校准调试默认值 |
| 允许范围 | `1..20Hz` | 固件 parser 校验 |
| 订阅作用域 | 当前 BLE session | 不写入持久配置 |
| 影响范围 | 只影响 BLE `EVT:STREAM` 上行 | 不停止内部测量和安全判断 |

## 5. 验证结果

本地验证通过：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`
- `cd tools/android_demo && ./gradlew :sonicwave-protocol:test`
- `cd tools/android_demo && ./gradlew :app-demo:assembleDebug`
- `python3 -m py_compile tools/esp32_demo_telemetry_calibration_audit.py`
- `bash -n tools/esp32_demo_telemetry_calibration_capture.sh`

真机验证通过但有消费层日志缺口：

- `session_meta.env`: `RESULT=pass`
- `ACK:CAP stream_control_supported=1`: 4 条
- `ACK:STREAM enabled=1 supported=1 rate_hz=10`: 2 条
- ESP32 `[STREAM_CONTROL] action=set enabled=1`: 1 条
- Android `EVT:STREAM`: 2368 条
- `measurement_diag_ok_total`: 7034
- `measurement_diag_fail_total`: 4
- 致命异常：未发现
- audit verdict: `PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP`

## 6. 文档落点

- 长期合同：`docs/system/esp32_realtime_stream_subscription_contract.md`
- 协议说明：`docs/protocol.md`
- BLE init 边界：`docs/ble-init-contract.md`
- APP 通信语义：`docs/system/esp32_app_communication_semantics.md`
- Demo 使用说明：`tools/android_demo/README.md`
- 本轮历史记录：`reports/tasks/stream_set_realtime_subscription_closure/`

## 7. 剩余风险

- Demo APP 当前缺少结构化 `MEASUREMENT_CONSUME / CAL_*` 消费日志；本轮 UI 通过由用户体感 + 连续 `EVT:STREAM` 间接支撑。
- 真机日志仍显示少量 RS485 transient：`fail=4`，但有恢复证据，且主链通过。
- 远端 `feature/stream-set-subscription` 分支仍保留；如后续不再需要 PR / 回溯，可再清理远端功能分支。

## 8. 接下来建议

推荐下一包：Demo APP 代码质量审计。

审计重点：

- Demo APP 遥测消费层是否需要补结构化日志。
- 校准 UI 是否应输出 `CAL_APP / CAL_UI` 自动验收证据。
- Demo APP 是否需要做 owner 分层整理。

暂不建议：

- 不继续改校准算法。
- 不继续改 MAX485 / Modbus 参数。
- 不继续改 BLE 调度参数。
- 不把正式 SW APP 改成默认接收实时流。
