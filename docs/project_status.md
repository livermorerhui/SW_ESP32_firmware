# Project Status

## 2026-04-09 Phase 4 Current Progress: Low-Risk Power Pass Advanced

The current Phase 4 pass has moved beyond planning and already completed a
validated first batch of low-risk power-oriented changes on the current
ESP32-S3-only bench.

What is now considered completed in the current Phase 4 pass:

- BLE transmit power was reduced from the previous maximum setting to a
  moderate level.
- BLE advertising now uses a staged profile:
  - fast discovery immediately after boot/disconnect
  - lower-power idle advertising after the fast-discovery window expires
- idle low-power advertising also uses a lower advertising-only TX power tier
  than the fast-discovery window
- idle polling was reduced for `BASE` / no-laser delivery paths.
- idle repeated Modbus read attempts now use backoff when the sensor path is
  unavailable and the device is not running.
- idle backoff windows now avoid unnecessary short-period task wakeups.
- BLE control-task idle checks now relax further while disconnected and already
  in the idle low-power advertising profile.

What was observed on bench after these changes:

- BLE discovery still works.
- reconnect still works.
- `PLUS` degraded-start still works.
- `WAVE:SET / WAVE:START / WAVE:STOP` did not regress.
- repeated unavailable-sensor logging became materially less frequent in idle
  scenarios.
- advertising profile transitions were observed and reconnect remained
  possible after the low-power advertising profile became active.
- reconnect remained possible after idle advertising TX power was lowered to
  the current `N0` tier on the bench.

What this means:

- Phase 4 has started for real, not just on paper.
- the current pass already achieved practical low-risk power reductions without
  reopening protocol or transport risk.
- within the current bench boundary, additional low-risk Phase 4 gains are now
  entering diminishing-return territory.

What it does **not** mean:

- Phase 4 is fully complete
- deep/light sleep strategy is validated
- full-device current draw characterization is complete
- measurement-capable hardware power behavior is validated

## 2026-04-09 Phase 4 Low-Risk Power Pass Started

Phase 4 has now started as a low-risk power pass on top of the completed
Phase 3 efficiency baseline.

Current Phase 4 scope is intentionally narrow:

- reduce BLE transmit power to a moderate validated level
- relax advertising intervals without changing the BLE protocol contract
- reduce avoidable idle polling in delivery-subset firmware paths

Current Phase 4 scope does **not** include:

- aggressive sleep-state entry
- deep/light sleep policy changes that may perturb BLE stability
- large scheduling refactors
- full-system power characterization under unavailable hardware

## 2026-04-09 Phase 3 Low-Risk Efficiency Pass Completed

The current low-risk Phase 3 efficiency pass is now considered complete for the
current ESP32-S3-only bench boundary.

This completion claim does **not** mean the full measurement-capable product is
optimized. It means the currently deliverable subset has finished one bounded
round of:

- log-volume reduction
- low-risk string/path cleanup
- Demo APP background-noise cleanup
- no-regression verification on `BASE` and `PLUS` degraded-start

What was completed in this Phase 3 pass:

- `LaserModule` read-fail logging was reduced from repeated paired noise to
  periodic summary-style reporting.
- `BleTransport` stream suppression logging now reports suppression bursts
  instead of repetitive single-line spam.
- `main.cpp` ACK/NACK builders were tightened to reduce chained temporary
  `String` assembly.
- Demo APP no longer keeps test-session automation hot for the current delivery
  subset.
- `WaveModule` output-driver logging was reduced to key start/stop and ramp
  transitions.
- `SystemStateMachine` start-ready logging no longer reprints on tiny weight
  jitter.

What this completion means:

- the current delivery subset remains functionally stable
- the main runtime logs are materially quieter and easier to inspect
- Phase 3 no longer needs more broad low-risk cleanup before moving on

What it does **not** mean:

- real measurement throughput is optimized
- full measurement-capable product performance is closed
- Phase 4 power work is done

Current recommended next step:

- keep the current Phase 3 result frozen
- move to Phase 4 low-risk power optimization
- continue using `BASE` and `PLUS` degraded-start as the current regression
  boundary

## 2026-04-08 Current Mainline Status: Phase 2.5 Delivery Closure

Current mainline should now be read as five segments rather than the older
four-phase view:

1. Phase 1: BLE stability repair
2. Phase 2: transport/bootstrap truth hardening
3. Phase 2.5: `BASE` / `PLUS degraded-start` delivery closure
4. Phase 3: runtime efficiency optimization
5. Phase 4: power optimization

Current interpretation:

- Phase 1 is complete.
- Phase 2 core work is complete.
- active work should remain on Phase 2.5 delivery closure.
- Phase 3 and Phase 4 are not current priorities.

What is already considered stable on the current ESP32-S3-only bench:

- BLE connect / disconnect / reconnect
- TX notify + RX write transport path
- `CAP? -> ACK:CAP`
- `SNAPSHOT? -> SNAPSHOT`
- profile write + truth refresh
- `WAVE:SET / WAVE:START / WAVE:STOP`
- `BASE` immediate start path
- `PLUS + laser installed + measurement unavailable` degraded-start path

What is not yet a current completion claim:

- real laser measurement validation
- MAX485 real communication validation
- PCM5102A output-path validation
- sustained valid `EVT:STREAM` under real measurement load
- full calibration closed loop
- whole-device long-duration stability under full measurement conditions

Current delivery boundary:

- deliver a stable BLE-controlled subset for `BASE`
- deliver a stable BLE-controlled subset for `PLUS` degraded-start under the
  current bench constraint
- do not present the current repo status as a completed full-measurement product

Current document anchors:

- `docs/system/base_plus_degraded_delivery_plan.md`
- `docs/system/base_plus_degraded_regression_checklist.md`
- `docs/system/current_hardware_validation_boundary.md`
- `docs/ble-init-contract.md`
- `docs/start-readiness-contract.md`

Immediate next-step guidance:

- keep `ACK:CAP` / `SNAPSHOT` responsibilities frozen
- keep BLE init sequencing frozen
- continue removing Demo APP measurement-dependent assumptions from the current
  delivery subset
