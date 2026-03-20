# Development Journal

## 2026-03-13 - Task-4A Firmware Safety Alignment

Summary:

- audited the existing firmware safety/runtime path
- confirmed the main baseline gaps were:
  - BLE disconnect aliased into user-off
  - user leave modeled as blocking fault instead of recoverable pause
  - no explicit protocol signal for warning-only vs pause-like vs abnormal-stop-like behavior
- implemented a minimal alignment patch that preserves the existing FSM shape and BLE framing

Implementation notes:

- added centralized safety policy switches in `src/config/GlobalConfig.h`
- re-enabled laser safety triggers in the aligned build by setting `DIAG_DISABLE_LASER_SAFETY=0`
- normalized reason names in `src/core/Types.h`
- added `EVT:SAFETY` in `src/core/ProtocolCodec.h`
- updated BLE session handling so connect and disconnect are explicit FSM inputs
- kept `EVT:FAULT` for legacy compatibility

Verification:

- `~/.platformio/penv/bin/pio run` passed

Follow-up:

- validate user-leave and fall scenarios on hardware with the aligned build

## 2026-03-14 - Task-Demo-Align Demo / SDK Capability Alignment

Summary:

- audited the actual demo layout in `tools/android_demo`
- confirmed the demo depends on its local `sonicwave-protocol`, `sonicwave-sdk`, and `sonicwave-transport` modules rather than direct text parsing in UI code
- confirmed baseline connect / wave / stream / stable-weight flows already worked against Task-4A firmware because unknown events were ignored safely
- identified the primary gap as missing `EVT:SAFETY` visibility in the protocol and UI surface

Implementation notes:

- added reusable `Event.Safety`, `SafetyEffect`, and `WaveState` types to the local protocol model
- updated `ProtocolCodec` and tests to decode `EVT:SAFETY` while continuing to ignore unknown future events
- extended the demo status panel to show safety reason, effect, runtime state, wave state, engineering meaning, and signal source
- added transport-derived `BLE_DISCONNECTED` visibility for reconnect-needed debugging when disconnect happens before any safety line is observed
- kept auto-adjust and attribution out of the demo because the current demo architecture does not already expose the SW APP product controller or usage-data path

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Task-C.4c Write UI Visibility / Log Noise Reduction

Summary:

- strengthened the visibility of `写入模型` success/failure in the calibration apply area without changing the write-model protocol chain
- reduced default raw-console noise by suppressing high-frequency stream logs unless explicitly enabled for deep debugging

Implementation notes:

- `writeModelStatus` was audited and is already sticky across normal stream updates
- no normal measurement-stream overwrite path was found that immediately clears success/failure after ACK
- the UI fix therefore focused on stronger visibility:
  - write-result summary is now shown directly above the main write button
  - result headline now clearly reads pending / success / failure
  - the button label changes to a dedicated pending state while writing
- added explicit diagnostics when the write result becomes visible in UI state:
  - `[CAL_UI] writeResult state=SUCCESS visible=true ...`
  - `[CAL_UI] writeResult state=FAILURE visible=true ...`
- added `verboseStreamLogsEnabled` to the APP UI state
- default raw-console behavior now suppresses:
  - repeated `EVT:STREAM:...`
  - `RX-RAW` chunks
- verbose stream logging can still be enabled intentionally from the Advanced Engineering area

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Audit-C4b-2 SET_MODEL BLE Return Path Verification

Summary:

- audited the exact boundary between firmware-local `SET_MODEL` success logging and the BLE/protocol response path back to the APP
- confirmed that the serial success line and the BLE success return are separate steps in the same command-success branch
- confirmed that the current explicit success payload is short enough to fit inside the firmware TX queue buffer without truncation

Audit notes:

- firmware success logging is done with `Serial.printf("[CAL] SET_MODEL result=success ...")`
- the actual BLE/protocol reply is carried by `outAck`
- after command dispatch, `BleTransport` unconditionally enqueues `ack` and later calls `pTx->notify()` in `sendLineNow()`
- the exact current success payload format is:
  - `ACK:CAL_SET_MODEL type=<TYPE> ref=<REF> c0=<C0> c1=<C1> c2=<C2>`
- the exact failure payload format is:
  - `NACK:CAL_SET_MODEL type=<TYPE> reason=<REASON>`
