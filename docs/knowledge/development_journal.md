# Development Journal

## 2026-04-02 - Phase 1 Exit Passed And Phase 2 Approved

Summary:

- completed the on-site Phase 1 validation closeout on April 2, 2026
- confirmed the Demo APP start button behavior now matches the intended gate contract
- recorded the formal phase decision as:
  - `Phase 1 Exit Decision = Passed`
  - `Can Proceed to Phase 2 = Yes`

Key evidence:

- without stable weight, the start button stayed gray and non-clickable
- after stable weight was established, the start button turned green quickly and became clickable
- the Demo APP interaction experience matched the expected Phase 1 behavior

Conclusion:

- Phase 1 is closed
- Phase 2 can start on the current validated baseline

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/roadmap_v1.md`

## 2026-04-01 - Phase 1 Real-Device Validation And Exit Decision

Historical note: this 2026-04-01 pending decision was superseded by the 2026-04-02 Phase 1 exit pass.

Summary:

- ran a real-device BLE + serial validation pass against the attached `SonicWave_Hub` bench unit
- confirmed the checked-in source and protocol tests expect:
  - `SNAPSHOT.wave_output_active`
  - `EVT:WAVE_OUTPUT active=<0|1>`
- confirmed the attached real device did not currently expose either signal
- confirmed the attached bench unit was `platform_model=BASE` and `laser_installed=0`, so measurement-plane exit validation could not be completed on this hardware profile
- recorded the honest phase decision at that time as:
  - `Phase 1 Exit = Pending`
  - `Can Proceed to Phase 2 = No`

Key evidence:

- BLE `ACK:CAP` returned `fw=SW-HUB-1.0.0 proto=2 platform_model=BASE laser_installed=0`
- real-device `SNAPSHOT` restored `top_state`, but omitted `wave_output_active`
- start path emitted `EVT:STATE RUNNING` and `ACK:OK`, but no `EVT:WAVE_OUTPUT`
- stop path emitted `EVT:STOP` and `EVT:STATE ARMED`, but no `EVT:WAVE_OUTPUT`
- default serial on the bench image still emitted repeated `measurement_bypass=1 reason=no_laser_config` lines, which also did not match current source logging cadence

Conclusion:

- the most likely mismatch is between the flashed bench firmware image and the current worktree, not between the current source files themselves
- Phase 1 cannot be honestly exited until bench firmware is reflashed to the current contract and rerun on a measurement-capable device/profile

Artifacts:

- `reports/tasks/phase1_real_device_validation/`

## 2026-03-21 - Task-MOTION-EXPORT-AUTOMATION 导出会话自动化（采样标签与自动命名）

Summary:

- 在 Demo APP 的 Motion-Safety Sampling Tool 中，把原来的“直接导出”流程改成“先填标签，再导出”
- 统一了 CSV / JSON 文件名，减少人工重命名和样本交接歧义
- 给 JSON metadata 增加了结构化标签与上下文字段，方便后续脚本直接读样本语义
- 保持采样、运行时 safety、sampling mode、stop / pause 行为不变

Implementation notes:

- 在 `MotionSamplingSection.kt` 中新增轻量导出标签表单：
  - 主标签：`NORMAL_USE` / `FALL_DURING_USE`
  - 细分类标签：`NORMAL_VIBRATION`、`LEAVE_PLATFORM`、`PARTIAL_LEAVE`、`FALL_ON_PLATFORM`、`FALL_OFF_PLATFORM`、`LEFT_RIGHT_SWAY`、`SQUAT_STAND`、`RAPID_UNLOAD`、`OTHER_DISTURBANCE`
  - 备注：可选文本输入
- 在 `UiModels.kt` 中定义导出标签枚举和新的 `MotionSamplingExportRequest`
- 在 `MotionSamplingExporter.kt` 中：
  - 统一 CSV / JSON 的基础文件名
  - 把主标签、细分类、备注、频率、强度、导出时间写入 JSON metadata
  - 保留旧的 `scenarioLabel` / `scenarioCategory` 兼容字段
- 在 `DemoViewModel.kt` 中保持原导出主链路不变，只更新导出完成后的会话摘要回显
- 所有新增注释都明确强调：
  - 这是导出自动化增强
  - 不改变运行时 safety 判定逻辑

Documentation notes:

- 更新了 `docs/system/motion_sampling_tool.md`
- 更新了 `docs/project_status.md`
- 补充了 `reports/task_motion_export_automation/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
- `tools/android_demo ./gradlew :sonicwave-protocol:test`

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

