# Current Hardware Validation Boundary

## Purpose

This document records what has and has not been validated with the current
hardware setup:

- ESP32-S3 present
- no PCM5102A
- no MAX485
- no laser range sensor

It is meant to prevent over-claiming test coverage after the recent BLE,
bootstrap-truth, and start-readiness fixes.

## Current Hardware Condition

The current bench setup can exercise:

- BLE advertising
- BLE connection and reconnection
- BLE RX write / TX notify
- bootstrap truth (`CAP? -> ACK:CAP`)
- runtime truth refresh (`SNAPSHOT? -> SNAPSHOT`)
- profile write / refresh flow
- degraded-start authorization flow
- wave control command flow

The current bench setup cannot exercise the real measurement plane because the
sensor path is absent and the firmware remains in a measurement-unavailable
state.

Typical observed runtime facts in this setup:

- `platform_model` and `laser_installed` can still be validated
- `laser_available=0`
- `degraded_start_available=1` may still be validated
- repeated `Modbus read fail` is expected
- no sustained valid measurement stream is expected

## Validated In Current Setup

The following items are considered validated with the current hardware:

- BLE advertising is stable.
- BLE connection and repeated reconnect are stable.
- TX notify subscription is stable.
- `CAP? -> ACK:CAP` is stable.
- `SNAPSHOT? -> SNAPSHOT` is stable.
- Device profile read / write / refresh is stable.
- Demo APP bootstrap truth and runtime truth refresh are logically aligned with
  the firmware.
- Start button state is consistent with the current bootstrap/runtime truth in
  the tested BASE / PLUS / degraded-start paths.
- `DEBUG:DEGRADED_START -> ACK:DEGRADED_START` is stable.
- `WAVE:SET` high-frequency control changes are stable.
- `WAVE:START / WAVE:STOP` control flow is stable.
- `ACK:OK`, `EVT:STATE`, `EVT:WAVE_OUTPUT`, and `SNAPSHOT` continue to arrive
  correctly under control-side pressure.
- Repeated reconnect no longer drops initialization truth.
- No current evidence shows `ACK:CAP` or connect-time `SNAPSHOT` being broken
  by MTU-sized first-packet issues.

## Not Yet Validated In Current Setup

The following items are not yet fully validated because the required peripherals
are not present:

- laser measurement availability under real hardware
- MAX485 real communication stability
- PCM5102A real output path stability
- correctness of distance / weight telemetry
- full calibration loop with real measurement input
- sustained high-rate `EVT:STREAM` under real measurement conditions
- control-vs-stream contention under sustained stream pressure
- stream suppression / backpressure behavior under real measurement load
- long-duration BLE throughput stability while valid telemetry is flowing
- recovery behavior after real sensor-path fault and recovery transitions

## What Current Pressure Tests Actually Prove

### Repeated Connect / Reconnect

Current repeated connect tests prove:

- connection bootstrap is stable
- `CAP?` truth is not being lost
- connect-time `SNAPSHOT` truth is not being lost
- the firmware is not force-disconnecting due to the old "no early RX write"
  path

They do not prove:

- telemetry-plane robustness under real stream load

### Control Pressure

Current control pressure tests prove:

- repeated `WAVE:SET` does not destabilize BLE control flow
- repeated `WAVE:START / WAVE:STOP` remains reliable
- truth refresh remains usable while control commands are active

They do not prove:

- control priority under sustained real `EVT:STREAM` pressure

## Current Formal Conclusion

With the current hardware, it is reasonable to state:

- BLE initialization flow is validated.
- BLE control flow is validated.
- device-profile truth refresh is validated.
- degraded-start flow is validated.
- Demo APP and firmware are logically compatible for bootstrap truth, runtime
  truth, and control commands.

It is not yet reasonable to state:

- the full measurement plane is validated
- control-vs-stream contention is fully validated
- real telemetry throughput behavior is validated

## Next Validation Priority When Hardware Is Available

Once the laser / MAX485 / related sensor hardware is available, the next
priority tests should be:

1. sustained valid `EVT:STREAM` while repeatedly issuing `WAVE:SET`
2. sustained valid `EVT:STREAM` while repeatedly issuing `WAVE:START / STOP`
3. repeated `SNAPSHOT?` during active valid telemetry
4. long-duration connection with active measurement flow
5. full calibration loop with real measurement data
6. fault / recovery truth consistency after real sensor-path transitions

## Fixed Boundary Statement

Current project status should be described as:

- BLE init and control flow validated on ESP32-S3-only bench hardware
- real measurement-plane and stream-contention validation still pending
