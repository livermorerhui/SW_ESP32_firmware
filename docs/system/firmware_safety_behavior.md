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

Recommended APP mapping:

- `RECOVERABLE_PAUSE` -> paused
- `ABNORMAL_STOP` -> abnormal stop
- `WARNING_ONLY` -> warning badge / degraded banner