- expand focused regression only for `BASE` and `PLUS` degraded-start
- do not start broad Phase 3 / Phase 4 optimization work yet

## 2026-04-02 Freeze Decision: Phase 3 Script Experimental Line

Decision recorded on April 2, 2026:

- current mainline = Phase 2 stable firmware baseline + SW repo alignment development
- current experimental line = Phase 3 firmware-owned script offload
- Phase 3 is frozen as an experimental/history line and is not the current mainline

Frozen scope:

- firmware-owned script offload
- heterogeneous step-list execution
- `SCRIPT:SET script_steps=...`
- `SCRIPT:START/STOP/ABORT` continued Phase 3 expansion
- WP3a / WP3b / WP3c / WP3d follow-up development
- Demo APP script runner as a current mainline feature

Current repo interpretation:

- active source should continue on the non-script Phase 2 path
- historical Phase 3 reports are kept as experiment records only
- SW normal development must not depend on any Phase 3 script surface unless the line is explicitly reopened under separate conditions

## 2026-04-02 Phase 2 WP2 Behavior Migration Landed

Phase 2 WP2 behavior migration is now in place on top of the WP1 skeleton.

What changed:

- `runtime zero` now participates in `effective zero`
- `effective zero` now locks for an occupied cycle to prevent in-cycle zero switching
- stable enter / live / exit now run on the explicit internal stable contract path
- `start_ready` is now contract-computed and no longer a direct baseline latch mirror

What is still pending:

- real-device tuning of runtime-zero thresholds
- real-device tuning of stable exit sensitivity
- any external exposure of the new internal contract layers

Current interpretation:

- Phase 1 remains the regression baseline
- Phase 2 behavior migration has started for real, not just structurally
- next work should prioritize hardware validation and threshold tuning, not another structure pass

## 2026-04-02 Phase 2 WP1 Skeleton Landed

Phase 2 WP1 skeleton implementation is now in place on top of the validated Phase 1 baseline.

What landed in firmware:

- formal dual-zero data model:
  - `calibration zero`
  - `runtime zero`
  - `effective zero`
- centralized Phase 2 threshold landing zone in `src/config/LaserPhase2Config.h`
- explicit internal stable contract layers:
  - `user_present`
  - `stable_candidate`
  - `stable_ready_live`
  - `baseline_ready_latched`
  - `start_ready`

What did not land yet:

- full dual-zero behavior migration
- full stable enter/exit migration
- full `start_ready` semantic decoupling from `baseline_ready`
- external snapshot/protocol expansion for the new internal layers

Current interpretation:

- Phase 1 remains the regression baseline
- Phase 2 has started with WP1 structural scaffolding completed
- current outward behavior is still intentionally bridge-preserved while WP2/WP3 migrate behavior on top of the new skeleton

## 2026-04-02 Phase 1 Exit Decision

Phase 1 exit is now `Passed` after the on-site validation pass completed on April 2, 2026.

Confirmed exit decision:

- `Phase 1 Exit Decision = Passed`
- `Can Proceed to Phase 2 = Yes`

What was validated in the accepted pass:

- no stable weight: the Demo APP start button stayed gray and non-clickable
- stable weight established: the start button turned green quickly and became clickable
- Demo APP interaction behavior matched the intended Phase 1 contract

Current stage:

- Phase 1 is closed
- Phase 2 can start immediately

Next stage focus:

- proceed with Phase 2 work on top of the validated firmware + Demo APP interaction baseline
- keep Phase 1 validation behavior as the regression baseline for later changes

## 2026-04-01 Phase 1 Real-Device Validation Status

Historical note: this 2026-04-01 pending status was superseded by the 2026-04-02 Phase 1 exit pass.

Phase 1 exit remained `Pending` after the 2026-04-01 real-device BLE + serial validation pass.

What was confirmed:

- the attached bench device is reachable over BLE as `SonicWave_Hub`
- `CAP?`, `SNAPSHOT?`, `WAVE:SET`, `WAVE:START`, and `WAVE:STOP` all execute on real hardware
- reconnect preserves `top_state` on the device side

What blocked exit:

- the attached real device did **not** expose `SNAPSHOT.wave_output_active`
- the attached real device did **not** emit `EVT:WAVE_OUTPUT active=<0|1>`
- the attached bench unit reported `platform_model=BASE` and `laser_installed=0`, so measurement-plane validation could not be completed
- no Android Demo APP runtime session was executed in this environment, so UI/logging/export retention remain unvalidated

Highest-confidence interpretation:

- the checked-in source and protocol tests are ahead of the flashed bench firmware image
- Phase 1 source closure therefore exists, but bench integration closure does not yet

Required next steps before Phase 2 at that time:

- flash the exact current firmware image that exports the control-confirmation contract
- rerun validation on a measurement-capable `laser_installed=1` bench
- run Android Demo APP real-device validation for reconnect/UI/logging/retention

Artifacts:

- `reports/tasks/phase1_real_device_validation/`

## 2026-03-21 Task-MOTION-EXPORT-AUTOMATION 状态

本任务在 Demo APP 的 Motion-Safety Sampling Tool 中落地了“导出会话自动化”MVP，目标是把采样标签、自动命名和 JSON metadata 写入标准化，降低高频采样阶段的人工整理成本。

本次完成：

- `导出会话` 现在会先弹出采样标签表单，而不是直接导出
- 表单支持：
  - 主标签：`NORMAL_USE` / `FALL_DURING_USE`
  - 细分类标签：`NORMAL_VIBRATION`、`LEAVE_PLATFORM`、`PARTIAL_LEAVE`、`FALL_ON_PLATFORM`、`FALL_OFF_PLATFORM`、`LEFT_RIGHT_SWAY`、`SQUAT_STAND`、`RAPID_UNLOAD`、`OTHER_DISTURBANCE`
  - 可选备注
- CSV / JSON 自动共用统一基础文件名：
  - `<主标签>_<细分类>_<频率>hz_<强度>_<时间戳>`
- JSON metadata 自动新增：
  - `primaryLabel`
  - `subLabel`
  - `notes`
  - `frequencyHz`
  - `intensity`
  - `exportedAt`
