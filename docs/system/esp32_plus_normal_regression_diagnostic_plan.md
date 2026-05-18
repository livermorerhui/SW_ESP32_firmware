# ESP32-plus 正常版连接回归诊断计划

## 背景

本轮问题只在烧录新固件后出现，且用户补充：

- ESP32-base 在新固件下连接基本正常。
- ESP32-plus 故障态在新固件下连接基本正常。
- ESP32-plus 正常态连接后出现断开、指示灯全灭再亮、Demo APP 停止按钮不变色、SW APP 连接后马上断开。

因此当前优先级不是泛化地怀疑 APP 或硬件，而是定位新固件在 `PLUS + MAX485/laser 正常` 链路上的回归。

## 目标链路

目标链路：

- APP 扫描并连接 ESP32-plus 正常版。
- APP 完成 notify 订阅。
- APP 发送 `CAP? / SNAPSHOT?`。
- 固件返回 `ACK:CAP / SNAPSHOT`。
- APP 发送 `WAVE:START / WAVE:STOP`。
- 固件返回 `ACK:*` 与 `EVT:WAVE_OUTPUT / EVT:STATE / EVT:STOP`。
- 正常测量链持续通过 MAX485/Modbus 读 laser，并按策略输出 `EVT:STREAM`。

## 相关链路

相关链路包括：

- BLE 连接生命周期：connect、disconnect、advertising restart、reconnect。
- BLE 协商：MTU 与 connection parameter update。
- BLE TX 压力：control queue、stream queue、关键帧 drop/skip。
- MAX485/Modbus：读耗时、成功率、失败率、方向控制。
- ESP32 稳定性：brownout、WDT、panic、重复 boot。
- APP GATT：连接状态、服务发现、notify descriptor enable、write result、RX line ingest。

## 真相源和证据源

真相源：

- ESP32 串口日志中的 boot/reset、BLE lifecycle、Modbus、BLE TX 诊断。
- APP logcat 中的 GATT 状态、TX/RX 关键事件。

证据源：

- SW 仓 `.artifacts/device-test-captures/*/esp32_serial.log`
- SW 仓 `.artifacts/device-test-captures/*/android_logcat_focus.log`
- SW 仓 `.artifacts/device-test-captures/*/android_logcat_full.log`
- SW 仓 `.artifacts/device-test-captures/*/runtime_events_session.jsonl`
- SW 仓 `.artifacts/device-test-captures/*/diagnostic_summary.md`
- 用户体感记录和脚本 `mark` 时间点

## 不该动的层

诊断阶段禁止直接改：

- `CAP? / SNAPSHOT? / WAVE:* / EVT:* / ACK:* / NACK:*` BLE 线格式。
- Demo APP / SW APP 已依赖的正式协议字段。
- Android session / BLE 主链业务语义。
- backend-api / admin-web。

诊断阶段允许改：

- ESP32 内部日志。
- ESP32 内部诊断开关。
- capture profile、capture wrapper、离线报告脚本。
- 文档和测试矩阵。

## 需要补充的 ESP32 日志

建议增加以下日志，日志前缀固定，方便脚本自动识别：

- `BOOT_DIAG`
  - `reset_reason`
  - `heap_free`
  - `psram_free`
  - `platform_model`
  - `laser_installed`
  - `fw`
  - `board`
- `BLE_LIFECYCLE`
  - `event=connect|disconnect`
  - `session_id`
  - `conn_id`
  - `raw_reason`
  - `connected_count`
- `BLE_NEGOTIATION`
  - `kind=mtu|conn_params`
  - `action=request|result|skip`
  - `status`
  - `value`
- `BLE_ADV`
  - `action=start|stop|skip|retry`
  - `reason`
  - `active`
  - `connected`
  - `since_last_restart_ms`
- `BLE_TX_PRESSURE`
  - `queue=control|stream`
  - `depth`
  - `drop`
  - `skip`
  - `class=ACK|SNAPSHOT|CRITICAL_EVENT|STREAM`
- `MEASUREMENT_DIAG`
  - `read_ms`
  - `ok_count`
  - `fail_count`
  - `max_read_ms`
  - `stream_rate_hz`
  - `interval_ms`

## 2026-04-25 诊断最小包落地范围

本轮已经落地的 ESP32 诊断日志：

- `BOOT_DIAG`
  - 已覆盖：`fw`、`proto`、`reset_reason`、`heap_free`、`heap_min_free`、`psram_free`、`board`、`psram_enabled`
  - 用途：判断用户看到的“灯全灭再亮”是否对应 ESP32 重启、brownout、WDT 或 panic。
- `BLE_LIFECYCLE`
  - 已覆盖：connect / disconnect、`session_id`、`conn_id`、`raw_reason`、`connected_count`、TX skip/drop 摘要。
  - 用途：判断 SW APP / Demo APP 看到的断链是否确实发生在固件 BLE 层。
