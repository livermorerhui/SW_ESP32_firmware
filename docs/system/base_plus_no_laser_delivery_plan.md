# BASE / PLUS+No-Laser Delivery Plan

## Purpose

This document defines the deliverable subset that can be completed now without
waiting for MAX485, PCM5102A, or the laser range sensor.

The intent is to cut scope cleanly and deliver a stable subset for:

- `BASE`
- `PLUS + no laser installed`

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

### `PLUS + no laser installed`

Expected behavior:

- `platform_model=PLUS`
- `laser_installed=0`
- Demo APP explicitly indicates no-laser mode
- start button remains available
- no stable-weight requirement
- no laser availability requirement
- `WAVE:SET / WAVE:START / WAVE:STOP` remain usable

## Explicitly Out Of Scope For This Stage

The following items are not blockers for the current subset delivery:

- real laser distance measurement
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
- `PLUS + no laser installed` truth must remain sufficient for immediate start
  availability
- current BLE control-priority behavior must remain intact

## Demo APP Contract For This Subset

The Demo APP must keep the current initialization order:

1. connect
2. subscribe notify
3. `CAP?`
4. `SNAPSHOT?`

For this subset specifically:

- `BASE` must not be blocked by stable-weight assumptions
- `PLUS + no laser installed` must not be blocked by laser-required assumptions
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

### `PLUS + no laser installed`

The subset is acceptable only if all of the following remain true:

- connect / reconnect is stable
- `ACK:CAP` is received reliably
- `SNAPSHOT` is received reliably
- the APP clearly indicates no-laser mode
- start button remains available
- `WAVE:SET / START / STOP` remain stable
- reconnect does not regress the no-laser start path

### Shared BLE / Truth Criteria

The subset is not acceptable if any of the following reappear:

- `ACK:CAP` truncation
- `SNAPSHOT` truncation
- MTU-budget warnings for truth packets
- reconnect-time truth loss
- start-button truth drifting away from firmware truth
- BLE mis-disconnect caused by initialization timing

## Implementation Priorities

### Now

The current priorities for this subset are:

1. keep the BLE initialization contract frozen
2. keep truth packet budgets frozen
3. keep Demo APP start-readiness behavior frozen for `BASE` and
   `PLUS + no laser installed`
4. avoid reintroducing measurement-dependent assumptions into these two paths

### Later

The following belongs to the later measurement-capable product track:

- `PLUS + laser installed + healthy measurement chain`
- MAX485-backed measurement path
- sustained valid `EVT:STREAM`
- calibration and full measurement closed loop

## Release Boundary Statement

The current stage should be described as:

- a stable BLE-controlled subset for `BASE` and `PLUS + no laser installed`
- not the final full-measurement product

That boundary must be kept explicit in demos, testing notes, and future task
planning.