- 原有兼容字段继续保留：
  - `scenarioLabel`
  - `scenarioCategory`
  - `waveFrequencyHz`
  - `waveIntensity`
  - `exportTimestampMs`

明确未改动：

- 固件运行时逻辑
- leave / fall 判定逻辑
- sampling mode 行为
- stop / pause 执行逻辑
- motion-safety 原始 session row 采样结构

当前价值：

- 导出后不再需要人工重命名
- CSV / JSON 命名一致，便于成对交接
- 样本标签不再依赖人工记忆
- 后续脚本可直接读取标签和上下文做离线分析

验证完成：

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
- `tools/android_demo ./gradlew :sonicwave-protocol:test`

## 2026-03-13 Task-4A Status

Task-4A firmware safety alignment is implemented at the firmware layer.

Completed on the firmware side:

- explicit reason vocabulary for:
  - `USER_LEFT_PLATFORM`
  - `FALL_SUSPECTED`
  - `BLE_DISCONNECTED`
  - `MEASUREMENT_UNAVAILABLE`
- policy-controlled stop behavior for BLE disconnect and measurement availability
- pause-style vs abnormal-stop-style signaling through `EVT:SAFETY`
- compatibility preservation for `EVT:STATE` and `EVT:FAULT`

Known remaining work outside firmware:

- SW APP Task-4 should consume `EVT:SAFETY`
- product UX should map:
  - `RECOVERABLE_PAUSE` -> paused
  - `ABNORMAL_STOP` -> abnormal stop
  - `WARNING_ONLY` -> non-blocking warning
- bench validation is still required with hardware
- Task-4A also re-enabled laser safety triggers by setting `DIAG_DISABLE_LASER_SAFETY=0` in the aligned build

## 2026-03-14 Task-Demo-Align Status

Task-Demo-Align updated the engineering demo in `tools/android_demo` so it stays aligned with the Task-4A firmware safety baseline without becoming a product app.

Completed in this task:

- kept the baseline engineering surface intact:
  - connect / disconnect
  - wave control
  - live distance / weight telemetry
  - stable weight visibility
- extended the local protocol/SDK stack to decode `EVT:SAFETY`
- exposed safety reason / effect / runtime state / wave state in the demo system-status panel
- added a transport-derived `BLE_DISCONNECTED` engineering signal so reconnect-needed meaning remains visible when BLE loss is the primary observable event
- verified that unknown protocol events still do not break baseline parsing

Explicitly left outside demo scope:

- SW APP product UX and training-state strategy
- auto-adjust / manual override product control logic
- customer attribution workflow UI
- CH341-first product assumptions

## 2026-03-18 Laser Audit-1 Status

Laser Audit-1 traced the live laser measurement path end-to-end and confirmed a direct semantics regression in the current worktree.

Confirmed in this task:

- the Modbus register is parsed as signed `int16_t`
- runtime distance continues to use `signed_raw / 100.0`
- negative values are part of the expected working model in repo artifacts:
  - default `zero=-22.0`
  - protocol example `EVT:STREAM:-22.58,7.42`
  - calibration command examples with negative zero distance
- the current failing validity gate was introduced later and was wrong in two ways:
  - it assumed a positive-only `65..135` operating window
  - it compared that window directly against raw fixed-point register values, creating a `100x` scale mismatch
- `32767` is now handled as an explicit over-range sentinel before generic range checks
- bounded diagnostics now log:
  - raw unsigned register value
  - signed parsed value
  - scaled runtime value
  - sentinel flag
  - final validity reason

Expected runtime effect after the fix:

- signed in-range negative and positive samples can again reach weight, stream, and stable logic
- `32767` no longer appears as a normal high measurement
- app-visible distance / weight / stable recovery is expected once hardware produces valid samples

Artifacts:

- `reports/laser_audit_1/`
- `docs/system/laser_measurement_semantics.md`

## 2026-03-18 Task-C Demo Chinese Semantic Enhancement Status

Task-C refined the Android demo engineering console so core status signals are easier to understand in Chinese while keeping the raw engineering identity visible.

Completed in this task:

- System Status now shows Chinese primary meaning for:
  - 状态
  - 故障
  - 安全原因
  - 影响
  - 运行态
  - 波形态
- raw enum/code remains visible as secondary text for debugging
- telemetry section now includes concise Chinese reading guidance
- calibration tools now include concise Chinese usage guidance and current stable-weight visibility
- official Chinese docs were added for:
  - Demo APP signal meaning
  - calibration usage guidance

Intentionally preserved as engineering-facing:

- raw enum/code names
- fault code / safety code visibility
- raw device log console
- model coefficient visibility
- engineering wording around stream / stable / capture / model comparison

Artifacts:

- `reports/task_c_demo_chinese_semantics/`
- `docs/system/demo_app_signal_meaning_zh.md`
- `docs/system/demo_app_calibration_guide_zh.md`

## 2026-03-19 Task-C.1 Calibration Tool Interaction Refinement Status

Task-C.1 refined the Android demo calibration tool in `tools/android_demo/app-demo` so the recording workflow, point visibility, and linear-vs-quadratic comparison are understandable at a glance while preserving the engineering-console identity.

Completed in this task:

- `记录校准点` is now gated by recording state and is disabled until recording starts
- the calibration card now makes the intended sequence explicit:
  - 归零
  - 开始/停止录制
  - 记录校准点
  - 模型比较
  - 最终校准/写入模型
- successful `ACK:CAL_POINT` events now accumulate into an on-screen calibration-point dataset
- the calibration tool now shows:
  - recorded point count
  - latest point summary
  - persistent point list
- engineering tools are visible by default instead of hidden behind a toggle
- a new scatter plot overlays:
  - recorded calibration points
  - linear fitted curve
  - quadratic fitted curve
