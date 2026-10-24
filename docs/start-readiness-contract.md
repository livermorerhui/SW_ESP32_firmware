# Start Readiness Contract

- 当前维护入口：[ESP32、Demo APP 与工厂交付维护说明](system/esp32_demo_release_maintenance.md)。
- 当前合同：ESP32 独立判断，APP 只读取并渲染。SW APP 本轮不修改。
- 2026-10-25 文档复盘：下列当前合同覆盖历史审计中的旧产品语义；历史审计只保留当时的 Owner、抽取范围和验证记录，不作为当前源码完成性证明。

## 当前用户路线与真相源

| 顺序 | 用户操作 | APP 行为 | ESP32 事实 |
|---|---|---|---|
| 1 | 连接设备 | 清空旧连接状态，读取当前设备配置和运行状态 | `ACK:CAP` 是能力/身份引导；配置 GET 确认型号、传感器、generation；`SNAPSHOT.start_ready` 是正式开始依据 |
| 2 | 选择型号或激光型号 | 只修改待设置值；同型号不能重复提交 | 未收到设置命令时，设备不变 |
| 3 | 点击设置 | SET 后等待 GET 确认，再刷新当前连接的运行状态 | 持久化、配置 generation 和 Reader 状态按正式 Owner 生效 |
| 4 | 确认 Base/Plus | Base 隐藏、Plus 显示 3 张卡片；激光型号确认后才设置空载距离 | APP 不以临时选项、SET ACK 或旧 Snapshot 冒充成功 |
| 5 | Plus 激光未设置或故障 | 保护开关灰色，显示一次激光故障通知；“知道了”只关通知 | ESP32 独立判定自动降级并发布 `start_ready` |
| 6 | 点击开始 | 按当前 `start_ready` 和 APP 操作状态显示按钮，发送开始动作 | `SystemStateMachine` 保持最终启停 Owner，不接收 APP 对 ready 的赋值或降级授权 |

服务器不参与这条本地 BLE 设置/就绪链。两种授权必须区分：本合同取消的是“激光降级确认授权”，不是把历史分期授权设计引入当前固件。

## 当前 ESP32 就绪目标

以下是用户已确认的目标，不代表所有传感器已完成验收：

| 设备状态 | ESP32 应发布的开始状态 |
|---|---|
| 硬阻断、BLE 断连、配置切换中或有效配置不能确认 | `start_ready=false` |
| Base，配置完成且无上述阻断 | `start_ready=true` |
| Plus 未设置激光（`sensor_profile=NONE`） | 自动降级，不等待 APP 授权或站稳；无上述阻断时 `true` |
| Plus 激光故障已确认 | 自动降级，不等待空载距离或站稳；无上述阻断时 `true` |
| Plus 激光正常，两个保护都关闭 | 不等待站稳；无上述阻断时 `true` |
| Plus 激光正常，任意保护开启 | 按固件保护合同，站稳后才允许 `true` |

`laser_installed` 是历史硬件能力兼容字段，不能代替 `sensor_profile` 判断是否设置激光。`baseline_ready` 是独立基线事实，不能由 APP 转换成 ready。`degraded_start_available/enabled` 仅作兼容或诊断，不能作为 APP 授权入口；旧 `DEGRADED_START_SET` 可被解析和回复，但不得改变固件 ready。

“硬阻断”指固件的阻止输出故障、故障停止或恢复窗口、配置无法确认等；激光未设置/测量异常本身不等于这种阻断。APP 对断连、配置刷新、命令发送中或非法参数的禁用，是操作生命周期控制，不是设备 ready 的第二份真相。

## 当前实现、差异与验收边界

审计源码为 `b2ce08e`；核心修正来自 `3617b81`。目标合同与当前差异分别记录：

| 项目 | 当前源码事实 | 边界 |
|---|---|---|
| APP 开始状态 | `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/UiModels.kt` 的 `waveStartAvailability()` 只以 `deviceStartReady` 作为设备就绪依据；`WaveControlStateReducer.kt` 接受新的 false | UI 操作锁不能被描述成 APP 修改 ESP32 ready |
| 故障通知 | `DemoViewModel.kt` 的 Snapshot 分支与 `dismissLaserFaultDialog()`；`MainScreen.kt` 的单按钮通知 | 不发送授权；不能把一次通知作为降级资格 |
| 卡片等待阶段 | `DemoViewModel.kt` 的 `sendProfileConfig()` 清除 `deviceConfigVerified`；`UiModels.kt` 的 `shouldShowPlusConfigurationCards()` 同时检查该值 | 当前 SET 等待会隐藏卡片，尚不能保证“回读成功前保持原卡片” |
| CYD 保护策略 | `src/modules/laser/LaserModule.cpp` 的 `protectedStartPolicyAvailable()` 只开放 J100；`src/core/SystemStateMachine.cpp` 的 `effectiveStartReady()` 保留未验收策略门禁 | CYD 距离/空载距离可用不等于保护启动已验收；不得倒改通用目标或擅自开放 |
| 旧固件兼容 | `DemoViewModel.kt` 的 `sendProfileConfig()` 有 `proto < 2` 分支，但身份、generation、verified 等检查先执行 | 不能据此宣布所有旧固件可设置，需完整链路证据 |
| 历史验证 | 历史用户确认通过，专项业务观测有 PASS | 采集完整性为 INCOMPLETE；不扩展为当前 1.1.3 或完整场景验收 |

