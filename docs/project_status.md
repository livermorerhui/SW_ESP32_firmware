# Project Status

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