- the current comparison target is visually highlighted and accompanied by lightweight error/monotonicity summary
- Chinese helper text now explains:
  - 获取模型
  - 写入模型
  - CAL:ZERO
  - 模型参考值
  - c0 / c1 / c2

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c1_calibration_tool_refinement/`
- `docs/system/demo_app_calibration_model_comparison_zh.md`

## 2026-03-19 Audit-CAL-OWNERSHIP Status

Audit-CAL-OWNERSHIP traced the real calibration ownership split between the Android demo and firmware and localized why `记录校准点` could be clicked while the visible point count remained `0`.

Confirmed in this task:

- `记录校准点` does not create a local point immediately in the Demo APP
- the app sends `CAL:CAPTURE w=<ref>` and only appends a point after firmware returns `ACK:CAL_POINT`
- firmware validates the current measurement and stability state in `LaserModule::captureCalibrationPoint(...)`, but does not keep a persistent list of calibration points
- the visible point list, point count, scatter plot, and local fit curves are all driven from the app-side `UiState.calibrationPoints`
- linear and quadratic fitting are computed locally in `tools/android_demo/sonicwave-protocol`
- `获取模型 / 写入模型 / CAL:ZERO` are firmware-facing operations

Highest-confidence root cause for `已记录校准点：0`:

- the current UI count/table/chart were already bound to `Event.CalibrationPoint`
- when the count stayed `0`, the more likely failure was no successful `ACK:CAL_POINT`
- firmware capture preconditions such as `NOT_STABLE` or `INVALID_SAMPLE` could reject the capture
- the demo previously did not surface capture failure close enough to the capture controls, so the workflow looked like a silent UI bug

Minimal fix applied in this audit:

- added bounded `[CAL_AUDIT]` logs around capture click, command emit, ACK/NACK handling, and point append
- added a persistent capture-status message in the calibration section so rejected captures now explain why no point was appended

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/audit_calibration_ownership/`
- `docs/system/calibration_recording_ownership.md`

## 2026-03-19 Audit-FIX-C4c-2 Write Model UI Binding Status

Audit-FIX-C4c-2 localized the remaining `写入模型` visibility bug to the final Android demo render/state-binding hop after the protocol ACK chain had already been proven healthy.

Confirmed in this task:

- the button text, button color, and result card are all intended to read from `UiState.writeModelStatus`
- the `ACK:CAL_SET_MODEL` success path was already reaching `Event.CalibrationSetModelResult`
- the ViewModel was logging:
  - `writeModel ack success`
  - `writeResult state=SUCCESS visible=true`
- but the success/failure event handlers were still calling `appendSystemLog(...)` from inside `_uiState.update { ... }`
- because `appendSystemLog(...)` itself appends into `rawLogLines` through another `_uiState.update`, the write-result state update could retry and preserve the older pending value
- the minimal fix was to remove write-result side effects from those `_uiState.update` lambdas and update `writeModelStatus` as a pure state transition before logging

Expected runtime effect after the fix:

- once `ACK:CAL_SET_MODEL` success arrives, the button no longer stays on `正在写入模型`
- button text, button color, and result summary now share the same final `writeModelStatus` source of truth
- failure closure remains intact through the same no-nested-update path

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/audit_fix_c4c2_write_ui_binding/`

## 2026-03-19 Audit-ESP32-CAL-BOUNDARY Status

Audit-ESP32-CAL-BOUNDARY reviewed whether ESP32 firmware currently owns more calibration responsibility than intended and compared the current implementation against the target split where Demo APP owns workflow/fitting and firmware owns runtime execution.

Confirmed in this task:

- Demo APP already owns the primary calibration workflow surface:
  - start / stop recording
  - visible point list
  - scatter plot
  - linear / quadratic fitting comparison
  - model selection
- ESP32 firmware does not store a reusable calibration point set and does not compute linear or quadratic fits
- ESP32 firmware does own:
  - runtime measurement acquisition
  - runtime stable-weight generation
  - capture-time sample validation
  - deployed model storage
  - deployed model evaluation during runtime weight computation
- the main boundary ambiguity is not firmware-side fitting logic, but the semantics of `CAL:CAPTURE`, where firmware acts as a stable-snapshot validator and acceptance gate

Highest-confidence boundary mismatches:

- protocol wording can make firmware look like the owner of “record point workflow”, even though it only validates and returns one accepted snapshot
- legacy / newer calibration commands overlap:
  - `SCALE:CAL`
  - `CAL:SET_MODEL`
  - `SCALE:ZERO`
  - `CAL:ZERO`
- `NOT_STABLE` capture rejection is runtime-integrity-aware, but it also affects app-owned workflow clarity and must be explained better on the app side

Recommended minimal correction direction:

- keep firmware as the validator and runtime model executor
- keep Demo APP as the authoritative owner of the calibration session dataset and fitting comparison
- avoid adding any firmware point-set storage, fitting, recommendation, or model-comparison logic
- in future cleanup, simplify command semantics so firmware-facing commands read more clearly as snapshot / deploy operations instead of workflow ownership

Minimal fix applied in this audit:

- no firmware code change was applied
- the audit found boundary ambiguity and protocol overlap, but no isolated firmware overreach bug that required a safe immediate patch

Artifacts:

- `reports/audit_esp32_calibration_boundary/`
- `docs/system/esp32_calibration_boundary.md`

## 2026-03-19 Task-C.2 Calibration Capture Feedback and Workflow Clarity Status

Task-C.2 refined the Demo APP calibration capture area so the user can tell, near the record action itself, whether capture succeeded, why capture failed, and which visible preconditions are currently satisfied.

Completed in this task:

- the capture area now shows a localized result card for:
  - pending capture
  - capture success
  - capture failure
- known firmware failure reasons are now mapped into concise Chinese guidance, while the raw device reason can still remain visible as secondary engineering context
- the capture area now shows a compact precondition checklist for:
  - recording started
  - device connected
  - stable state visible in UI
  - valid reference weight
  - live measurement available
- the capture workflow now includes a compact summary of:
  - latest capture result
  - current capture availability
- helper text now clarifies the semantics of:
  - 记录校准点
  - 获取模型
  - 写入模型
  - CAL:ZERO
  - 校准
- the wording explicitly preserves the audited boundary:
  - APP requests capture and owns the point list
  - firmware validates the stable sample
  - the point is appended to the APP dataset only after success

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c2_capture_feedback/`

## 2026-03-19 Design-CAL-STABLE Status

Design-CAL-STABLE defined the next-stage stable-weight rule and the target app-authoritative calibration capture workflow without forcing an immediate broad code migration.