## 2026-03-20 - Task-MOTION-1B Session Labeling, Metadata, and Export Action Polish

Summary:

- refined the motion-sampling export flow so the operator explicitly labels the scenario at export time
- enriched the session snapshot and JSON metadata with the missing wave/sampling/model context
- improved file naming so exported sessions are easier to identify later
- strengthened export/clear semantics so unexported sessions are harder to lose accidentally

Implementation notes:

- extended `MotionSamplingSessionUi` with:
  - wave frequency/intensity snapshot
  - sampling-mode flag
  - running-at-session-start flag
  - last export scenario and timestamp
- changed `MotionSamplingExporter` to build filenames from:
  - scenario label
  - frequency
  - intensity
  - export timestamp
- added an export dialog in `MotionSamplingSection` with fixed scenario options and a custom-text path
- changed the action row so export is green/primary and clear is red/destructive
- added a stopped-but-unexported recommendation plus a confirmation dialog before discarding an unexported session
- kept start/stop sampling, row capture, charts, and runtime leave/fall behavior unchanged

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/motion_sampling_tool.md`
- added the report pack under `reports/task_motion_1b_session_export_polish/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-20 - Task-STATE-UI-1 System Status Hierarchy Cleanup

Summary:

- refactored the Demo APP system-status section so `状态` becomes the primary top-level signal
- kept `安全原因` and `影响` prominent as the main explanation layer
- demoted `运行态` and especially `波形态` to a lower-priority engineering reference area
- preserved fault/code/source/meaning details below the primary interpretation layer

Implementation notes:

- replaced the previous equal-weight status grid with a clearer hierarchy:
  - primary heading: `当前主状态`
  - large hero card for `状态`
  - primary supporting cards for `安全原因` and `影响`
  - secondary heading: `工程参考状态`
  - lower-emphasis cards for `运行态` and `波形态`
- removed the top-level `故障` peer card and preserved fault information in the details text area instead
- updated the status-section hint text so it explicitly describes the new trust/usefulness order

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/demo_app_calibration_guide_zh.md`
- added the report pack under `reports/task_state_ui_1_hierarchy_cleanup/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-20 - Task-WAVE-UI-1A Bottom Wave Bar Layout and Preset Highlight Polish

Summary:

- polished the fixed bottom wave-control bar without changing its working interaction logic
- reduced the visual dominance of the frequency and intensity input fields so the bar reads more like a compact control strip
- strengthened the selected preset state by changing the preset highlight to orange
- preserved the current start/stop logic, connection-dependent behavior, and existing status descriptions

Implementation notes:

- updated `WaveControlBottomBar` instead of changing the broader screen structure
- reduced the relative width/visual weight of the two input fields and slightly tightened their presentation
- increased row spacing and rebalanced the row weights so preset chips and start/stop buttons stand out more clearly
- applied a stronger orange selected-state color to both frequency and intensity preset chips
- left the existing start/stop enable logic and current status-hint text unchanged

Documentation notes:

- updated `docs/project_status.md`
- added the report pack under `reports/task_wave_ui_1a_bar_polish/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-20 - Task-MOTION-1A Sampling Mode Fall Action Suppression

Summary:

- added a controlled engineering motion-sampling mode so repeated false fall triggers no longer immediately interrupt waveform output during data collection
- kept fall detection itself alive and observable instead of silently disabling it
- left `USER_LEFT_PLATFORM` behavior untouched so leave/fall semantics do not blur together

Implementation notes:

- confirmed the action split:
  - `LaserModule` still raises `onUserOff()` and `onFallSuspected()`
  - `SystemStateMachine` still owns stop/pause behavior
- added a dedicated firmware command/config path:
  - `DEBUG:MOTION_SAMPLING enabled=0|1`
- extended capability reporting so the current firmware mode can be observed through:
  - `motion_sampling_mode`
  - `fall_action_suppressed`
- in sampling mode:
  - `onFallSuspected()` still runs
  - fall visibility is still emitted as fault/safety output
  - fall stop action is downgraded to non-stopping observability
  - bounded suppression diagnostics are rate-limited to avoid log spam
- in normal mode:
  - fall behavior stays on the original blocking/pause path
- leave-platform behavior was intentionally preserved with no suppression changes
- added a Demo APP control/visibility layer in the motion-sampling section:
  - enable mode
  - disable mode
  - explicit status banner showing whether fall stop suppression is active
  - capability parsing to reflect firmware state in the UI

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/motion_safety_framework.md`
- updated `docs/system/motion_sampling_tool.md`
- added the report pack under `reports/task_motion_1a_sampling_fall_suppression/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
- `pio run` could not be executed here because `pio` is unavailable in the current environment

## 2026-03-20 - Task-WAVE-UI-1 Fixed Bottom Wave Control Bar

Summary:

- moved the high-frequency wave controls out of the scrollable content and into a lightweight fixed bottom bar
- kept the implementation compact instead of turning the bottom area into a large heavy control panel
- preserved the existing wave command behavior while making start/stop and parameter edits persistently reachable

Implementation notes:

- added a reusable `WaveControlBottomBar` component to the Demo APP
- mounted it through `Scaffold.bottomBar`, so the rest of the engineering page keeps scrolling above it
- replaced the old scroll-area wave card as the primary wave-control surface
- implemented the intended two-row structure:
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
- presets act as quick-fill helpers only:
  - manual frequency and intensity entry still remain fully usable
- refined button visuals and enable rules:
  - start uses green when startable
  - stop uses red while running
  - disabled states are gray and visually consistent with behavior
  - invalid manual values now disable start until corrected
- added a concise current-state hint above the bar for faster debugging comprehension
- added bounded `[WAVE_UI]` diagnostics for preset clicks

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/demo_app_calibration_guide_zh.md`
- added the report pack under `reports/task_wave_ui_1_fixed_bottom_bar/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`

## 2026-03-20 - Audit-MOTION-1C Leave-Platform Pause-to-Stop Action Closure

Summary:

- audited the already-working leave detection path and confirmed the bug was not in detection
- found that `USER_LEFT_PLATFORM` was reaching `RECOVERABLE_PAUSE`, but the running waveform path was not being closed immediately
- applied the smallest fix by reusing the existing internal stop-request path when recoverable pause is entered from `RUNNING`

Implementation notes:

- current path before the fix was:
  - leave detected in `LaserModule`
  - `onUserOff()` called
  - `enterRecoverablePause(USER_LEFT_PLATFORM, ...)` latched the pause reason
  - `syncReadyState()` then refused to leave `RUNNING`
  - visibility was emitted as `RECOVERABLE_PAUSE` while state/wave could still report `RUNNING`
  - actual stop later depended on an external `WAVE:STOP`
- the minimal reliable closure fix was added in `SystemStateMachine::enterRecoverablePause(...)`
- when recoverable pause is entered while the machine is still `RUNNING`, the code now:
  - logs bounded leave-closure diagnostics
  - calls `requestStop()`
  - reuses the existing internal stop/state-exit path instead of inventing a second stop mechanism
- this keeps the action chain explicit:
  - `USER_LEFT_PLATFORM`
  - `RECOVERABLE_PAUSE`
  - automatic running-path closure
- sampling/debug mode remains isolated to fall suppression:
  - it does not interfere with leave closure

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/motion_safety_framework.md`
- updated `docs/system/fall_detection_logic.md`
- added the report pack under `reports/audit_motion_1c_leave_action_closure/`

Verification:

- `tools/android_demo ./gradlew :sonicwave-protocol:test`
- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
- `pio run` could not be executed here because `pio` is unavailable in the current environment

## 2026-03-20 - Task-MOTION-1 Motion-Safety Sampling Tool MVP

Summary:

- built the first usable Motion-Safety Sampling Tool MVP inside the Demo APP engineering surface
- kept the task focused on record / view / export, without redesigning firmware leave/fall logic
- reused the existing live stream and runtime/safety UI state as the source of truth for captured rows

Implementation notes:

- added a new `MotionSamplingSection` to the Demo APP main engineering screen
- added explicit motion-sampling session controls:
  - start
  - stop
  - clear unsaved session
  - export session
- introduced a dedicated motion-sampling session model separate from the existing calibration telemetry recorder
- sampling rows are captured from `onStreamSample(...)` and stored as structured time-series data instead of raw log text
- the recorded row schema includes:
  - timestamps and elapsed time
  - live distance
  - live weight
  - nullable stable weight
  - measurement validity
  - runtime state
  - wave state
  - safety effect/reason codes
  - connection state
  - nullable model / marker / future motion-safety fields
  - nullable `ddDt` and `dwDt` extension values
- the session remains reviewable after stop because MVP storage is:
  - in-memory active session
  - explicit export when the operator decides to save it
- added recorded-session review UI:
  - session id / timestamps / row count
  - last recorded values
  - recent row preview
  - session-based chart rendering
- added export through `MotionSamplingExporter`:
  - CSV rows for analysis
  - JSON metadata sidecar for schema version, model metadata, and future extensibility hints
  - target output in `Downloads/SonicWave/`
- added bounded `[MOTION_SAMPLE]` diagnostics without high-frequency per-row log spam

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/motion_safety_framework.md`
- added `docs/system/motion_sampling_tool.md`
- added the report pack under `reports/task_motion_1_sampling_mvp/`

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

