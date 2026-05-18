# ESP32 Firmware Stable / Baseline / Presence Owner Audit

日期：2026-05-18

## 1. 本轮类型

本轮是审计 / 计划包，不改运行代码。

目标链路：

- ESP32 `LaserModule` stable / baseline / presence owner 拆分边界。

相关链路：

- `SNAPSHOT.start_ready`
- `SNAPSHOT.baseline_ready`
- `EVT:BASELINE`
- `WAVE:START` allow/reject
- user leave / fall safety action
- runtime-zero / occupied-cycle effective-zero lock

真相源：

- `src/modules/laser/LaserModule.cpp`
- `src/modules/laser/PresenceContractEvaluator.*`
- `src/modules/laser/BaselineEvidenceEvaluator.*`
- `src/modules/laser/StartGateContractEvaluator.*`
- `src/core/SystemStateMachine.*`
- `docs/start-readiness-contract.md`

不该动的层：

- BLE 线格式。
- Android APP 消费逻辑。
- `SystemStateMachine` final start / stop / safety action owner。
- `WaveModule` ramp / I2S 输出时序。

## 2. 审计结论

`LaserModule` 的 stable / baseline / presence 区域仍有结构债，但不是“继续直接拆”的低风险区。

当前已抽出的 owner 是合理的：

- `PresenceContractEvaluator`：纯 presence 判定。
- `BaselineEvidenceEvaluator`：纯 stable window / baseline eligibility 判定。
- `StartGateContractEvaluator`：纯 start-ready decision / reason 判定。
- `RuntimeZeroObserver` / `CalibrationRuntime`：runtime-zero eligibility 和 effective-zero
  计算的纯 helper。

仍留在 `LaserModule` 的部分也有合理原因：

- presence 变化后是否调用 `SystemStateMachine::onUserOff`。
- 每轮是否写回 `SystemStateMachine::setRuntimeReady`。
- baseline latch 后何时 refresh `RhythmStateJudge`。
- `syncStableContractBridge` 后何时写回 `SystemStateMachine::setStartReadiness`。
- confirmed leave 后 `releaseOccupiedCycle -> RhythmStateJudge::reset ->
  clearStableContractBridge -> setStartReadiness(false)` 的顺序。

这些是动作时序，不是纯计算；直接迁移会提高真机回归风险。

## 3. 已形成的正式计划

新增长期入口：

- `docs/system/esp32_laser_stable_baseline_presence_owner_refactor_plan.md`

计划里将后续工作分为：

- 低风险可推进：
  - `LSP-001 Stable Contract Logging Helper`
  - `LSP-002 Stable Window Metrics Helper Tests`
  - `LSP-003 Presence Counter Decision Wrapper`
  - `LSP-004 Baseline Writeback Evidence Builder`
- 中风险需单独决策：
  - stable candidate owner
  - baseline latch owner
  - presence owner state carrier
  - occupied-cycle owner
- 高风险冻结：
  - 迁移 `setRuntimeReady` / `setStartReadiness` / `onUserOff`
  - 改 `start_ready` / `baseline_ready` 对外含义
  - 改 confirmed leave clear 顺序
  - 把 motion shadow 直接接 runtime stop

## 4. 优先级总表同步

已同步：

- `docs/system/esp32_firmware_optimization_priority_table.md`

`FW-OPT-005` 状态更新为：

- diagnostics + calibration runtime 已完成。
- stable / baseline / presence 深拆已完成审计。
- 下一步只推荐先做 `LSP-001` / `LSP-002`，不默认进入 action timing 迁移。

## 5. 验证

本轮是文档审计包，运行验证：

- `git diff --check`

不需要真机测试：

- 未改固件运行代码。
- 未改 BLE 正式合同。
- 未改 measurement、start/stop、安全动作时序。

## 6. 下一步建议

如果继续让 ESP32 固件更符合成熟工程标准，建议下一包只做：

1. `LSP-001 Stable Contract Logging Helper`
2. `LSP-002 Stable Window Metrics Helper Tests`

这两个能继续降低 `LaserModule` 体积和回归风险，同时不会碰 `start_ready` /
`baseline_ready` / leave action timing。