Design conclusions from this task:

- stable weight should remain a firmware-owned runtime-quality concept
- stable weight should continue to support:
  - runtime display
  - runtime quality judgment
  - platform-state / safety logic
  - optional calibration guidance
- stable weight should no longer remain a hard blocker for every calibration point capture
- Demo APP should become the authoritative owner of calibration-point recording:
  - clicking `记录校准点` should directly create a local point
  - that point should at minimum contain the entered reference weight and the current live distance snapshot
  - stable weight may remain optional context, not mandatory gate
- ESP32 should remain focused on:
  - live measurement acquisition
  - live validity checks
  - stable-weight generation
  - deployed-model storage and runtime evaluation
  - platform-state and safety logic

Recommended next-stage direction:

- keep live stream / stable events / model write-back flow intact
- move point capture ownership fully into the Demo APP
- treat current `CAL:CAPTURE` as a transitional or optional engineering validation path rather than the primary point-recording path
- parameterize and document stable-weight rules so tiny micro-fluctuations do not make stability practically unreachable

Implementation status:

- design and documentation only
- no runtime code was changed in this task

Artifacts:

- `reports/design_cal_stable_and_app_capture/`
- `docs/system/stable_weight_definition.md`

## 2026-03-19 Task-C.3 Status

Task-C.3 implemented the app-authoritative calibration capture path in the Demo APP.

Implementation outcomes:

- clicking `记录校准点` now appends a local calibration point immediately from current app-side live state
- the primary capture route is now `APP_LIVE_SNAPSHOT`
- point visibility no longer depends on firmware `ACK:CAL_POINT`
- point count, point table, scatter plot, and fitting input now update from the same local append path
- stable weight remains visible in the UI and is still stored as optional point metadata when available
- stable weight is no longer a hard blocker for local point capture
- linear / quadratic comparison remains app-local
- model read / write flow remains firmware-facing and unchanged

Compatibility notes:

- legacy `CAL:CAPTURE` parsing remains in the app as a secondary engineering route
- `ACK:CAL_POINT` no longer owns the main point-list growth path
- firmware remains focused on live measurement, stable-weight generation, deployed-model runtime execution, and safety logic

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c3_app_capture/`

## 2026-03-19 Task-C.4 Status

Task-C.4 simplified the Demo APP calibration UI into a clearer model-driven workflow.

Main workflow changes:

- the main visible path is now:
  - zero
  - start / stop recording
  - capture points
  - review points and curves
  - select model
  - write model
- model selection now automatically prepares deployable parameters for the selected fit
- `写入模型` is now the clear primary final action in the main flow

Legacy / engineering changes:

- low-level engineering commands are moved behind a collapsed Advanced Engineering section
- manual `ref/c0/c1/c2` editing remains available only as an advanced override path
- legacy `Z/K + 校准` path is no longer mixed into the main flow
- `获取模型` remains available for device readback, but it no longer competes visually with the main calibration path

Structural notes:

- a model-option abstraction now drives selection state, availability state, and prepared state
- this keeps the current `LINEAR` / `QUADRATIC` flow modular for future model additions

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c4_calibration_ui_simplification/`

## 2026-03-19 Task-C.4a Status

Task-C.4a added visible success/failure feedback for `写入模型`.

Implementation outcomes:

- writing a model now shows visible pending / success / failure feedback near the main write button
- success and failure are visually distinguished by color and result card state
- feedback can indicate which model was written when available
- write-model semantics remain unchanged: it still deploys the currently selected/prepared model

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c4a_write_feedback/`

## 2026-03-19 Audit-FALL-1 Status

Audit-FALL-1 identified the current firmware source of truth for `FALL_SUSPECTED`.

Key conclusions:

- current fall suspicion is triggered in `LaserModule`, not by a multi-signal safety policy layer
- the trigger is currently a single-frame derived-weight rate check:
  - only while `TopState::RUNNING`
  - only against `FALL_DW_DT_SUSPECT_TH = 25.0 kg/s`
- `USER_LEFT_PLATFORM` is handled separately through weight hysteresis:
  - presence enter at `> 5.0 kg`
  - leave at `< 3.0 kg`
- normal vibration can be misclassified because fall uses a one-frame weight-rate spike while leave-platform uses a different absolute-threshold path

Recommended minimal correction direction:

- keep the existing leave-platform path
- keep the existing fault/effect mapping
- add short confirmation/debounce to fall suspicion instead of relying on a single frame

Implementation status:

- audit and documentation only
- no firmware code fix applied in this step

Artifacts:

- `reports/audit_fall_logic/`

## 2026-03-19 Task-C.4b Status

Task-C.4b closed the model-write pending state with an explicit firmware completion signal and polished the sampling-to-deployment transition.

Implementation outcomes:

- firmware `CAL:SET_MODEL` now emits explicit protocol completion lines:
  - success: `ACK:CAL_SET_MODEL ...`
  - failure: `NACK:CAL_SET_MODEL ...`
- Demo APP now parses that explicit write result and uses it as the primary completion signal for `写入模型`
- the write button/result card now exits orange pending reliably on explicit success or failure instead of depending on generic observation
- the main calibration flow now reads more naturally from sampling into deployment:
  - start recording
  - capture points
  - review points/curves
  - stop recording
  - select model
  - write model

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Verification not completed in this environment:

- firmware local build, because neither `platformio` nor `pio` is available in the shell

Artifacts:

- `reports/task_c4b_write_ack_and_stop_flow/`

## 2026-03-19 Audit-C4b-VERIFY Status

Audit-C4b-VERIFY verified the write-model ACK chain in code from firmware emit to Demo APP UI feedback.

Audit conclusions:

- the code path is complete end-to-end:
  - APP sends `CAL:SET_MODEL`
  - firmware applies the model
  - firmware emits explicit `ACK:CAL_SET_MODEL ...` or `NACK:CAL_SET_MODEL ...`
  - the Demo APP parser recognizes that dedicated result
  - `DemoViewModel` closes pending on that event
  - the UI binds button color and result card directly to `writeModelStatus`
- there is no source-level format mismatch between the current firmware emission and parser expectation
- the protocol layer also has dedicated tests for both explicit success and explicit failure decoding

Highest-confidence interpretation of the previously observed “orange forever” symptom:

- the runtime device likely was not yet running the explicit-ACK firmware build when the user observed the issue
- as checked in source, the current code now supports the full success-return chain

Implementation status:

- audit and documentation only
- no additional code fix was required in this verification step

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/audit_write_model_ack_chain/`

