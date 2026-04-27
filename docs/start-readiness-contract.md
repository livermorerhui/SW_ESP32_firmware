# Start Readiness Contract

## Purpose

This document fixes the start-button truth contract between firmware and the
Demo APP.

The goal is to keep the APP start gate aligned with firmware truth and avoid
blocking start because a reconnect-time runtime packet was delayed, oversized,
or partially lost.

## Truth Sources

### Bootstrap truth

Source:

- `ACK:CAP`

Used for:

- `platform_model`
- `laser_installed`

This layer decides whether the device is operating as a laserless profile.

### Runtime truth

Source:

- `SNAPSHOT`
- `EVT:BASELINE`
- dedicated ACK/event flows such as degraded-start confirmation

Used for:

- `start_ready`
- `baseline_ready`
- `laser_available`
- `degraded_start_available`
- `degraded_start_enabled`

## Start Button Rules

### `BASE`

`BASE` is a laserless profile.

Expected behavior:

- after connect, `Start` should be ready
- reconnect should not require rewriting the device profile

APP rule:

- `platform_model=BASE` is sufficient to treat the profile as laserless

### `PLUS` with `laser_installed=0`

This is not a supported product profile in the current product definition.

Expected behavior:

- it must not be documented as a normal delivery path
- the APP must not treat it as an officially supported laserless profile

APP rule:

- only `BASE` is treated as the formal laserless product profile

### `PLUS` with `laser_installed=1` and healthy measurement chain

Expected behavior:

- `Start` becomes ready only after firmware reports formal ready truth

APP rule:

- trust firmware `start_ready`
- do not re-derive readiness from local UI heuristics

### `PLUS` with `laser_installed=1` and measurement unavailable

Expected behavior:

- APP shows a repair/degraded-start prompt
- after degraded-start is available and confirmed, `Start` should be allowed

APP rule:

- rely on `degraded_start_available`
- require explicit degraded-start authorization before sending `WAVE:START`

## Non-Goals

The APP must not:

- require stable-weight UI data before honoring laserless start profiles
- require `SNAPSHOT.start_ready=true` for `BASE`

## Review Checklist

Before changing start-button behavior, verify:

- does the APP still trust firmware `start_ready` for laser-equipped profiles
- does `BASE` still become ready immediately after reconnect
- does degraded-start still require confirmation only for laser-equipped fault
  paths
- does connect-time `SNAPSHOT` remain small enough for the common single-notify
  MTU path

## StableContract / StartGate Owner Freeze

2026-04-27 audit result:

- `LaserModule` owns `StableContractState`.
- `SystemStateMachine` owns final `requestStart` allow/reject.
- APP / Demo APP consume firmware truth through `SNAPSHOT.start_ready`,
  `SNAPSHOT.baseline_ready`, and `EVT:BASELINE`; they must not re-derive
  ESP32 start readiness from local UI state.

Frozen internal meanings:

- `runtime_ready` means presence / user-present evidence.
- `start_ready` is the formal start gate for laser-equipped non-BASE profiles.
- `baseline_ready` is baseline evidence, not the only start/continue owner.
- `WAVE:START` must keep returning `NACK:NOT_ARMED` for unmet readiness and
  `NACK:FAULT_LOCKED` for blocking faults.

Safe next refactor cut:

- Extract only the pure start-gate decision from `syncStartReadyContract`.
- Keep `StableContractState`, baseline latch/clear, `setStartReadiness` timing,
  leave clear, rhythm reset, and effective-zero lock/unlock in `LaserModule`
  until a separate contract audit approves moving them.

Implementation note:

- `StartGateContractEvaluator` now owns only the pure decision and reason
  calculation.
- `LaserModule` still owns state write-back and when to call
  `SystemStateMachine::setStartReadiness`.
- This extraction does not change `SNAPSHOT`, `EVT:BASELINE`, or `WAVE:START`
  behavior.

Diagnostics note:

- `START_GATE_DIAG` is a serial-only observability line.
- It reports the current evaluator reason and a low-frequency reason-count
  window for `measurement_invalid`, `user_not_present`, `baseline_not_ready`,
  `live_stable_not_ready`, `running_contract_hold`, and `idle_contract_ready`.
- It must not become an APP / Demo APP protocol dependency.
- It does not change `start_ready`, `baseline_ready`, BLE wire format, or
  `WAVE:START` ACK/NACK behavior.

## Baseline / Presence Owner Freeze

2026-04-27 audit result:

- `LaserModule` still owns the timing relationship between presence, stable
  latch, baseline latch, occupied-cycle cleanup, and start-readiness write-back.
- `SystemStateMachine` still owns `runtime_ready`, `start_ready`, leave action,
  and final `WAVE:START` allow/reject.
- `RhythmStateJudge` mirrors accepted baseline evidence for motion safety
  evaluation only; it must not become a waveform stop/start action owner.

Frozen internal meanings:

- `userPresent` is occupied/user-on-platform evidence.
- `runtime_ready` mirrors presence only and must not be used as the formal
  laser-equipped start gate by APPs.
- `stableReadyLive` is live stable-window evidence.
- `baselineReadyLatched` is durable baseline truth for the current occupied
  cycle.
- `start_ready` is derived from measurement health, presence, and durable
  baseline truth.

Do not move these actions in the first baseline/presence extraction:

- `SystemStateMachine::onUserOff`
- `SystemStateMachine::setRuntimeReady`
- `SystemStateMachine::setStartReadiness`
- occupied-cycle `lock/release`
- effective-zero lock/unlock
- `RhythmStateJudge::refreshBaselineFromStable`
- `RhythmStateJudge::reset`
- baseline clear ordering after confirmed leave

Safe next refactor cut:

- Extract presence/baseline pure decision helpers only.
- Return next state, changed flag, and reason.
- Keep action timing and all BLE protocol output in the current owners until a
  separate action-owner audit approves moving them.

Implementation note:

- `PresenceContractEvaluator` owns only the pure presence decision:
  `nextUserPresent`, `changed`, and `reason`.
- `BaselineEvidenceEvaluator` owns only the pure stable-window and baseline
  eligibility decision: window readiness, threshold pass/fail, next confirm
  count, and baseline eligibility.
- `LaserModule` still owns enter/exit counters, `invalidPresenceSamples`,
  candidate entry/reset, stable latch execution, occupied-cycle lock/release,
  `SystemStateMachine::onUserOff`, `SystemStateMachine::setRuntimeReady`, and
  all downstream action timing.
