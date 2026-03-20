# Fall Detection Logic

## Current Firmware Source of Truth

Current `FALL_SUSPECTED` ownership is split as follows:

- `LaserModule` decides when a fall-suspected condition exists
- `SystemStateMachine` decides how that condition maps to runtime effect

Current trigger path:

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

## Distinction From USER_LEFT_PLATFORM

`USER_LEFT_PLATFORM` is driven by a different path:

- presence enters when `weight > MIN_WEIGHT` (`5.0 kg`)
- presence exits when `weight < LEAVE_TH` (`3.0 kg`)
- on the falling edge only, firmware calls `sm->onUserOff()`

Meaning:

- `USER_LEFT_PLATFORM` is hysteresis + edge based
- `FALL_SUSPECTED` is instantaneous rate based

This means normal vibration can keep weight above leave threshold, avoid `USER_LEFT_PLATFORM`, and still trip `FALL_SUSPECTED` if the derived weight changes fast enough between samples.

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