## 2026-03-19 Audit-C4b-2 Status

Audit-C4b-2 isolated the firmware-internal success log versus the actual BLE/protocol return path for `SET_MODEL`.

Audit conclusions:

- `[CAL] SET_MODEL result=success ...` is generated by a serial log call in the firmware command handler
- that serial line is not itself the BLE response
- the BLE/protocol success return is a separate path:
  - firmware sets `outAck = "ACK:CAL_SET_MODEL ..."`
  - `BleTransport` dispatches the command
  - `enqueueTxLine(ack)` queues the response
  - `sendLineNow()` calls `pTx->notify()` with the framed line
- the exact success payload length is well below the firmware TX buffer limit, so the current checked-in success line is not being truncated by the `char line[128]` queue buffer
- on the APP side, `incomingLines` is the raw BLE line stream, `rawLines` exposes it directly for logging, and the parser has an exact `ACK:CAL_SET_MODEL` match branch

Highest-confidence interpretation:

- in the current source, `SET_MODEL` success is not serial-only; it is also sent over the BLE/protocol path
- if a runtime device still leaves the APP orange, the most likely remaining causes are:
  - runtime firmware/app version mismatch
  - notify/connection state failure at runtime, which would prevent `pTx->notify()` delivery even though serial logs still appear locally

Implementation status:

- audit and documentation only
- no additional code fix applied in this step

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/audit_c4b2_set_model_ble_return/`

## 2026-03-19 Task-C.4c Status

Task-C.4c improved write-model result visibility and reduced default log noise in the Demo APP.

Implementation outcomes:

- write success/failure is now surfaced more prominently in the apply area:
  - a sticky status summary now appears above the main `写入模型` button
  - the status title explicitly reads pending / success / failure
  - pending button text now changes to a dedicated writing state
- audit of overwrite paths found no normal stream-driven reset of `writeModelStatus`; the main problem was visibility, not the protocol chain
- high-frequency stream logs are now suppressed by default in the APP raw console:
  - repeated `EVT:STREAM:...` RX lines are hidden
  - `RX-RAW` chunks are hidden unless verbose stream logging is enabled
- verbose stream logging remains available behind an explicit Advanced Engineering toggle

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c4c_write_ui_and_log_noise/`

## 2026-03-20 Task-C.4e Status

Task-C.4e rolls back the Task-C.4d local-context direction and restores Step 3 as one coherent point-capture workflow area.

Implementation outcomes:

- confirmed the Demo APP source-of-truth module under `tools/android_demo/app-demo/`
- removed the Task-C.4d compact `录点前上下文` / local context block from the capture section
- reordered Step 3 to read top-to-bottom as:
  - `录点条件`
  - `录点摘要`
  - `参考重量输入框`
  - `实时距离`
  - `记录校准点`
- kept the original global live distance / live weight / stable weight summary near the top of the calibration card unchanged
- preserved capture behavior:
  - `onCapturePoint` and `canCaptureCalibrationPoint` remain unchanged
  - point count / point table / scatter plot behavior remain unchanged
  - fitting and model write-back remain unchanged

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_c4e_capture_layout_correction/`

## 2026-03-20 Design-MOTION-SAFETY Status

Design-MOTION-SAFETY defines the next-stage motion safety framework without rewriting firmware thresholds yet.

Implementation outcomes:

- confirmed the firmware source of truth in:
  - `src/modules/laser/LaserModule.*`
  - `src/core/SystemStateMachine.*`
- confirmed the Demo APP source of truth in:
  - `tools/android_demo/app-demo/`
- documented the current baseline explicitly:
  - leave-platform currently uses derived-weight hysteresis and a falling-edge `onUserOff()` trigger
  - fall suspicion currently uses a single-frame derived-weight rate during `RUNNING`
  - `SystemStateMachine` keeps the public runtime contract centered on `EVT:STATE`, `EVT:FAULT`, and `EVT:SAFETY`
- defined a compatibility-preserving motion safety framework with:
  - internal motion safety states
  - modular leave detection architecture
  - modular fall detection architecture
  - parameterized firmware landing model
- defined Demo APP debug sampling as the front end for:
  - continuous time-series capture
  - labeling and markers
  - chart/table review
  - CSV + JSON export
- documented that direct measurement truth is distance / displacement, while weight is model-derived and still useful for runtime safety analysis
- documented a staged path:
  - extend debug sampling first
  - collect and label real motion data
  - derive compact runtime parameters
  - then land modular leave/fall logic in firmware

Implementation status:

- design and documentation only
- no runtime threshold rewrite applied in this task
- no firmware or Demo APP behavior changed in this task

Artifacts:

- `reports/design_motion_safety_framework/`
- `docs/system/motion_safety_framework.md`

## 2026-03-20 Task-MOTION-1 Status

Task-MOTION-1 delivers the first usable Demo APP Motion-Safety Sampling Tool MVP for engineering data capture.

Implementation outcomes:

- confirmed the Demo APP source-of-truth module under:
  - `tools/android_demo/app-demo/`
- kept firmware runtime responsibilities unchanged:
  - live measurement streaming stays in firmware
  - runtime state output stays in firmware
  - safety output stays in firmware
  - no leave/fall runtime redesign was applied in this task
- added a dedicated engineering-facing `Motion-Safety Sampling Tool` section to the Demo APP main screen
- added explicit session controls:
  - `开始采样`
  - `停止采样`
  - `清空会话`
  - `导出会话`
- added an in-memory motion sampling session model with:
  - unique `sessionId`
  - `startedAtMs`
  - `endedAtMs`
  - app/device/protocol/model metadata snapshot
  - structured per-row motion samples
- recorded structured time-series rows from the live stream with:
  - `timestampMs`
  - `elapsedMs`
  - `distanceMm`
  - `liveWeightKg`
  - nullable `stableWeightKg`
  - `measurementValid`
  - `stableVisible`
  - `runtimeStateCode`
  - `waveStateCode`
  - `safetyStateCode`
  - `safetyReasonCode`
  - `safetyCode`
  - `connectionStateCode`
  - nullable model / marker / future motion-safety fields
  - nullable `ddDt`
  - nullable `dwDt`
- added recorded-session review UI with:
  - current live summary
  - session summary
  - recent row preview
  - last recorded values
- added basic recorded-session charts:
  - distance vs time
  - live weight vs time
- added export support:
  - CSV as the primary row export
  - JSON sidecar for session metadata / schema / extensibility hints
  - Android Downloads output under `Downloads/SonicWave/`
- added bounded diagnostics:
  - session started
  - periodic row-count logging every 50 rows
  - session stopped
  - export destination

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_motion_1_sampling_mvp/`
- `docs/system/motion_sampling_tool.md`

