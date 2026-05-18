# ESP32 Firmware A1-A3 Engineering Standardization

状态：已完成本地实现与验证，未提交
日期：2026-05-18
类型：低风险工程标准化包

## 1. 本轮分类

本轮属于 ESP32 固件低风险整包开发。

目标链路：

- 固件 release hardening / minimum soak 状态真相源。
- `HubAckBuilder` ACK/NACK 文本防漂移。
- 旧协议 / 旧开发文档入口防误导。

相关链路：

- SW release hardening capture `20260518_103916...minimum_soak_release_esp32_plus`。
- `tools/run_evaluator_unit_tests.py` host-side focused tests。
- `docs/protocol/golden_frames/sonicwave_ble_frames_v1.jsonl` canonical fixture。

不该动的层：

- BLE 线格式和字段语义。
- Android APP 消费逻辑。
- `SystemStateMachine` start / stop / safety action timing。
- `WaveModule` ramp / I2S 输出时序。
- `LaserModule` measurement / stable / presence runtime 行为。

## 2. 实际完成

### A1: minimum soak 状态同步

更新 `docs/system/esp32_firmware_optimization_priority_table.md`：

- 记录 SW `52f782f` + ESP32 `5bb7e0c` 已完成 ESP32-plus minimum soak。
- 将 `FW-OPT-009` 从待规划更新为当前基线已完成。
- 明确如固件或 APP commit 变化，需要重新跑最小 capture。

### A2: `HubAckBuilder` focused tests 补强

更新 `tools/run_evaluator_unit_tests.py`：

- 补 `ACK:CAP` BASE / PLUS 两种 profile 文本。
- 补 `ACK:DEVICE_CONFIG` BASE / PLUS 文本。
- 补 `ACK:DEGRADED_START enabled/available` true / false。
- 补 `NACK:` 空 reason 边界。
- 补 calibration model rejected 空 reason。
- 补 `ACK:FALL_STOP`、`ACK:LEAVE_PROTECTION`、`ACK:MOTION_SAMPLING` true / false 文本。

本包只锁当前文本合同，不改 `HubAckBuilder` 实现，不改变 BLE 线格式。

### A3: 旧文档状态标注

更新：

- `docs/protocol.md`
- `docs/firmware_developer_guide.md`
- `docs/safety_design.md`

新增 current / legacy / historical 状态横幅，明确当前真相源：

- `docs/system/esp32_app_communication_semantics.md`
- `docs/system/esp32_ble_safe_refactor_freeze_checklist.md`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `docs/system/esp32_firmware_owner_boundary_audit.md`
- `docs/start-readiness-contract.md`
- canonical golden frame fixture 与 host-side tests

## 3. 验证

已执行：

```bash
git diff --check
python3 tools/run_evaluator_unit_tests.py
python3 -m platformio run -e esp32s3
```

结果：

- `git diff --check` 通过。
- `python3 tools/run_evaluator_unit_tests.py` 通过，输出 `evaluator unit tests passed`。
- `python3 -m platformio run -e esp32s3` 通过。

PlatformIO 输出 `Obsolete PIO Core v6.1.18 is used`，属于本机多版本提示，不影响构建结果。

## 4. 未做事项

- 未改 BLE 线格式。
- 未改 Android APP mirror fixture。
- 未改 `SystemStateMachine` action owner。
- 未拆 `BleTransport`。
- 未拆 `LaserModule` runtime 行为。
- 未进行真机测试；本包不改变固件运行行为，按计划不需要本轮真机验证。

## 5. 后续建议

下一包如果继续推进工程标准化，建议进入 `LaserModule` diagnostics / evidence helper 抽取。边界仍应保持：

- 只移动日志和 evidence 拼装。
- 不迁移 start-ready / baseline ready 触发时机。
- 不迁移 final safety / stop action。
- 通过 host-side tests、`esp32s3` 构建；如触碰 measurement / stable runtime，再准备真机 capture。