完整差异、测试入口和证据索引见[统一蓝图第 15 节](system/esp32_demo_start_ready_independent_judgment施工蓝图.md#15-2026-10-25-复盘状态与源码差异登记)。本轮只更新文档，未重新构建、Run 或进行设备测试。

## 后续修改复核

- Base、Plus 均只消费当前连接的正式 `start_ready`；不能把 Base 型号或无激光推算为本地 true。
- 选择、SET ACK、配置 GET、运行 Snapshot 必须分开；旧 epoch/旧配置结果不得覆盖新状态。
- 故障通知必须绑定已确认 Plus 的故障进入状态；Plus 转 Base 不沿用通知，重复故障帧不重复弹窗。
- 保护开关分别设置；激光不可用时禁止操作，不要求先关闭开关才能自动降级。
- 主机测试、构建、用户操作、业务日志、采集完整性和发布包身份分别报告。

## 历史记录适用范围

以下为 2026-04-27～28 的历史重构记录，保留当时动作顺序和测试证据。历史“Base 直接就绪”“仅非 Base 使用 start_ready”“依赖降级确认”等语义均不能覆盖上面的当前合同。历史测试命令和结果不自动覆盖当前 `main` 或正式 `esp32s3_unified` 发布包；当前构建入口见维护说明。

## 历史审计：StableContract / StartGate Owner Freeze

2026-04-27 audit result:

- `LaserModule` owns `StableContractState`.
- `SystemStateMachine` owns final `requestStart` allow/reject.
- APP / Demo APP consume firmware truth through `SNAPSHOT.start_ready`,
  `SNAPSHOT.baseline_ready`, and `EVT:BASELINE`; they must not re-derive
  ESP32 start readiness from local UI state.

Frozen internal meanings:

- `runtime_ready` means presence / user-present evidence.
- `start_ready` is the formal start gate for laser-equipped non-BASE profiles.
- `baseline_ready` is baseline evidence, not the only start/continue owner.
- `WAVE:START` must keep returning `NACK:NOT_ARMED` for unmet readiness and
  `NACK:FAULT_LOCKED` for blocking faults.

Safe next refactor cut:

- Extract only the pure start-gate decision from `syncStartReadyContract`.
- Keep `StableContractState`, baseline latch/clear, `setStartReadiness` timing,
  leave clear, rhythm reset, and effective-zero lock/unlock in `LaserModule`
  until a separate contract audit approves moving them.

Implementation note:

- `StartGateContractEvaluator` now owns only the pure decision and reason
  calculation.
- `LaserModule` still owns state write-back and when to call
  `SystemStateMachine::setStartReadiness`.
- This extraction does not change `SNAPSHOT`, `EVT:BASELINE`, or `WAVE:START`
  behavior.

Diagnostics note:

- `START_GATE_DIAG` is a serial-only observability line.
- It reports the current evaluator reason and a low-frequency reason-count
  window for `measurement_invalid`, `user_not_present`, `baseline_not_ready`,
  `live_stable_not_ready`, `running_contract_hold`, and `idle_contract_ready`.
- It must not become an APP / Demo APP protocol dependency.
- It does not change `start_ready`, `baseline_ready`, BLE wire format, or
  `WAVE:START` ACK/NACK behavior.

## 历史审计：Baseline / Presence Owner Freeze

2026-04-27 audit result:

- `LaserModule` still owns the timing relationship between presence, stable
  latch, baseline latch, occupied-cycle cleanup, and start-readiness write-back.
- `SystemStateMachine` still owns `runtime_ready`, `start_ready`, leave action,
  and final `WAVE:START` allow/reject.
- `RhythmStateJudge` mirrors accepted baseline evidence for motion safety
  evaluation only; it must not become a waveform stop/start action owner.

Frozen internal meanings:

- `userPresent` is occupied/user-on-platform evidence.
- `runtime_ready` mirrors presence only and must not be used as the formal
  laser-equipped start gate by APPs.
- `stableReadyLive` is live stable-window evidence.
- `baselineReadyLatched` is durable baseline truth for the current occupied
  cycle.
- `start_ready` is derived from measurement health, presence, and durable
  baseline truth.

Do not move these actions in the first baseline/presence extraction:

- `SystemStateMachine::onUserOff`
- `SystemStateMachine::setRuntimeReady`
- `SystemStateMachine::setStartReadiness`
- occupied-cycle `lock/release`
- effective-zero lock/unlock
- `RhythmStateJudge::refreshBaselineFromStable`
- `RhythmStateJudge::reset`
- baseline clear ordering after confirmed leave

Safe next refactor cut:

- Extract presence/baseline pure decision helpers only.
- Return next state, changed flag, and reason.
- Keep action timing and all BLE protocol output in the current owners until a
  separate action-owner audit approves moving them.

Implementation note:

- `PresenceContractEvaluator` owns only the pure presence decision:
  `nextUserPresent`, `changed`, and `reason`.
- `BaselineEvidenceEvaluator` owns only the pure stable-window and baseline
  eligibility decision: window readiness, threshold pass/fail, next confirm
  count, and baseline eligibility.
- `LaserModule` still owns enter/exit counters, `invalidPresenceSamples`,
  candidate entry/reset, stable latch execution, occupied-cycle lock/release,
  `SystemStateMachine::onUserOff`, `SystemStateMachine::setRuntimeReady`, and
  all downstream action timing.

Validation note:

- `python3 tools/run_evaluator_unit_tests.py` covers the host-side pure
  evaluator contract without hardware.
- `python3 -m platformio run -e esp32s3` must still pass after evaluator
  changes.

## 历史审计：BaselineContract Diagnostics Freeze

2026-04-27 diagnostic implementation:

- `BASELINE_CONTRACT_DIAG_ENABLED` gates serial-only baseline contract
  diagnostics.
- `[BASELINE_CONTRACT] event=latch` records where baseline truth was latched:
  `stable_primary` or `rhythm_bridge`.
- `[BASELINE_CONTRACT] event=clear` records why durable baseline/start-ready
  bridge state was cleared, including the previous bridge state and weights.
- `[BASELINE_CONTRACT] event=start_ready_writeback` records each meaningful
  `SystemStateMachine::setStartReadiness` write-back transition, including
  source, top state, final reason, and weight.

Frozen boundaries:

- These diagnostics are serial evidence only.
- They must not become APP / Demo APP / backend protocol dependencies.
- They do not change `SNAPSHOT.start_ready`, `SNAPSHOT.baseline_ready`,
  `EVT:BASELINE`, `EVT:STREAM`, or `WAVE:START` ACK/NACK behavior.
- `LaserModule` still owns baseline latch/clear timing, start-ready write-back
  timing, occupied-cycle cleanup, and rhythm reset timing.
- `SystemStateMachine` still owns final start allow/reject behavior.

## 历史审计：Baseline Action Timing Freeze

2026-04-28 audit result:

- Baseline action timing is not a pure decision boundary. It currently spans
  `LaserModule`, `SystemStateMachine`, `RhythmStateJudge`, and `DualZeroState`.
- `LaserModule` remains the owner of action ordering for presence enter, stable
  latch, baseline latch, start-ready write-back, confirmed-leave clear,
  calibration/config/zero clear, and invalid-measurement recovery.
- `SystemStateMachine` remains the final owner of `runtime_ready`,
  `start_ready`, state transition, leave action, fault/pause handling, and
  `WAVE:START` allow/reject.
- `RhythmStateJudge` remains an evidence mirror for accepted baseline and
  motion-safety evaluation only.

Frozen action order:

- Presence enter locks effective-zero before stable/baseline latch.
- Stable latch updates `StableContractState` before
  `RhythmStateJudge::refreshBaselineFromStable`.
- `SystemStateMachine::setStartReadiness` is written back from
  `LaserModule::taskLoop` after `syncStableContractBridge`.
- Manual stop must not clear durable baseline while the user is still present.
- Confirmed leave clears occupied-cycle state, resets `RhythmStateJudge`, clears
  stable contract bridge, then writes `start_ready=false`.
- Calibration, device config, and zero changes remain strong clear paths and
  must not wait for the next normal measurement lifecycle.
- Invalid measurement is not the same as confirmed leave and must not directly
  own baseline clear.

Do not move in the next refactor cut:

- `SystemStateMachine::setStartReadiness`
- `SystemStateMachine::setRuntimeReady`
- `SystemStateMachine::onUserOff`
- `lockEffectiveZeroForOccupiedCycle`
- `releaseOccupiedCycle`
- `clearStableContractBridge`
- `RhythmStateJudge::reset`
- `RhythmStateJudge::refreshBaselineFromStable`

Safe next cut:

- Add read-only action evidence/snapshot helpers if needed.
- Centralize reason construction and before/after evidence logging.
- Keep all action execution timing in `LaserModule` until a later audit proves
  it can be moved without changing start/stop/leave behavior.

Implementation note:

- `BaselineActionStateSnapshot` and `BaselineActionWritebackEvidence` centralize
  the serial diagnostic evidence used by `[BASELINE_CONTRACT]`.
- They are read-only evidence structs. They do not execute actions, own state,
  or change call timing.
- `LaserModule` still invokes `SystemStateMachine::setStartReadiness`,
  `clearStableContractBridge`, `releaseOccupiedCycle`, and `RhythmStateJudge`
  methods at the same call sites.
- 2026-04-28 minimum device smoke confirmed the evidence path still emits
  `latch`, `start_ready_writeback`, and `clear` events, while normal
  connect/start/stop/leave behavior remains intact.