## 2026-03-20 Task-MOTION-1A Status

Task-MOTION-1A adds a temporary engineering sampling mode so fall detections stay visible during motion-safety data collection without immediately stopping waveform output.

Implementation outcomes:

- confirmed the current runtime split:
  - `src/modules/laser/LaserModule.cpp` still computes leave/fall triggers
  - `src/core/SystemStateMachine.cpp` still owns pause/stop action decisions
- added an explicit firmware/runtime engineering mode control:
  - primary protocol command `DEBUG:MOTION_SAMPLING enabled=0|1`
  - firmware capability reporting now exposes:
    - `motion_sampling_mode`
    - `fall_action_suppressed`
- kept normal mode behavior unchanged:
  - `FALL_SUSPECTED` still follows the original abnormal-stop path when the mode is off
- added sampling-mode fall-only suppression:
  - fall detection is still evaluated
  - fall observability is still emitted through fault/safety events and serial diagnostics
  - final fall-triggered stop/pause action is suppressed only while sampling mode is enabled
- kept leave-platform behavior unchanged:
  - `USER_LEFT_PLATFORM` still uses the original pause/stop path
  - no leave suppression was added in this task
- added bounded firmware diagnostics:
  - `[MOTION_SAMPLE_MODE] enabled=true|false`
  - `[FALL] detected but action suppressed dueToSamplingMode=true`
- added Demo APP engineering visibility/control inside the motion-sampling tool:
  - enable sampling mode
  - disable sampling mode
  - explicit on-screen state label showing whether fall stop suppression is active
  - capability parsing updates so the app can reflect firmware mode state

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Verification not completed in this environment:

- `pio run`
  - `pio` is not installed in the current shell environment

Artifacts:

- `reports/task_motion_1a_sampling_fall_suppression/`

## 2026-03-20 Audit-MOTION-1C Status

Audit-MOTION-1C verified and fixed the leave-platform action closure bug.

Implementation outcomes:

- confirmed that leave detection itself was already working:
  - `LaserModule` was raising `USER_LEFT_PLATFORM`
  - firmware was emitting `RECOVERABLE_PAUSE`
  - the Demo APP was receiving the leave safety event
- identified the closure bug in `SystemStateMachine`:
  - `enterRecoverablePause(...)` latched the pause reason and emitted visibility
  - but `syncReadyState()` intentionally refused to leave `RUNNING`
  - so the system could remain `RUNNING` / `wave=RUNNING` until a later external `WAVE:STOP`
- applied the minimal reliable fix:
  - when `RECOVERABLE_PAUSE` is entered while the state machine is currently `RUNNING`, it now reuses the existing internal `requestStop()` path immediately
  - this closes the running waveform path automatically instead of waiting for manual stop input
- preserved leave semantics:
  - `USER_LEFT_PLATFORM` still uses the recoverable-pause policy
  - leave detection thresholds and detection logic were not changed
- preserved sampling-mode behavior:
  - sampling mode still suppresses fall-triggered stop only
  - no leave suppression was introduced
- added bounded closure diagnostics:
  - `[LEAVE] confirmed action=RECOVERABLE_PAUSE`
  - `[FSM] RECOVERABLE_PAUSE closing running path source=...`

Verification completed:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Verification not completed in this environment:

- `pio run`
  - `pio` is not installed in the current shell environment

Artifacts:

- `reports/audit_motion_1c_leave_action_closure/`

## 2026-03-20 Task-WAVE-UI-1 Status

Task-WAVE-UI-1 implements the lightweight fixed bottom wave-control bar for the Demo APP engineering screen.

Implementation outcomes:

- confirmed the previous wave controls lived inside the scrollable `WaveSection`, which forced repeated upward scrolling during testing
- added a reusable `WaveControlBottomBar` component and mounted it as the `Scaffold` bottom bar
- moved the high-frequency wave controls out of the scroll area and into a fixed two-row layout:
  - Row 1:
    - frequency input
    - `20`
    - `30`
    - `40`
    - `START`
  - Row 2:
    - intensity input
    - `60`
    - `80`
    - `100`
    - `STOP`
- kept the main page content scrollable above the fixed bottom bar
- preserved manual input and added quick-fill helpers:
  - frequency presets update the frequency input
  - intensity presets update the intensity input
- refined button-state clarity:
  - start uses green when enabled
  - stop uses red when enabled
  - disabled states fall back to neutral gray
  - start is disabled when disconnected, already running, or current wave values are invalid
  - stop is enabled only while running
- added a lightweight current-state hint above the bar:
  - `待启动 / Ready to start`
  - `运行中 / Running`
  - `未连接 / Disconnected`
  - `故障停止 / Fault stop`
  - invalid-parameter or recoverable-pause hints when relevant
- preserved command semantics:
  - start still uses the existing `WAVE:SET` + `WAVE:START` path
  - stop still uses the existing `WAVE:STOP` path
- added bounded preset diagnostics:
  - `[WAVE_UI] preset frequency=...`
  - `[WAVE_UI] preset intensity=...`