- current success payload length is ~79 chars in the observed representative case, below the `TxMsg.line[128]` queue limit
- on Android:
  - BLE inbound lines enter `BluetoothGattTransport.incomingLines`
  - `SonicWaveClient.rawLines` exposes the same raw stream
  - `ProtocolCodec.decode()` has a dedicated `ACK:CAL_SET_MODEL` / `NACK:CAL_SET_MODEL` branch

Conclusion:

- the current source does send `SET_MODEL` success over BLE/protocol; it is not serial-only
- if runtime behavior still shows indefinite orange pending, the most likely remaining explanations are:
  - device not running the updated firmware image
  - BLE notify delivery not actually active at runtime despite local serial success

Implementation status:

- audit only
- no additional code change required in this step

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Audit-C4b-VERIFY Write-Model ACK Chain Verification

Summary:

- audited the current source-level chain for `写入模型` from APP send through firmware apply, explicit result emission, parser decode, ViewModel closure, and UI feedback binding
- verified that the current code now contains a complete explicit ACK/NACK success path for model deployment
- did not find a current source mismatch between firmware emitted format and Demo APP parser expectations

Audit notes:

- firmware `CAL:SET_MODEL` now emits:
  - `ACK:CAL_SET_MODEL type=... ref=... c0=... c1=... c2=...`
  - `NACK:CAL_SET_MODEL type=... reason=...`
- the protocol parser checks `parseCalibrationSetModelResult(...)` before generic `parseNack(...)` / `parseAck(...)`, so the dedicated model-write result is not shadowed by generic parsing
- `DemoViewModel` enters pending when the write command is sent, and closes pending on:
  - `Event.CalibrationSetModelResult(success = true)` -> success
  - `Event.CalibrationSetModelResult(success = false)` -> failure
- the calibration UI button color and result card both read directly from `writeModelStatus`, so a successful state transition should visibly turn the workflow green
- dedicated protocol tests already cover both explicit success and explicit failure decode cases

Conclusion:

- the current checked-in code supports the full end-to-end success-return path
- if a user still sees indefinite orange pending on device, the highest-confidence explanation is firmware/app runtime version mismatch rather than a still-broken source chain

Implementation status:

- audit only
- no additional code fix applied in this step

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Task-C.4b Write ACK Completion / Stop Flow Polish

Summary:

- added an explicit firmware-side completion signal for `CAL:SET_MODEL` so write-model success is no longer inferred from generic behavior alone
- updated the Demo APP protocol parser and write-result state machine to close pending from a dedicated write-model result event
- moved `停止录制` into the natural end of the sampling phase so the main workflow reads more clearly before model deployment

Implementation notes:

- firmware `main.cpp` now returns:
  - `ACK:CAL_SET_MODEL type=... ref=... c0=... c1=... c2=...` on success
  - `NACK:CAL_SET_MODEL type=... reason=...` on failure
- protocol-layer decoding now maps those lines to a dedicated `Event.CalibrationSetModelResult`
- `DemoViewModel` now prioritizes that explicit result to close `writeModelStatus`:
  - pending -> success on explicit ACK
  - pending -> failure on explicit NACK
  - generic ACK/NACK fallback remains for compatibility with older firmware
- calibration UI now presents the main flow as:
  - zero
  - start recording
  - capture points
  - review points / curves
  - stop recording
  - select model
  - write model
- write is still not hard-blocked by recording state, but the UI now clearly recommends stopping recording before deployment

Documentation notes:

- updated the calibration guide and model-comparison notes with explicit write ACK/NACK behavior and the new placement of `停止录制`
- added the full Task-C.4b report pack under `reports/task_c4b_write_ack_and_stop_flow/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

Verification not completed:

- firmware local build, because `platformio` / `pio` is not installed in this shell

## 2026-03-19 - Audit-FALL-1 Current Fall Detection Logic

Summary:

- audited the current firmware fall detection path end to end
- confirmed that `FALL_SUSPECTED` is currently triggered from `LaserModule`, then mapped by `SystemStateMachine` into `ABNORMAL_STOP`
- confirmed that the current trigger is much simpler than a multi-signal fall classifier

Audit notes:

- current fall trigger uses:
  - model-derived runtime weight
  - previous weight
  - elapsed time between loop samples
  - `RUNNING` state guard
- current formula is effectively:
  - `abs(weight - lastWeight) / dt > 25.0 kg/s`
- current loop cadence is roughly 200 ms, so a single step of a bit more than 5 kg is already enough to cross threshold
- no debounce window or multi-sample confirmation is present
- `USER_LEFT_PLATFORM` is classified separately using hysteresis thresholds (`> 5 kg` enter, `< 3 kg` leave)
- this makes normal vibration false positives plausible:
  - user remains present
  - leave-platform does not trigger
  - one derived-weight spike can still trigger fall

Correction notes:

- no code fix applied in this audit step
- recommended minimal direction is to add short confirmation/debounce for fall suspicion instead of changing the whole safety architecture

## 2026-03-19 - Task-C.4a Model Write Success Feedback

Summary:

- added visible result feedback for `写入模型` so the final deployment step no longer feels silent or uncertain
- kept the current selected/prepared-model write logic unchanged

Implementation notes:

- introduced dedicated write-model feedback UI state:
  - pending
  - success
  - failure
- `sendCalibrationSetModel()` now marks the write as pending before waiting for device response
- `ACK` turns the write result into visible success feedback
- `NACK` and transport/error paths turn it into visible failure feedback
- the result card appears next to the main write button and the button color also reflects the latest result state
- bounded diagnostics were added for:
  - write requested
  - write success
  - write failure

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Task-C.4 Calibration UI Simplification / Model-Driven Apply

Summary:

- simplified the calibration card so the normal path reads as one coherent workflow instead of a mixed toolbox
- made model selection the primary deploy-preparation interaction
- demoted legacy and low-level controls into a collapsed advanced engineering area

Implementation notes:

- introduced model-driven deploy preparation state in the app UI state:
  - selected model
  - prepared model
  - model options with selected/available/prepared flags
- selecting `线性` or `二次` now immediately prepares the corresponding fitted coefficients when that fit is available
- main write flow now consumes the prepared model state instead of expecting the user to hand-copy coefficients
- point table / scatter / fit summary remain unchanged in purpose and continue to provide the visual basis for the model choice
- advanced engineering remains available for:
  - `获取模型`
  - `CAL:ZERO`
  - manual `ref/c0/c1/c2` override
  - legacy `Z/K + 校准`
- fetched device model is kept as readback/reference state instead of overriding the main selected-fit workflow automatically

UI notes:

- selected model state now has stronger visual emphasis
- the prepared model summary is shown in the main flow
- the main final action is now clearly `写入模型`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-18 - Laser Audit-1 Signed Semantics / Sentinel Audit

Summary:

- audited the live laser measurement chain from Modbus register read through stream / stable emission
- compared the current worktree against the older baseline path to separate the new regression from longstanding behavior
- confirmed the current failing gate was introduced in local uncommitted changes rather than the original baseline
- confirmed repo evidence favors a signed displacement-style model instead of a positive-only absolute-distance model

Implementation notes:

- traced the raw path in `src/modules/laser/LaserModule.cpp`:
  - `readInputRegisters(REG_DISTANCE, 1)`
  - `uint16_t rawRegister`
  - `int16_t signedDistanceRaw`
  - `scaledDistance = signedDistanceRaw / 100.0f`
- confirmed the previous gate used `65..135` constants directly against the raw fixed-point register value
- replaced that gate with a signed raw validity window inferred from the observed device display range:
  - `LASER_VALID_MEASUREMENT_MIN_RAW = -3570`
  - `LASER_VALID_MEASUREMENT_MAX_RAW = 3570`
- added explicit sentinel handling for `32767`
- upgraded bounded laser validity logs to include raw / signed / scaled / sentinel fields

Documentation notes:

- added `docs/system/laser_measurement_semantics.md`
- added the full audit pack under `reports/laser_audit_1/`
- documented confidence levels where hardware-datasheet proof is still unavailable

Verification:

- `~/.platformio/penv/bin/pio run`

## 2026-03-18 - Task-C Demo APP Chinese Semantic Enhancement

Summary:

- audited the current Android demo status wording and confirmed the main gap was not missing data, but missing Chinese semantic explanation
- kept the demo as an engineering console rather than converting it into a product-style app
- made Chinese meaning primary in the key status cards while preserving raw enum/code as secondary text

Implementation notes:

- updated `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/UiModels.kt`
  - fault code -> Chinese label mapping
  - safety reason/effect/runtime/wave Chinese mapping helpers
- updated `DemoViewModel`
  - safety status now stores both Chinese primary meaning and raw code
  - safety summary messages now include Chinese meaning with raw code in parentheses
- updated `SystemStatusSection`
  - added Chinese-first display strategy with raw secondary text
  - added concise helper text explaining the display strategy
- updated `TelemetryChartSection`
  - added concise Chinese guidance for reading stream stability and noise
- updated `CalibrationToolsSection`
  - added concise Chinese usage guidance
  - exposed current stable weight in the calibration panel
  - improved numeric formatting for model/measurement fields
- updated string resources in both default and `zh-rCN`

Documentation notes:

- updated `docs/system/demo_app_engineering_console.md`
- added `docs/system/demo_app_signal_meaning_zh.md`
- added `docs/system/demo_app_calibration_guide_zh.md`
- added the report pack under `reports/task_c_demo_chinese_semantics/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Task-C.1 Demo Calibration Workflow / Model Comparison Refinement

