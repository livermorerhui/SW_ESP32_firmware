# Firmware Safety Behavior

## Final Runtime Behavior

### User Leaves Platform

- default reason: `USER_LEFT_PLATFORM`
- default effect: `RECOVERABLE_PAUSE`
- default runtime result:
  - wave stops
  - state leaves `RUNNING`
  - no abnormal-stop latch by default

### Fall Suspected

- reason: `FALL_SUSPECTED`
- default effect: `ABNORMAL_STOP`
- runtime result:
  - wave stops
  - state becomes `FAULT_STOP`
  - blocking cooldown / recovery logic remains active

### BLE Disconnected

- reason: `BLE_DISCONNECTED`
- default effect: `WARNING_ONLY`
- default runtime result:
  - no forced stop
  - behavior remains configurable through firmware policy
- note:
  - transport loss itself is the primary APP-visible signal at disconnect time

### Measurement Unavailable

- reason: `MEASUREMENT_UNAVAILABLE`
- default effect: `WARNING_ONLY`
- default runtime result:
  - no forced stop
  - no accidental start block by default
  - behavior remains configurable through firmware policy

## APP-Facing Signals

- `EVT:STATE`
- `EVT:FAULT`
- `EVT:SAFETY`

## SafetyAction / StopReason Owner Boundary

Current owner split:

- `LaserModule` owns presence / baseline / rhythm danger evidence and may only raise candidates.
- `SystemStateMachine` owns final safety action, stop action, `stop_reason`, `stop_source`, visible reason, and `EVT:STOP / EVT:SAFETY / EVT:FAULT` emission.
- `ProtocolCodec` owns the existing BLE line encoding only.
- `BleTransport` owns critical-event delivery classification only.

Frozen rule:

- A detector or measurement owner must not directly stop the wave or publish final BLE safety semantics.
- `USER_LEFT_PLATFORM` and `FALL_SUSPECTED` must remain separate product reasons.
- Disabling `FALL_STOP` only suppresses the `FALL_SUSPECTED` automatic stop action; it does not suppress `USER_LEFT_PLATFORM`.
- Any future extraction must first move pure reason/effect/source evaluation only, while leaving action timing in `SystemStateMachine`.

2026-04-28 implementation boundary:

- `SafetyActionContractEvaluator` owns only pure `FALL_SUSPECTED` action decision and stop reason/source fallback evaluation.
- `FallStopActionDecision` is a DTO for the decision result; executing the result remains in `SystemStateMachine::applyFallSuspectedAction`.
- `SystemStateMachine` still owns `enterBlockingFault`, `enterRecoverablePause`, `requestStop`, `emitStopEvent`, `emitSafety`, `emitFault`, `WaveModule::stopSoft`, `onUserOff`, `onBleDisconnected`, and `setSensorHealthy`.
- BLE line formats and `SystemStateMachine` action timing are unchanged.

2026-04-28 BLE diagnostic guard:

- `send_skipped` logs for non-critical frames are time-throttled by `FirmwareLogPolicy` when BLE is not connected.
- `BLE_TX_PRESSURE` is no longer forced by every non-critical skipped frame and is time-throttled by `FirmwareLogPolicy`.
- Critical events still mark reconnect snapshot dirty; this preserves reconnect truth without flooding serial logs.

2026-04-28 smoke evidence:

- `safety_action_log_policy_smoke_retry` passed on ESP32-plus normal hardware with SW APP.
- Android evidence: `CONNECT_SUCCESS=2`, `DEVICE_SNAPSHOT_SYNCED=20`, `CONNECT_SNAPSHOT_REFRESH_FAILED=0`, `start_confirmed_by_device=2`, `stop_confirmed_by_device=1`.
- ESP32 evidence: `START ALLOW=1`, `STOP_SUMMARY=1`, `reconnect_snapshot_compensated=2`, no reset/panic/Brownout/Guru, no `MEASUREMENT_TRANSIENT`, no Modbus read fail.
- Disconnected `send_skipped` logs were reduced to the `FirmwareLogPolicy` cadence; `BLE_TX_PRESSURE` remains a periodic diagnostic summary.

Recommended APP mapping:

- `RECOVERABLE_PAUSE` -> paused
- `ABNORMAL_STOP` -> abnormal stop
- `WARNING_ONLY` -> warning badge / degraded banner