## 2026-03-20 - Design-MOTION-SAFETY Motion Safety Framework and Debug Sampling Design

Summary:

- defined the next-stage motion safety framework around the real current firmware baseline instead of jumping to guessed thresholds
- kept the public runtime contract unchanged:
  - `IDLE / ARMED / RUNNING / FAULT_STOP`
  - `EVT:STATE / EVT:FAULT / EVT:SAFETY`
- defined a separate internal/debug-facing motion safety layer with states for:
  - empty
  - occupied idle
  - running occupied
  - leave candidate / left platform
  - fall candidate / fall confirmed
  - recovering
- documented that laser directly measures distance while weight is model-derived, and both are useful for safety analysis

Implementation notes:

- audited the current baseline:
  - leave-platform is currently derived-weight hysteresis plus edge trigger
  - fall suspicion is currently a single-frame derived-weight rate threshold during `RUNNING`
  - Demo APP currently only records raw telemetry rows as `timestamp,distance,weight,stable`
- defined modular detector structure for both leave-platform and fall:
  - candidate
  - confirmation
  - classification or recovery
  - action output
- defined the Demo APP debug sampling direction as an evolution of the existing telemetry recorder and chart path, not a separate app
- chose row-based time-series as the primary record format, with charts as secondary visualization
- chose:
  - CSV for per-sample export
  - JSON for session metadata, labels, markers, and analysis context
- defined compact firmware parameter categories rather than final numeric thresholds

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/fall_detection_logic.md`
- added `docs/system/motion_safety_framework.md`
- added the report pack under `reports/design_motion_safety_framework/`

Verification:

- documentation/design task only
- no firmware or Demo APP logic change was applied in this step

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

## 2026-03-21 - Task-MOTION-ANALYSIS-1 Moving-Average Research Overlay

Summary:

- added a lightweight moving-average research overlay inside the Demo APP motion-sampling review surface
- kept the task focused on post-capture comparison of raw vs smoothed curves
- left firmware leave/fall ownership and runtime safety behavior untouched

Implementation notes:

- confirmed the active Demo APP source-of-truth path under `tools/android_demo/app-demo/`
- kept the UI changes local to `MotionSamplingSection`
- added one shared MA point-count input with:
  - default `5`
  - presets `3 / 5 / 7`
  - valid range `1..50`
  - automatic recompute when the applied value changes
- derived moving-average values only from the captured in-memory session rows already shown in the sampling tool
- kept raw captured rows intact and computed MA data as local derived UI series rather than mutating:
  - `MotionSamplingRowUi`
  - `MotionSamplingSessionUi`
  - export payloads
- extended the combined session chart so raw and MA curves are visible together for:
  - distance
  - live weight
- added explicit latest `MA(n)` readouts next to the existing raw latest-value summary
- left `stableWeightKg` out of the overlay MVP because it is nullable and tied to stable-visibility state instead of continuous row coverage
- added concise research-only copy so the MA overlay is not mistaken for current runtime safety logic
- confirmed the runtime boundary stayed unchanged:
  - `LaserModule` still raises leave/fall conditions
  - `SystemStateMachine` still owns stop/pause effects
  - `DemoViewModel`, exporters, and protocol models were not touched for this overlay

Documentation notes:

- updated `docs/project_status.md`
- updated `docs/system/motion_sampling_tool.md`
- added the Task-MOTION-ANALYSIS-1 report pack under `reports/task_motion_analysis_1_ma_overlay/`

Verification:

- `tools/android_demo ./gradlew :app-demo:compileDebugKotlin`
- `tools/android_demo ./gradlew :sonicwave-protocol:test`
