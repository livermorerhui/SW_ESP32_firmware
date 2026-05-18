# ESP32 Laser Stable / Baseline / Presence Owner Refactor Plan

最后更新时间：2026-05-18

## 1. 结论

本文件是 `LaserModule` stable / baseline / presence 后续重构的固定计划入口。

当前结论：这一块不能按普通“继续瘦身”直接拆。`LaserModule` 已经有
`PresenceContractEvaluator`、`BaselineEvidenceEvaluator`、`StartGateContractEvaluator`
等纯决策 owner，但动作时序仍跨 `LaserModule`、`SystemStateMachine`、
`RhythmStateJudge` 和 `DualZeroState`。后续只能先做日志、evidence、纯 helper
抽取；凡是迁移动作时机的方案，都必须单独开合同审计和真机 capture。

本计划默认冻结：

- `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*` 线格式。
- `SNAPSHOT.start_ready`、`SNAPSHOT.baseline_ready` 和 `EVT:BASELINE` 消费语义。
- `SystemStateMachine` 对 `runtime_ready`、`start_ready`、最终 start / stop /
  safety action 的 owner 身份。
- `WaveModule` ramp / I2S 输出时序。
- Android APP 消费逻辑。

## 2. 当前 owner 边界

| Owner | 当前职责 | 可继续依赖的事实 |
| --- | --- | --- |
| `LaserModule` | measurement loop、presence 计数、stable candidate/latch、baseline latch/clear、start-ready writeback 调用时机、occupied-cycle lock/release | 是 stable / baseline / presence 动作编排 owner |
| `PresenceContractEvaluator` | 根据阈值和确认计数输出 `nextUserPresent / changed / reason` | 只负责纯 presence 决策，不执行 `onUserOff` 或 runtime writeback |
| `BaselineEvidenceEvaluator` | 根据 stable window 指标输出 stable/baseline eligibility 和 confirm count | 只负责纯窗口判定，不 latch baseline |
| `StartGateContractEvaluator` | 根据 measurement/presence/baseline/top state 输出 start-ready 决策和 reason | 只负责纯 start gate 判定，不调用 `setStartReadiness` |
| `SystemStateMachine` | `runtime_ready`、`start_ready`、`requestStart` allow/reject、leave/fall/safety action | 最终动作和对外状态 owner |
| `RhythmStateJudge` | 接收 accepted baseline evidence，输出 baseline-main / rhythm evidence | evidence mirror，不是 final stop/start owner |
| `DualZeroState` / runtime-zero helpers | occupied-cycle effective-zero lock、runtime-zero observation 和 effective-zero 选择 | zero lock/release 时机仍由 `LaserModule` 调用 |

## 3. 不能自动迁移的动作

以下动作不能在普通结构优化包里迁移到新 owner：

- `SystemStateMachine::setRuntimeReady`
- `SystemStateMachine::setStartReadiness`
- `SystemStateMachine::onUserOff`
- `lockEffectiveZeroForOccupiedCycle`
- `releaseOccupiedCycle`
- `clearStableContractBridge`
- `RhythmStateJudge::refreshBaselineFromStable`
- `RhythmStateJudge::reset`
- baseline latch / clear 的相对顺序
- confirmed leave 后的 occupied-cycle cleanup 和 start-ready clear 顺序
- invalid measurement 时对 stable、presence、start-ready 的降级路径

原因：这些动作共同决定 `WAVE:START` 是否允许、离台是否触发保护、baseline
是否仍可用于恢复、以及 runtime-zero 是否被锁在当前 occupied cycle。单独迁移一个动作
可能不改编译结果，但会改变现场时序。

## 4. 可安全推进的小包

### LSP-001 Stable Contract Logging Helper

类型：低风险内部抽取

可做内容：

- 把 `BaselineActionStateSnapshot`、`BaselineActionWritebackEvidence` 和
  `[BASELINE_CONTRACT]` 日志格式抽到只读 helper。
- `LaserModule` 仍在原 call site 调用 helper。

禁止内容：

- 不移动 `clearStableContractBridge`。
- 不移动 `sm->setStartReadiness`。
- 不改日志字段名和 BLE 输出。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

### LSP-002 Stable Window Metrics Helper Tests

类型：低风险 host-side 测试补强

可做内容：

- 给 stable window metrics、trimmed mean、baseline eligibility 增加 focused
  host-side tests。
- 只锁现有行为，不调整阈值和算法。

禁止内容：

- 不改 stable latch 阈值。
- 不改 stable build interval。
- 不改 baselineReady latch 条件。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`

### LSP-003 Presence Counter Decision Wrapper

类型：低到中风险内部整理

可做内容：

- 将 enter / exit confirm count 的更新和 `PresenceContractEvaluator` 输入组装
  收口成一个窄 helper。
- helper 返回 next counters、presence decision、reason。
- `LaserModule` 保留 `sm->onUserOff`、`sm->setRuntimeReady` 调用。

禁止内容：

- 不让 helper 调用 `SystemStateMachine`。
- 不改变 `invalidPresenceSamples` 退出逻辑。
- 不改变 RUNNING 下 invalid presence 的保护行为。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`
- 如触碰 invalid presence 行为，必须准备真机 capture。

### LSP-004 Baseline Writeback Evidence Builder

类型：低风险内部抽取

可做内容：

- 将 start-ready writeback evidence 的 before/after snapshot 和节流判断抽成纯 helper。
- 保留 `LaserModule::logStartReadyWriteback` 的触发点。

禁止内容：

- 不改变 writeback source / reason 文本。
- 不改变 `BASELINE_CONTRACT_DIAG_ENABLED` 语义。
- 不让 helper 执行 `setStartReadiness`。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

## 5. 中风险候选包

以下事项只能在低风险小包完成后单独决策：

| 候选 | 风险 | 前置条件 |
| --- | --- | --- |
| Stable candidate owner | 可能改变 candidate enter/reset 和 stable latch 相对时序 | 先补 stable window focused tests 和 capture checklist |
| Baseline latch owner | 可能改变 `baselineReadyLatched` 与 `RhythmStateJudge` refresh 顺序 | 先固定 latch/clear golden evidence |
| Presence owner state carrier | 可能改变 `onUserOff`、`runtime_ready`、invalid presence 关系 | 先补 presence transition tests 和真机 leave capture |
| Occupied-cycle owner | 可能改变 effective-zero lock/release 和 baseline clear 时机 | 先审 runtime-zero / calibration / leave 三条链路 |

## 6. 高风险冻结项

以下事项不进入自动重构：

- 把 `setRuntimeReady` / `setStartReadiness` 移出 `LaserModule` call site。
- 把 `onUserOff` 移到 presence helper。
- 改 `start_ready` 与 `baseline_ready` 的对外含义。
- 改 confirmed leave 后的 clear 顺序。
- 把 motion shadow 或 baseline-main evidence 直接升级成 runtime stop action。
- 修改 APP / Demo APP 对 `SNAPSHOT`、`EVT:BASELINE`、`EVT:STOP` 的消费合同。

## 7. 后续推荐顺序

1. 先做 `LSP-001 Stable Contract Logging Helper`。
2. 再做 `LSP-002 Stable Window Metrics Helper Tests`。
3. 如仍需要瘦身，再评估 `LSP-003 Presence Counter Decision Wrapper`。
4. 中风险候选包必须先补审计和真机 capture 计划，再决定是否实现。

本计划不是要求立即实现全部项；它用于防止后续把 action timing 当成普通 helper
拆分。