- `BLE_ADV`
  - 已覆盖：advertising start / stop / skip、skip reason、300ms restart rate limit。
  - 用途：判断快速断链后是否广告重启被限流跳过。
- `BLE_TX_PRESSURE`
  - 已覆盖：control / stream queue 深度、水位、drop、skip、stream replace、stream suppress。
  - 用途：判断 PLUS 正常测量流是否压住 `ACK / SNAPSHOT / EVT:STOP` 等关键帧。
- `MEASUREMENT_DIAG`
  - 已覆盖：Modbus read 窗口、ok/fail、last result、last/max read duration、轮询间隔、attempt rate、top state、backoff。
  - 用途：判断 `20ms` 测量轮询是否造成读阻塞、失败风暴或过高测量压力。

本轮没有改变：

- BLE 对外线格式。
- `CAP? / SNAPSHOT? / WAVE:* / EVT:* / ACK:* / NACK:*` 语义。
- 测量轮询参数。
- BLE MTU / connection parameter 行为。
- advertising retry 行为。

## APP / logcat 关注点

APP 侧如果需要补日志，优先补以下 tag 或字段：

- `SW_CONNECT`
  - GATT connect request
  - `onConnectionStateChange status/newState`
- `SW_BLE_GATT`
  - service discovery
  - MTU changed
  - descriptor write result
  - characteristic write result
- `SW_DEVICE_TX`
  - `CAP?`
  - `SNAPSHOT?`
  - `WAVE:START`
  - `WAVE:STOP`
- `SW_DEVICE_RX`
  - `ACK:CAP`
  - `SNAPSHOT`
  - `EVT:WAVE_OUTPUT`
  - `EVT:STOP`
  - stream 只计数，不刷全量内容
- `SW_UI_STATE`
  - start / stop button state
  - state change reason

## 采集工具

SW 仓新增或扩展：

- `scripts/device_test_capture.sh`
  - 新增 profile：`esp32_plus_normal`
  - 新增 recipe：`esp32_plus_normal_connect_diag`
- `scripts/esp32_plus_normal_diag_capture.sh`
  - 包装现有 capture 脚本，固定进入 ESP32-plus 正常版诊断模式。
- `scripts/esp32_plus_diag_report.py`
  - 对最新 capture 生成 `diagnostic_summary.md`。

这些工具只收集和整理证据，不改变 APP / 固件运行行为。

## 测试矩阵

第一批只做定位，不做修复证明：

| Case | 固件 | 设备状态 | 操作 | 目标 |
| --- | --- | --- | --- | --- |
| D1 | 新固件 | PLUS 正常 | 只连接不开始 | 判断连接阶段是否复位或断开 |
| D2 | 新固件 | PLUS 正常 | 连接后开始 10 秒再停止 | 判断测量流和启停控制是否冲突 |
| D3 | 新固件 | PLUS 正常 | 关闭 stream 输出后重复 D2 | 判断是否为 stream 压力 |
| D4 | 新固件 | PLUS 正常 | 轮询退回 200ms 后重复 D2 | 判断是否为 20ms 测量压力 |
| D5 | 新固件 | PLUS 正常 | 禁用 conn param update 后重复 D2 | 判断是否为 BLE 协商 |
| D6 | 新固件 | PLUS 正常 | 用 nRF Connect / LightBlue 手动写命令 | 排除 APP 主链干扰 |
| C1 | 新固件 | BASE | 连接/开始/停止 | 对照低风险链路 |
| C2 | 新固件 | PLUS 故障态 | 连接/开始/停止 | 对照无正常测量流链路 |

## 修复决策规则

- 如果 `BOOT_DIAG` 多次出现或出现 brownout/WDT/panic，先修复复位/供电/任务阻塞问题。
- 如果断开发生在 MTU/conn param update 后，先延后或开关化 BLE 参数更新。
- 如果广告 start 被 skip 且没有 retry，先补 advertising retry。
- 如果 Modbus read 周期或耗时异常，先参数化测量轮询频率。
- 如果 stream 速率高并伴随 control drop/skip，先做 stream 限速或 control 优先级修复。
- 如果 ESP32 已发出关键帧但 APP UI 不变，再转 APP RX/UI 消费链修复。

## 收口标准

正式修复通过必须同时满足：

- ESP32 串口无异常复位。
- Android logcat 能串起 connect -> notify enabled -> `CAP?` -> `SNAPSHOT?` -> start -> stop。
- ESP32 无关键帧 drop/skip。
- PLUS 正常态、PLUS 故障态、BASE 三类至少各通过一条核心链路。
- Demo APP 和 SW APP 各自通过连接、开始、停止主链。
