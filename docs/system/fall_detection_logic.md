# Fall Detection Logic

## Current Firmware Source of Truth

Current `FALL_SUSPECTED` ownership is split as follows:

- `LaserModule` decides when a fall-suspected condition exists
- `SystemStateMachine` decides how that condition maps to runtime effect

2026-04-26 review note:

- recent PLUS normal testing showed `FALL_SUSPECTED` can be reached through the baseline-main abnormal-hold path
- this was observed with double-feet-only contact causing a large relative derived-weight deviation while BLE remained connected
- therefore the latest symptom should not be treated as an APP connection failure first; the primary suspect is motion-safety classification / action policy

Legacy / direct trigger path:

1. `LaserModule::taskLoop()` reads distance roughly every 200 ms
2. firmware evaluates runtime weight from current distance
3. firmware computes a single-frame weight-change rate:
   - `rate = abs(weight - lastWeight) / dt`
4. if:
   - state is `RUNNING`
   - and `rate > FALL_DW_DT_SUSPECT_TH`
   then firmware calls `sm->onFallSuspected()`

Key points:

- current threshold is `FALL_DW_DT_SUSPECT_TH = 25.0f kg/s`
- current logic uses no debounce window
- current logic uses no multi-sample confirmation
- current logic does not require `USER_LEFT_PLATFORM`-like load-drop semantics
- current logic is based on model-derived weight rate, not a dedicated fall feature

Current baseline-main path:

1. firmware latches a stable baseline weight
2. runtime uses a moving-average derived weight
3. relative deviation is computed as `abs(ma_weight - stable_weight) / stable_weight`
4. if deviation leaves the safe band and does not recover within the recovery window, it can enter danger candidate
5. if the danger condition holds long enough, firmware emits a danger stop candidate such as `ABNORMAL_UNRECOVERED_HOLD_TIMEOUT`
6. `SystemStateMachine::decideFallSuspectedAction()` maps that candidate to `FALL_SUSPECTED` action semantics

This path explains why double-feet-only tests can trigger fall protection without any BLE disconnect:

- the user may still be present
- weight may not drop below leave threshold
- but the relative moving-average deviation can exceed the baseline-main abnormal window

## Distinction From USER_LEFT_PLATFORM

`USER_LEFT_PLATFORM` is driven by a different path:

- presence enters when `weight > MIN_WEIGHT` (`5.0 kg`)
- presence exits when `weight < LEAVE_TH` (`3.0 kg`)
- on the falling edge only, firmware calls `sm->onUserOff()`

Meaning:

- `USER_LEFT_PLATFORM` is hysteresis + edge based
- `FALL_SUSPECTED` is instantaneous rate based

This means normal vibration can keep weight above leave threshold, avoid `USER_LEFT_PLATFORM`, and still trip `FALL_SUSPECTED` if the derived weight changes fast enough between samples.

Protection switch boundary:

- `fall_stop_enabled=false` suppresses `FALL_SUSPECTED` stop action only
- it still allows fall-suspected detection and `WARNING_ONLY` visibility
- it does not suppress `USER_LEFT_PLATFORM`
- leave-platform auto-stop / recoverable pause must remain active even when fall-stop protection is disabled

## Why False Positives Are Plausible

Highest-confidence reason:

- current fall logic is too aggressive for a vibrating platform because it treats one large model-derived weight step as a fall suspicion

Contributing factors:

- only one rate threshold
- no consecutive-sample confirmation
- no running-vibration-specific tolerance
- uses derived weight rather than a more explicit fall pattern
- a 200 ms sample interval means a `> 5 kg` step already exceeds `25 kg/s`

## Minimal Safe Direction

Recommended minimal direction:

1. keep `USER_LEFT_PLATFORM` hysteresis path unchanged
2. keep `FALL_SUSPECTED` available only in `RUNNING`
3. add a short confirmation window for fall suspicion
4. require multiple suspicious samples or a combined pattern before latching
5. avoid classifying a single vibration-driven spike as a fall

This is safer than simply raising the threshold blindly, because it preserves sensitivity while reducing single-frame false positives.

## Motion Safety Framework Direction

This document now serves two purposes:

- preserve the current firmware baseline clearly
- point to the next-stage modular direction without pretending the final thresholds are already known

The recommended next step is not a blind threshold change. It is:

1. define a motion safety state framework first
2. modularize leave-platform detection
3. modularize fall detection
4. add Demo APP debug sampling so real motion data can be collected and labeled
5. derive compact runtime parameters from real samples
6. land those parameters into firmware in a parameterized form

## Recommended Separation

### Leave-platform path

Leave-platform should become a dedicated detector module with:

- candidate stage
- confirmation stage
- hysteresis / recovery stage
- action output stage

It should use multiple signals when available:

- direct distance / displacement
- model-derived weight
- sample validity
- running state

### Fall path

Fall should become a separate detector module with:

- candidate stage
- confirmation stage
- classification stage
- action output stage

It should not be treated as a single-frame spike rule. The future design should evaluate short-window evidence and distinguish:

- normal vibration spike
- leave-platform
- fall-like abnormal event

## Measurement Truth

Important architecture boundary:

- laser directly measures distance / displacement
- weight is derived through the deployed calibration model

This means future fall and leave analysis should be allowed to look at both:

- direct distance-side behavior
- derived weight-side behavior

instead of assuming one derived-weight threshold is always enough.

## Debug-First Parameter Derivation

The future threshold set should come from real motion samples rather than guessed constants.

Recommended workflow:

1. record representative debug sampling sessions in the Demo APP
2. label segments such as:
   - `静止站立`
   - `正常律动`
   - `离台`
   - `异常动作`
   - `疑似摔倒`
3. inspect tables and charts
4. derive candidate thresholds, windows, and hysteresis values
5. validate false positives / false negatives
6. land only the final compact parameters in firmware

See `docs/system/motion_safety_framework.md` for the full framework definition.

## Related Leave-Closure Note

Audit-MOTION-1C did not change leave detection thresholds.

It fixed the separate action-closure bug where `USER_LEFT_PLATFORM` could already be emitted with `RECOVERABLE_PAUSE`, but the running waveform path was not being closed immediately.

That fix was intentionally kept separate from fall logic:

- sampling mode still suppresses fall-triggered stop only
- leave-platform action closure still remains active

## Review Lesson

Do not infer safety behavior from the UI label alone.

For future audits, inspect all three layers before proposing changes:

1. firmware detector and `SystemStateMachine` action mapping
2. SW APP `SAFETY / STOP / FAULT` consumption
3. Demo APP debug control and export wording

The product phrase “关闭律动保护” must be expanded in engineering documents as:

- suppress `FALL_SUSPECTED` auto-stop
- keep detection and warning
- keep `USER_LEFT_PLATFORM` auto-stop
