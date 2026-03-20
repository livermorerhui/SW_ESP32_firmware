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

Recommended APP mapping:

- `RECOVERABLE_PAUSE` -> paused
- `ABNORMAL_STOP` -> abnormal stop
- `WARNING_ONLY` -> warning badge / degraded banner
