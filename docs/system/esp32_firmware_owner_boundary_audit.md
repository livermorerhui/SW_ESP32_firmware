# ESP32 Firmware Owner Boundary Audit

最后更新时间：2026-05-17

## 1. 结论

本审计只锁定后续重构边界，不拆代码。

当前 ESP32 固件可继续联调；剩余结构债主要集中在 `HubHandler`、`BleTransport`、`LaserModule` 三个偏宽 owner。后续如果进入实现包，默认只做内部抽取，不改变：

- `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*`
- `SystemStateMachine` start / stop / safety action timing
- Android APP 消费逻辑
- `MEASUREMENT_UNAVAILABLE` 的 `WARNING_ONLY` 语义
- `WaveModule` ramp / I2S 输出时序

## 2. HubHandler

位置：`src/main.cpp`

当前职责：

- 接收 `ProtocolCodec::parseCommand()` 产出的 `Command`。
- 将命令转发到真正 action owner：
  - `SystemStateMachine`：start / stop / degraded-start / safety switch / BLE lifecycle。
  - `WaveModule`：频率、强度参数。
  - `LaserModule`：设备配置、校准、measurement / stable 查询。
- 生成 `ACK:* / NACK:*` 文本。
- 输出命令分发和配置 truth 串口 evidence。

可安全抽取项：

- ACK builder：把 `ACK:CAP`、`ACK:DEVICE_CONFIG`、`ACK:CAL_*` 等字符串拼装移到纯 helper。
- 命令分发日志 helper：保留日志文本不变，只减少 `HubHandler::handle()` 内部长度。
- calibration model type 文本 helper 可与 calibration owner 靠近。

禁止迁移项：

- 不迁移 `SystemStateMachine::requestStart()` / `requestStop()` 调用时机。
- 不改 legacy `F/I/E` 复用状态机联锁的行为。
- 不改 `ACK:* / NACK:*` 线格式。
- 不把 `CAP?` 语义改成直接读取 `LaserModule` 以外的新 truth source。

后续验证：

- `ProtocolCodec` host-side contract tests。
- `python3 -m platformio run -e esp32s3`。
- 如 ACK builder 被抽出，需新增 ACK 文本 focused tests。

## 3. BleTransport

位置：`src/transport/ble/BleTransport.cpp`

当前职责：

- BLE server / characteristic / advertising 初始化。
- GAP callback、连接 / 断开 session 状态记录。
- MTU 和 connection parameter negotiation。
- RX command queue、control task、TX control / stream queue。
- `SNAPSHOT?` direct query。
- `ProtocolCodec::encodeEvent()` 后的 notify framing / fragmentation。
- TX pressure、critical event drop、reconnect snapshot compensation。
- stalled connection recovery。

可安全抽取项：

- TX frame classifier 与 pressure counters：可抽成内部 helper，但必须保留分类结果。
- advertising profile policy：可抽出 fast / idle profile 决策。
- negotiation / recovery 日志文本 helper：可减小主 transport 文件长度。

禁止迁移项：

- 不改 notify payload、换行 framing、fragmentation 行为。
- 不改 control queue 优先级和 stream queue overwrite 语义。
- 不改 reconnect snapshot dirty / compensation 触发条件。
- 不改 disconnect recovery 的 force-disconnect 时机。
- 不改 `SNAPSHOT?` direct query 的 truth source。

后续验证：

- BLE freeze checklist 先审。
- `ProtocolCodec` contract tests 作为基础防线。
- 真正拆分 BLE transport 前，需要最小真机 capture 覆盖连接、snapshot、start、stop、断开重连。

## 4. LaserModule

位置：`src/modules/laser/LaserModule.cpp`

当前职责：

- Laser / Modbus measurement read loop。
- measurement health 与 low-frequency probe。
- measurement plane `EVT:STREAM` 发布。
- device config 和 calibration model。
- runtime zero、stable filter、presence、baseline/start-ready bridge。
- rhythm state judge 接入、fall candidate、run summary、motion shadow evidence。

已抽出的 pure / focused owners：

- `MeasurementHealthStateMachine`
- `MeasurementAvailabilityProbePolicy`
- `MeasurementPlane`
- `PresenceContractEvaluator`
- `BaselineEvidenceEvaluator`
- `StartGateContractEvaluator`
- `RuntimeZeroObserver`
- `RunSummaryCollector`
- `MotionSafetyShadowEvaluator`
- `StopOutcomeSummaryEvaluator`

可安全抽取项：

- measurement read outcome adapter：只整理 read result 到 health/probe/plane 的内部桥接。
- stable contract logging helper：只移动日志与 writeback evidence 拼装。
- calibration ACK / model validation 相关 helper：不改变存储 key 和线格式。
- run summary state transition helper：保持 `RunSummaryCollector` 仍为串口 evidence。

禁止迁移项：

- 不迁移 final safety / stop action 到 `LaserModule` 或新 owner。
- 不改 `SystemStateMachine::setRuntimeReady()` / `setStartReadiness()` 的调用语义。
- 不改 start gate 与 baseline ready 的真实触发时机。
- 不把 motion shadow 直接接 runtime stop。
- 不改 measurement unavailable degraded-start 授权语义。

后续验证：

- 每次抽取只允许一个窄 owner。
- 保持 `tools/run_evaluator_unit_tests.py` 通过。
- 涉及 measurement / stable / start-ready 的实现包，需要 `esp32s3` 构建和真机 capture。

## 5. 推荐后续顺序

1. 若继续低风险工程质量：先只抽 `HubHandler` ACK builder，并用 host-side tests 锁 ACK 文本。
2. 若进入 BLE：先做 `BleTransport` 详细责任矩阵和 freeze checklist，不直接拆。
3. 若进入 Laser：优先抽日志 / evidence helper，不动 action timing。
4. motion safety runtime action 仍独立专项，不能混入普通结构重构。