Summary:

- audited the actual calibration implementation in `tools/android_demo/app-demo`
- confirmed the main usability gap was not missing protocol capability, but missing UI state clarity around recording, capture confirmation, and model comparison
- kept the demo as an engineering tool and did not redesign firmware protocol, BLE transport, or the overall single-screen layout

Implementation notes:

- added a reusable pure Kotlin comparison helper in `tools/android_demo/sonicwave-protocol`
  - distance-mm to runtime-distance conversion
  - linear fit
  - quadratic fit
  - MAE / max-error summary
  - monotonicity check aligned with the current firmware valid raw range
- added JVM tests for the fitting helper and insufficient-point behavior
- extended `DemoViewModel` / `UiState`
  - persistent `calibrationPoints`
  - `comparisonResult`
  - `selectedComparisonModel`
  - `canCaptureCalibrationPoint`
- changed calibration capture behavior so the button is only enabled while recording is active
- accumulated every successful `ACK:CAL_POINT` into an in-memory point list that survives start/stop recording within the same connection
- cleared calibration-point session data on disconnect / new connection to avoid dataset mixing
- rewrote the calibration card to show:
  - explicit workflow order
  - recording-state explanation
  - recorded-point count and list
  - scatter plot with linear/quadratic fitted curves
  - always-visible engineering tools and Chinese helper text
- added bounded `[CAL_UI]` log lines for recording transitions, point capture, model selection, model fetch, and model write

Documentation notes:

- updated `docs/system/demo_app_calibration_guide_zh.md`
- updated `docs/system/demo_app_signal_meaning_zh.md`
- added `docs/system/demo_app_calibration_model_comparison_zh.md`
- added the report pack under `reports/task_c1_calibration_tool_refinement/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Audit-CAL-OWNERSHIP Calibration Recording / Model Ownership Audit

Summary:

- audited the real calibration ownership chain across `app-demo`, `sonicwave-protocol`, `sonicwave-sdk`, firmware protocol parsing, and `LaserModule`
- confirmed the visible “point did not appear” issue should be analyzed as an ACK-driven state problem, not guessed as a chart-only bug
- localized the highest-confidence failure mode to firmware-side capture preconditions with weak UI-local failure visibility

Implementation notes:

- traced `记录校准点` end-to-end:
  - `DemoViewModel.sendCalibrationCapture()`
  - `Command.CalibrationCapture`
  - `ProtocolCodec.encode(...) -> CAL:CAPTURE`
  - firmware `ProtocolCodec::parseCommand(...)`
  - `HubHandler::handle(...)`
  - `LaserModule::captureCalibrationPoint(...)`
  - `ACK:CAL_POINT ...` / `NACK:<reason>`
  - app `ProtocolCodec.parseCalibrationPoint(...)`
  - `Event.CalibrationPoint` -> `UiState.calibrationPoints`
- confirmed the app does not create a local point on click
- confirmed firmware does not maintain a full calibration-point list for later sync
- confirmed the app owns the scatter plot and both local fit curves through `CalibrationComparisonEngine`
- added minimal audit-driven visibility in `DemoViewModel` and `CalibrationToolsSection`:
  - persistent `captureStatus`
  - bounded `[CAL_AUDIT]` logs for click / emit / ACK / NACK / append

Documentation notes:

- added `docs/system/calibration_recording_ownership.md`
- updated calibration docs with ownership and failure-visibility notes
- added the full audit pack under `reports/audit_calibration_ownership/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Audit-ESP32-CAL-BOUNDARY ESP32 Calibration Boundary / Responsibility Audit

Summary:

- audited the calibration boundary specifically from the firmware side instead of only tracing the record-to-UI symptom chain
- confirmed the current architecture already keeps fitting and visualization in the Demo APP
- found the main boundary ambiguity at the capture-command semantics layer rather than in heavy computation ownership

Implementation notes:

- traced firmware calibration responsibilities through:
  - `src/core/ProtocolCodec.h`
  - `src/main.cpp`
  - `src/modules/laser/LaserModule.h`
  - `src/modules/laser/LaserModule.cpp`
- confirmed firmware capture handling currently does:
  - reference sanity check
  - live measurement validity check
  - stable-latched check
  - stable baseline availability check
  - one-shot `ACK:CAL_POINT` payload generation
- confirmed firmware does not:
  - store a reusable calibration point list
  - compute linear fit
  - compute quadratic fit
  - compare candidate models
  - recommend a model
- confirmed firmware does:
  - persist the deployed model
  - validate monotonicity / finiteness of the deployed model
  - evaluate runtime weight using the deployed model
- classified `CAL:CAPTURE` as a shared boundary command whose current naming can be misread as workflow ownership even though it is effectively a validated stable-snapshot request

Documentation notes:

- added `docs/system/esp32_calibration_boundary.md`
- updated calibration ownership docs with firmware-boundary framing
- added the full audit pack under `reports/audit_esp32_calibration_boundary/`

Verification:

- no additional build/test run was required because this audit did not apply code changes

## 2026-03-19 - Task-C.2 Calibration Capture Feedback / Workflow Clarity

Summary:

- refined the calibration capture area without changing calibration ownership or fitting logic
- focused on the user-facing gap between “I clicked capture” and “I understand what happened”
- kept the Demo APP as the workflow owner and used the firmware only as the final stable-sample validator

Implementation notes:

- upgraded capture feedback from a plain string into structured UI state:
  - pending
  - success
  - failure
  - optional raw device reason
- added a precondition checklist near the capture controls covering:
  - recording started
  - device connected
  - stable state visible
  - valid reference input
  - live measurement presence
- added a compact capture summary block so the latest result and current availability can be read at a glance
- mapped firmware capture failures into clearer Chinese wording while keeping raw reason visibility for engineering debug
- clarified button semantics in the calibration card for:
  - capture
  - get model
  - set model
  - CAL:ZERO
  - calibrate
- kept the wording boundary-safe:
  - the app requests capture
  - firmware validates current stable sample
  - successful capture is appended into the app-owned dataset

Documentation notes:

- updated `docs/system/demo_app_calibration_guide_zh.md`
- updated `docs/system/calibration_recording_ownership.md`
- updated `docs/system/demo_app_calibration_model_comparison_zh.md`
- added the report pack under `reports/task_c2_capture_feedback/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Design-CAL-STABLE Stable Weight / App-Authoritative Capture Design

Summary:

- audited the current stable-weight and `CAL:CAPTURE` behavior as the baseline for the next-stage architecture
- confirmed the current firmware stable gate is reasonable for runtime quality, but too rigid to remain the central gate for app-owned calibration recording
- defined the target design where Demo APP captures points directly while firmware continues to own measurement, quality, stable-weight generation, and deployed-model execution

Design notes:

- current firmware stable logic is based on a 10-sample window, a `weight > MIN_WEIGHT` entry threshold, and `stddev(weight) < STD_TH` before latching
- capture currently depends on firmware `STABLE_LATCHED`, which makes the calibration workflow inherit runtime stable gating too directly
- recommended a future stable-weight rule that remains window-based and parameterized, but is more tolerant of micro-fluctuations through combined window checks and debounced confirmation
- recommended that future point capture record an app-local snapshot immediately at click time with:
  - reference weight input
  - current live distance
  - current live weight
  - optional current stable weight
  - timestamp
  - quality/context metadata
- recommended that stable weight become a guidance / quality signal for calibration rather than a universal hard prerequisite
- proposed a staged migration plan that preserves current live telemetry and model write-back paths while decoupling point ownership from `ACK:CAL_POINT`

Documentation notes:

- updated calibration ownership / boundary docs to describe the intended next-stage architecture
- added `docs/system/stable_weight_definition.md`
- added the full design pack under `reports/design_cal_stable_and_app_capture/`

Verification:

- design/documentation task only
- no build or runtime validation was required in this step

## 2026-03-19 - Task-C.3 App-Authoritative Calibration Point Capture

Summary:

- moved calibration point creation to the Demo APP so the point list now grows from a local live snapshot instead of waiting on firmware `ACK:CAL_POINT`
- kept stable weight visible and preserved it as runtime/helper metadata rather than a universal capture gate
- preserved the existing comparison, model selection, and model write-back workflow

Implementation notes:

- `sendCalibrationCapture()` now captures from app-side state at click time:
  - current reference weight input
  - current live distance snapshot
  - current live/predicted weight when visible
  - current stable weight when visible
  - timestamp
  - capture route metadata
- new points are tagged with `APP_LIVE_SNAPSHOT`
- legacy device-returned points still parse and append with `DEVICE_STABLE_CAPTURE`, but that path is no longer the primary workflow
- capture availability now depends on:
  - device connected
  - recording active
  - valid reference weight
  - current live distance snapshot present
- stable visibility and live sample quality remain visible as advisory-only conditions in the UI
- scatter / fit filtering was kept aligned with app-local ownership:
  - valid point
  - distance present
  - reference present
  - no mandatory stable flag requirement

Documentation notes:

- updated calibration guide, ownership notes, ESP32 boundary notes, and stable-weight notes to describe the new main capture route
- added the Task-C.3 report pack under `reports/task_c3_app_capture/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Audit-FIX-C4c-2 Write Model UI Binding Bug