Verification completed:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_wave_ui_1_fixed_bottom_bar/`

## 2026-03-20 Task-WAVE-UI-1A Status

Task-WAVE-UI-1A applies a small visual/layout polish pass to the fixed bottom wave-control bar without changing its existing interaction behavior.

Implementation outcomes:

- kept the existing fixed-bottom two-row structure intact
- preserved the current state hint behavior and connection-dependent enable/disable logic
- reduced the visual dominance of the frequency and intensity input fields:
  - narrowed their relative row weight
  - reduced their perceived height and label emphasis
  - kept both fields fully editable for manual values such as `25 Hz` or `75`
- rebalanced row spacing so the presets and action buttons have more breathing room and the bar feels less left-heavy
- strengthened preset selected-state visibility:
  - selected frequency presets now use a stronger orange background
  - selected intensity presets now use the same orange background
  - unselected presets remain visually distinct
- preserved the existing start/stop semantics:
  - start still follows the current wave start command path
  - stop still follows the current wave stop command path
- preserved the existing status explanation behavior, including messages such as:
  - `不可启动，用户离开平台`

Verification completed:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Artifacts:

- `reports/task_wave_ui_1a_bar_polish/`

## 2026-03-20 Task-MOTION-1B Status

Task-MOTION-1B refines the Motion-Safety Sampling Tool export flow so captured sessions are easier to label, trace, and protect from accidental loss.

Implementation outcomes:

- export now requires an explicit operator scenario choice instead of auto-guessing
- the export dialog provides:
  - `静止站立`
  - `正常律动`
  - `离开平台`
  - `异常动作-快速减载`
  - `异常动作-左右摇摆`
  - `异常动作-下蹲站起`
  - `异常动作-半离台`
  - `自定义`
- custom scenario text is required when `自定义` is selected
- motion-sampling sessions now snapshot additional context at session start:
  - wave frequency
  - wave intensity
  - sampling-mode flag
  - whether wave output was already running at session start
  - model type and coefficients
- exported JSON metadata now includes:
  - `scenarioLabel`
  - `scenarioCategory`
  - `waveFrequencyHz`
  - `waveIntensity`
  - `samplingModeEnabled`
  - `waveWasRunningAtSessionStart`
  - `modelType`
  - `modelCoefficients`
  - `exportTimestampMs`
  - `originalSessionId`
- export filenames now follow:
  - `<场景>_<频率>hz_<强度>_<YYYYMMDDHHMM>.csv`
  - `<场景>_<频率>hz_<强度>_<YYYYMMDDHHMM>.json`
- the action row is now clearer:
  - export is green and visually primary
  - clear is red and visually destructive
- stopped-but-unexported sessions now show an export recommendation
- clearing an unexported session now requires explicit confirmation
- sampling collection behavior remains unchanged:
  - start/stop unchanged
  - row recording unchanged
  - charts unchanged
  - runtime leave/fall logic unchanged

Verification completed:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Verification not completed in this environment:

- on-device export dialog flow
- actual file creation check under `Downloads/SonicWave/`

Artifacts:

- `reports/task_motion_1b_session_export_polish/`

## 2026-03-20 Task-STATE-UI-1 Status

Task-STATE-UI-1 cleans up the Demo APP system-status hierarchy so the presentation better matches the fields that are most trustworthy during real runtime use.

Implementation outcomes:

- promoted the primary interpretation layer to:
  - `状态`
  - `安全原因`
  - `影响`
- made `状态` the most prominent status card in the section
- kept `安全原因` and `影响` visually prominent as the main explanation layer for why the system is in its current condition
- demoted `运行态` into a lower-priority engineering reference row
- removed `波形态` from equal top-level prominence and kept it only in the lower-priority engineering reference row
- removed the previous equal-weight competition between:
  - 状态
  - 故障
  - 安全原因
  - 影响
  - 运行态
  - 波形态
- preserved engineering details below the primary layer:
  - 故障参考
  - 故障码
  - 工程含义
  - 信号来源
  - 安全码
- added small copy/layout grouping:
  - `当前主状态`
  - `工程参考状态`
- preserved underlying protocol/firmware semantics

Verification completed:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Verification not completed in this environment:

- on-device visual validation across ready/running/leave/fall states

Artifacts:

- `reports/task_state_ui_1_hierarchy_cleanup/`

## 2026-03-21 Task-MOTION-ANALYSIS-1 Status

Task-MOTION-ANALYSIS-1 adds an in-app moving-average research overlay to the Demo APP Motion-Safety Sampling Tool while keeping runtime safety logic unchanged.

Implementation outcomes:

- confirmed the current Demo APP source-of-truth module under:
  - `tools/android_demo/app-demo/`
- kept implementation local to the motion-sampling review surface:
  - `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/MotionSamplingSection.kt`
  - localized strings
  - motion-sampling docs/report artifacts
- added one shared moving-average point-count control in the motion-sampling section:
  - default `5`
  - presets `3 / 5 / 7`
  - valid range `1..50`
  - blank/invalid text preserves the last valid applied value
- derived moving-average data only from captured session rows already stored in memory
- kept the captured row model unchanged:
  - no session schema expansion
  - no export format change
  - no mutation of raw captured rows
- extended the captured-session chart so it now overlays:
  - raw distance
  - distance MA
  - raw live weight
  - live-weight MA
- kept raw curves visible and made the MA overlay visually distinct through line weight/opacity instead of replacing the existing chart
- extended the `Last recorded values` block with explicit latest `MA(n)` summaries for:
  - distance
  - live weight
- added concise research-only guidance in the motion-sampling section so the overlay is not confused with current runtime safety behavior
- intentionally excluded `stableWeightKg` from the MA overlay MVP:
  - it remains visible in the raw latest-value summary
  - it is not overlaid because it is nullable and only meaningful while stable visibility is active
- preserved runtime ownership and behavior:
  - `src/modules/laser/LaserModule.cpp` remains the leave/fall trigger source
  - `src/core/SystemStateMachine.cpp` remains the stop/pause policy owner
  - no leave/fall/sampling-mode runtime logic was changed in this task

Verification completed:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
- `tools/android_demo ./gradlew :sonicwave-protocol:test`

Verification not completed in this environment:

- on-device visual confirmation of MA controls and overlay readability during a real captured session

Artifacts:

- `reports/task_motion_analysis_1_ma_overlay/`
