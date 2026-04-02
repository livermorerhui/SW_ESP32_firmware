# Phase 2 WP3 Parameter And State Convergence

## Convergence Summary

### Q1. Which parameters now look reasonable, and which still do not?

Reasonably converged for this phase:

- `start_ready` hold contract
  - already validated previously
  - not reopened here
- `baseline_ready` latching semantics
  - durable across live-stable drops until occupied-cycle clear
- stable leave clear confirmation
  - kept at `2`
  - still responsive for actual off-platform exit
- stable movement clear confirmation
  - now intentionally stricter at `3`
  - better matched to “minor sway is not leave”

Still not safe to hard-freeze without more hardware data:

- dual-zero refresh window thresholds
  - `refreshStdDevDistance`
  - `refreshRangeDistance`
  - `driftGuardDistance`
- stable movement delta magnitudes
  - `exitWeightDeltaKg = 0.5`
  - `exitDistanceDelta = 1.0`
- presence edge feel near `MIN_WEIGHT` / `LEAVE_TH`

### Q2. Is the system too easy to drop out of stable/ready during light sway?

Before this round:

- live stable could clear on only `2` confirmed movement-exit evaluations
- UI also made that feel worse by conflating live stable with baseline-held/ready semantics

After this round:

- light sway is less likely to clear live stable immediately because movement exits require `3` confirmations
- even if live stable drops, `start_ready` and held baseline semantics remain clearer in the app

### Q3. Do leave detection, recoverable pause, and ready recovery still feel natural?

Structurally yes:

- leave detection owner and threshold path were not redesigned
- recoverable pause path was not rewritten
- ready recovery contract remains firmware-owned

What still needs hardware confirmation:

- whether the retained `leave_threshold` / presence behavior feels natural on the exact platform and user posture mix

### Q4. Are `baseline_ready`, `start_ready`, `stable_weight`, and `stable evidence` now clear enough?

Clearer than before:

- `baseline_ready`
  - durable “baseline retained” truth
- `start_ready`
  - formal pre-start gate truth
- `stable_weight_active`
  - current live stable window truth
- `stable_weight`
  - held baseline evidence value when available

This is the main semantic refinement of WP3.

### Q5. Did Demo APP still have easy-to-misread state combinations?

Yes before this round:

- Demo could show stable as active purely because `baseline_ready` stayed true
- a snapshot could also zero out the visible stable value while the baseline still existed

Now:

- live stable activity is driven by `stable_weight_active`
- held baseline value is preserved across snapshot merges when `baseline_ready` still holds
- stable card copy explicitly distinguishes:
  - active live stable
  - baseline held but live stable inactive

### Q6. Which Phase 2 parameters should converge now vs later?

Converged now:

- stable movement exit confirmation count
- stable leave exit confirmation separation
- protocol/app semantic split for `stable_weight_active`

Leave for later measured tuning:

- dual-zero numeric thresholds
- movement delta magnitudes
- leave threshold feel around edge cases
- any stronger smoothing or trimmed/median stable value computation

### Q7. What were the highest-value minimal improvements?

The highest-value minimal improvements were:

1. separate stable clear confirmation for leave vs movement
2. export `stable_weight_active` without changing formal ready ownership
3. make Demo start gate depend on held stable evidence instead of the live stable lamp
4. stop snapshot truth refresh from clobbering held baseline evidence

## Parameters Adjusted

Adjusted in code:

- `STABLE_EXIT_CONFIRM_SAMPLES_LEAVE = 2`
- `STABLE_EXIT_CONFIRM_SAMPLES_MOVEMENT = 3`

Mapped into:

- `LaserStableThresholdConfig.exitLeaveConfirmSamples`
- `LaserStableThresholdConfig.exitMovementConfirmSamples`

No numeric changes were made to:

- `LEAVE_TH`
- `MIN_WEIGHT`
- `STABLE_REARM_WEIGHT_DELTA_TH`
- `STABLE_REARM_DISTANCE_DELTA_TH`
- dual-zero thresholds

That was intentional to avoid speculative over-tuning.

## State Semantics Refined

Refined semantics:

- `baseline_ready`
  - current occupied cycle has a retained accepted baseline
- `start_ready`
  - firmware says start is formally allowed now
- `stable_weight_active`
  - live stable window currently active
- `stable_weight`
  - current/held stable baseline value, when available
- `ARMED`
  - top-state visibility only; not the owner of ready semantics
- `RUNNING`
  - top-state visibility only; not proof that safety/wave/ready meanings are identical

## Items That Should Not Be Hard-Fixed Yet

Do not hard-freeze yet:

- dual-zero refresh tuning
- exact sway tolerance around movement delta exits
- exact leave feel across different body weights/platform contacts
- any deeper danger-vs-leave threshold separation

Reason:

- current code evidence supports semantic cleanup and minimal de-twitching
- it does not support pretending the remaining numeric thresholds are already field-proven
