# ESP32 Firmware LSP Stable Contract Helpers

状态：已完成本地实现与验证，未提交
日期：2026-05-18
类型：LaserModule 低风险瘦身 / stable contract diagnostics / stable window focused tests

## 1. 本轮分类

本轮属于 ESP32 固件低风险整包开发。

目标链路：

- `LaserModule` stable / baseline / presence 的只读 evidence logging。
- stable window metrics / trimmed mean host-side contract tests。

相关链路：

- `SNAPSHOT.start_ready`
- `SNAPSHOT.baseline_ready`
- `EVT:BASELINE`
- `WAVE:START` allow/reject
- user leave / fall safety action

不该动的层：

- BLE 线格式。
- Android APP 消费逻辑。
- `SystemStateMachine` final start / stop / safety action owner。
- `SystemStateMachine::setRuntimeReady` / `setStartReadiness` / `onUserOff`
  调用时机。
- baseline latch / clear timing。
- occupied-cycle lock / release timing。
- stable 阈值、stable build interval、baselineReady latch 条件。

## 2. 实际完成

新增：

- `src/modules/laser/BaselineContractDiagnostics.h`
- `src/modules/laser/BaselineContractDiagnostics.cpp`
- `src/modules/laser/LaserStableWindow.h`
- `src/modules/laser/LaserStableWindow.cpp`

更新：

- `src/modules/laser/LaserModule.h`
- `src/modules/laser/LaserModule.cpp`
- `tools/run_evaluator_unit_tests.py`
- `docs/system/esp32_firmware_optimization_priority_table.md`

## 3. 抽象边界

`BaselineContractDiagnostics` 只负责：

- baseline contract state view。
- `[BASELINE_CONTRACT] event=latch` 串口 evidence。
- `[BASELINE_CONTRACT] event=clear` 串口 evidence。
- `[BASELINE_CONTRACT] event=start_ready_writeback` 串口 evidence。
- start-ready writeback 日志节流状态。

它不负责：

- 修改 `StableContractState`。
- 调用 `SystemStateMachine`。
- 调用 `RhythmStateJudge`。
- 执行 baseline latch / clear。
- 发布 BLE event。

`LaserStableWindow` 只负责：

- ring window stats。
- stable window metrics。
- trimmed mean。

它不负责：

- 决定是否 latch stable。
- 修改 stable confirm count。
- 读取 `SystemStateMachine`。
- 调整阈值或时序。

## 4. 测试覆盖

新增 host-side focused tests：

- stable window full-window mean / range / drift / stddev。
- ring-head 满窗口场景。
- invalid window 场景。
- trimmed mean 去掉极端值和 trim 过大 fallback 场景。

已有 evaluator tests 继续覆盖：

- `PresenceContractEvaluator`
- `BaselineEvidenceEvaluator`
- `StartGateContractEvaluator`
- `CalibrationRuntime`
- measurement health / probe policy
- protocol / ACK contracts

## 5. 验证

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

## 6. 行为影响

本包不需要真机测试：

- 未改 BLE 正式合同。
- 未改 start / stop / safety action timing。
- 未改 measurement runtime 行为。
- 未改 stable threshold / latch 条件。
- 未改 Android 消费语义。

## 7. 后续建议

如果继续瘦身，只建议先评估：

- `LSP-003 Presence Counter Decision Wrapper`

仍不建议自动进入：

- stable candidate owner。
- baseline latch owner。
- presence owner state carrier。
- occupied-cycle owner。

这些属于中风险动作时序迁移，必须先做单独审计和真机 capture 计划。
