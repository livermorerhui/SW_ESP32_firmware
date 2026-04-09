# BASE / PLUS Degraded Delivery Plan

## Purpose

This document defines the deliverable subset that can be completed now with the
current bench constraints and without waiting for full healthy measurement-path
validation.

The intent is to cut scope cleanly and deliver a stable subset for:

- `BASE`
- `PLUS + laser installed + measurement unavailable`

This is a deliberate product slice, not a temporary workaround.

## Product Goal For This Stage

The current stage should deliver a stable BLE-controlled device profile and
wave-control product subset that does not depend on the real measurement plane.

The subset must provide:

- stable BLE connect / reconnect
- stable device profile read / write / refresh
- stable start / stop interaction
- stable fall-stop / degraded-start related interaction
- Demo APP truth and UI behavior aligned with firmware truth

This stage does **not** aim to deliver full measurement-capable hardware
behavior.

## Supported Product Scenarios

### `BASE`

Expected behavior:

- `platform_model=BASE`
- `laser_installed=0`
- start button becomes ready after connect
- no stable-weight requirement
- no laser availability requirement
- `WAVE:SET / WAVE:START / WAVE:STOP` remain usable

### `PLUS + laser installed + measurement unavailable`

Expected behavior:

- `platform_model=PLUS`
- `laser_installed=1`
- `laser_available=0`
- `degraded_start_available=1`
- Demo APP explicitly indicates degraded-start / repair-needed state
- after degraded-start confirmation, start remains available
- `WAVE:SET / WAVE:START / WAVE:STOP` remain usable

## Explicitly Out Of Scope For This Stage

The following items are not blockers for the current subset delivery:

- real healthy laser distance measurement
- real MAX485 communication
- real PCM5102A output-path verification
- correct distance / weight telemetry
- high-rate valid `EVT:STREAM`
- calibration closed loop with real measurement input
- real control-vs-stream contention under sustained telemetry
- real peripheral fault / recovery validation with full hardware

## Firmware Contract For This Subset

The firmware must keep the current compatibility-safe BLE contract:

- `TX=notify`
- `RX=write`
- newline framed text protocol remains unchanged
- `ACK:CAP` remains bootstrap truth only
- `SNAPSHOT` remains the constrained runtime truth subset used by connect-time
  and start-gate logic

For this subset specifically:

- `BASE` truth must remain sufficient for immediate start readiness
- `PLUS` degraded-start truth must remain sufficient for reconnect-time
  degraded authorization and start availability
- current BLE control-priority behavior must remain intact

## Demo APP Contract For This Subset

The Demo APP must keep the current initialization order:

1. connect
2. subscribe notify
3. `CAP?`
4. `SNAPSHOT?`

For this subset specifically:

- `BASE` must not be blocked by stable-weight assumptions
- `PLUS` degraded-start path must follow `degraded_start_available /
  degraded_start_enabled` truth rather than local UI heuristics
- the start button must follow bootstrap/runtime truth rather than hidden UI
  heuristics
- profile writes must be followed by `CAP? -> SNAPSHOT?` refresh

## Acceptance Criteria

### `BASE`

The subset is acceptable only if all of the following remain true:

- connect / reconnect is stable
- `ACK:CAP` is received reliably
- `SNAPSHOT` is received reliably
- start button becomes ready after connect
- `WAVE:SET / START / STOP` remain stable
- reconnect does not regress start-button readiness

### `PLUS + laser installed + measurement unavailable`

The subset is acceptable only if all of the following remain true:

- connect / reconnect is stable
- `ACK:CAP` is received reliably
- `SNAPSHOT` is received reliably
- the APP clearly indicates degraded-start / repair-needed state
- degraded-start authorization remains available
- start becomes available after degraded-start confirmation
- `WAVE:SET / START / STOP` remain stable
- reconnect does not regress the degraded-start path

### Shared BLE / Truth Criteria

The subset is not acceptable if any of the following reappear:

- `ACK:CAP` truncation
- `SNAPSHOT` truncation
- MTU-budget warnings for truth packets
- reconnect-time truth loss
- start-button truth drifting away from firmware truth
- BLE mis-disconnect caused by initialization timing

## Regression Checklist

Use the focused checklist in:

- `docs/system/base_plus_degraded_regression_checklist.md`
- `docs/system/base_plus_degraded_manual_validation.md`

## Implementation Priorities

### Now

The current priorities for this subset are:

1. keep the BLE initialization contract frozen
2. keep truth packet budgets frozen
3. keep Demo APP start-readiness behavior frozen for `BASE` and
   `PLUS` degraded-start
4. avoid reintroducing measurement-dependent assumptions into these two paths

### Later

The following belongs to the later measurement-capable product track:

- `PLUS + laser installed + healthy measurement chain`
- MAX485-backed measurement path
- sustained valid `EVT:STREAM`
- calibration and full measurement closed loop

## Release Boundary Statement

The current stage should be described as:

- a stable BLE-controlled subset for `BASE` and `PLUS` degraded-start
- not the final full-measurement product

That boundary must be kept explicit in demos, testing notes, and future task
planning.

## Phase 3 Closure Note

For the current delivery subset, the bounded low-risk Phase 3 pass is now
considered complete.

That completion specifically covers:

- lower serial-log pressure on the current bench
- lower BLE suppression-log noise
- lower Demo APP background noise for the current subset
- no observed regression of `BASE` and `PLUS` degraded-start control flow

It does not reopen the full-measurement product track and does not replace the
need for later Phase 4 power work.

## Phase 4 Current Progress Note

For the current delivery subset, Phase 4 has now completed a substantial
low-risk power pass.

That current Phase 4 progress specifically covers:

- moderate BLE transmit-power reduction
- staged advertising behavior:
  - fast discovery after boot/disconnect
  - lower-power idle advertising after the discovery window
- lower advertising-only TX power in the idle advertising profile
- reduced idle polling for no-laser / unavailable-measurement paths
- reduced idle wake frequency during unavailable-measurement backoff windows

Current interpretation:

- these changes are acceptable because they did not regress `BASE` or `PLUS`
  degraded-start delivery behavior on the current bench
- these changes are still only a partial Phase 4 result
- deep/light sleep policy remains outside the current delivery closure