Summary:

- fixed the last visible `写入模型` bug without touching firmware or the ACK protocol chain
- confirmed the write-result source of truth was conceptually correct, but the success/failure handlers were mutating UI state and raw-log state at the same time
- removed the nested-state-update hazard so final SUCCESS / FAILURE can reach the button and result card reliably

Implementation notes:

- `Event.CalibrationSetModelResult`, generic `Ack`, `Nack`, and `Error` write-result handling no longer call `appendSystemLog(...)` from inside `_uiState.update`
- write-result feedback is now computed first, committed to `writeModelStatus`, and only then logged
- this prevents `MutableStateFlow.update` retry behavior from re-evaluating the handler after:
  - `awaitingModelWriteResult` was already cleared
  - `pendingWriteModelType` was already nulled
  - the old pending `writeModelStatus` was still present in the retried state snapshot
- added bounded `[CAL_UI_BIND]` diagnostics so runtime logs can show the render source-of-truth for:
  - button text
  - button visual state
  - result card state

Documentation notes:

- updated `docs/project_status.md`
- added the audit/fix report pack under `reports/audit_fix_c4c2_write_ui_binding/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-19 - Task-C.4d Calibration Point Context Display Polish

Summary:

- added a small capture-context block around `记录校准点` so the user can verify the two values that matter most right before recording:
  - current live distance
  - current reference-weight input
- preserved the original distance/weight/stable-weight display in its existing place at the top of the calibration card
- kept the point-capture workflow and logic unchanged

Implementation notes:

- inserted a compact bordered context card directly below the reference-weight input in `CalibrationToolsSection`
- the new block shows:
  - current live distance in `mm`
  - current reference weight in `kg`
- if the current reference input is not parseable, the local block now surfaces that invalid value instead of hiding it
- added a short helper line clarifying that the next capture uses the current reference-weight input plus the current live-distance snapshot

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/demo_app_calibration_guide_zh.md`
- added the report pack under `reports/task_c4d_capture_context_display/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-20 - Task-C.4e Capture Section Layout Correction

Summary:

- rolled back the Task-C.4d local context direction around `记录校准点`
- removed the extra bordered `录点前上下文` block from Step 3
- reordered Step 3 to match the intended capture workflow:
  - `录点条件`
  - `录点摘要`
  - `参考重量输入框`
  - `实时距离`
  - `记录校准点`
- kept the point-capture logic and downstream calibration workflow unchanged

Implementation notes:

- confirmed the actual Demo APP source-of-truth path under `tools/android_demo/app-demo/`
- reordered the existing `CapturePreconditionChecklist`, `CaptureResultSummary`, reference-weight input, and capture button inside `CalibrationToolsSection`
- reused the existing `uiState.distance` live measurement source and surfaced it as a simple Step 3 line above the record button
- removed the Task-C.4d-only context strings and helper composable instead of adding another nearby duplicate display

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/demo_app_calibration_guide_zh.md`
- added the report pack under `reports/task_c4e_capture_layout_correction/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
