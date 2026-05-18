# Protocol Golden Frame Fixture Phase 2

状态：实现完成
文档类型：阶段报告
适用范围：ESP32 canonical golden frame fixture 与 host-side evaluator
Owner：ESP32 固件协议合同治理
日期：2026-05-18

## 1. 本轮分类

本轮属于整包开发型小包。

目标链路：

- ESP32 canonical fixture
- `tools/run_evaluator_unit_tests.py`
- `HubAckBuilder::cap(...)`
- `ProtocolCodec::encodeSnapshot(...)`

相关链路：

- SW APP mirror fixture
- SW APP `ProtocolGoldenFrameFixtureTest`
- 后续可选跨仓一致性脚本

真相源：

- ESP32 固件正式 BLE 文本协议。

证据源：

- `docs/protocol/golden_frames/sonicwave_ble_frames_v1.jsonl`
- `tools/run_evaluator_unit_tests.py`
- host-side evaluator 输出

不该动的层：

- BLE 线格式
- ESP32 command / start / stop / pause / continue 行为
- `SystemStateMachine` action owner
- Android repository / session runtime / safety notice
- CH341 / hardware adapter

## 2. 本轮实际完成

- 新增 canonical fixture：
  - `docs/protocol/golden_frames/sonicwave_ble_frames_v1.jsonl`
- 更新 host-side evaluator：
  - `tools/run_evaluator_unit_tests.py`
- 更新固件优先级总表：
  - `docs/system/esp32_firmware_optimization_priority_table.md`

## 3. 测试合同

Python 层读取 fixture 并验证：

- fixture 存在且非空。
- `id` 唯一。
- Phase 2 只接受 `device_to_app` frame。
- 必填字段类型正确。
- `frame.length + 1` 不超过 `budget_bytes`。
- raw frame 包含 `required` 字段。
- raw frame 不包含 `forbidden` 字段。

C++ host evaluator 通过临时生成 `GoldenFrames.h` 验证：

- `HubAckBuilder::cap(...)` 输出等于 `ack_cap_plus_v1`。
- `ProtocolCodec::encodeSnapshot(...)` 对 READY slim snapshot 输出等于 `snapshot_slim_ready_v1`。
- `ProtocolCodec::encodeSnapshot(...)` 对 FAULT slim snapshot 输出等于 `snapshot_slim_fault_v1`。

## 4. 验证结果

已执行：

```bash
python3 tools/run_evaluator_unit_tests.py
```

结果：通过，`evaluator unit tests passed`。

后续收口前建议继续执行：

```bash
git diff --check
python3 -m platformio run -e esp32s3
python3 -m platformio run -e esp32_plus_laser_sim
```

## 5. 影响范围

本轮只改测试、fixture 和文档。

不影响：

- BLE 线格式。
- ESP32 command 行为。
- start / stop / pause / continue 时序。
- Android APP 消费链。
- session runtime、safety notice、CH341。

不需要真机验证。

## 6. 剩余风险

- SW APP mirror fixture 与 ESP32 canonical fixture 尚未由脚本自动比较。
- Demo APP protocol module 尚未纳入共享 fixture 策略。
- `ACK:* / EVT:*` 的 fixture 当前主要作为 schema / budget 合同，host-side exact output 先覆盖 `ACK:CAP` 与 slim `SNAPSHOT`。

## 7. 接下来建议

如继续推进，建议做 Phase 3：

```text
ProtocolGoldenFrameFixture Phase 3：跨仓一致性脚本
```

Phase 3 应放在 SW APP 仓库或独立联调脚本中，默认不让普通 CI 因缺少另一个仓库而失败。
