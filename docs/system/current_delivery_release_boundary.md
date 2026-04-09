# Current Delivery Release Boundary

## Purpose

This document is the final closeout note for the current development round.

Its purpose is to state, in one place:

- what this round actually completed
- what can reasonably be demonstrated or released now
- what remains outside the current release boundary

It should be used as the default reference when describing current project
status to future contributors, demo operators, or follow-up tasks.

## Final Outcome Of This Round

This round is considered complete for the current hardware boundary:

- `Phase 1` completed
- `Phase 2 core` completed
- `Phase 2.5` delivery closure completed for the current subset
- `Phase 3` completed as one bounded low-risk efficiency pass
- `Phase 4` completed as one substantial low-risk power pass

This does **not** mean the full measurement-capable product is complete.

## Current Releaseable Subset

The current releaseable subset is:

- `BASE`
- `PLUS + laser installed + measurement unavailable + degraded-start`

This subset is BLE-controlled and bench-validated on the current ESP32-S3-only
setup.

## What Is Considered Stable Now

The following are considered stable within the current release boundary:

- BLE advertising
- BLE connect / disconnect / reconnect
- `TX=notify` and `RX=write`
- `CAP? -> ACK:CAP`
- `SNAPSHOT? -> SNAPSHOT`
- profile write and truth refresh
- `BASE` immediate-start path
- `PLUS` degraded-start authorization path
- `WAVE:SET / WAVE:START / WAVE:STOP`
- current Demo APP truth / button / prompt behavior for the supported subset

## What This Round Added Beyond Functional Closure

### Phase 3 Outcome

The bounded Phase 3 pass delivered:

- lower firmware log noise
- lower Demo APP background noise
- reduced repeated protocol/queue log spam
- reduced repeated ACK/NACK string-assembly overhead

### Phase 4 Outcome

The bounded Phase 4 pass delivered:

- lower BLE transmit power than the previous maximum setting
- staged advertising:
  - `FAST_DISCOVERY`
  - `IDLE_LOW_POWER`
- lower advertising-only TX power in the idle advertising profile
- reduced idle polling in no-laser / unavailable-measurement scenarios
- reduced repeated idle Modbus read attempts via backoff
- reduced task wake frequency during idle backoff windows

These changes were treated as acceptable only because current bench validation
did not show regressions in discovery, connect, reconnect, degraded-start, or
wave control.

## Explicit Release Boundary

The current release boundary is:

- a stable BLE-controlled subset for `BASE`
- a stable BLE-controlled subset for `PLUS` degraded-start under current bench
  constraints
- one completed low-risk efficiency pass
- one substantial low-risk power pass

The current release boundary is **not**:

- a full measurement-capable release
- a real-sensor validation pass
- a MAX485 validation pass
- a PCM5102A validation pass
- a calibration closed-loop release
- a long-duration full-system performance or power certification

## What Must Not Be Over-Claimed

The following statements should **not** be used:

- "the full product is complete"
- "measurement path is validated"
- "real telemetry stream is validated"
- "full power optimization is complete"
- "full hardware stack is validated"

The following statements are reasonable:

- "the current BLE-controlled delivery subset is stable on the present bench"
- "`BASE` and `PLUS` degraded-start are the supported scenarios of this round"
- "full measurement-capable validation remains for later hardware-enabled work"

## Supported Demo Story

The supported demo story for this round is:

1. power on and advertise
2. connect and complete `CAP? -> SNAPSHOT?`
3. demonstrate `BASE`, or demonstrate `PLUS` degraded-start
4. run `WAVE:SET / START / STOP`
5. disconnect and reconnect
6. confirm truth and control path remain consistent

The demo should avoid presenting unavailable sensor-path behavior as if it were
validated healthy measurement behavior.

## Follow-Up Work After This Round

Future work should be treated as a separate track and should start from one of
these directions:

1. measurement-capable validation once the required hardware is available
2. calibration closed-loop work
3. real telemetry stream / contention validation
4. deeper power work only if backed by real current-draw measurement and a
   clear no-regression plan

## Reference Documents

This closeout note should be read together with:

- `docs/project_status.md`
- `docs/system/base_plus_degraded_delivery_plan.md`
- `docs/system/base_plus_degraded_regression_checklist.md`
- `docs/system/base_plus_degraded_manual_validation.md`
- `docs/system/current_hardware_validation_boundary.md`
