# Phase 2 WP3 Non-Regression Verification

## Static Evidence Confirmation

Confirmed by code inspection:

- formal `start_ready` owner path remains in firmware
- `SystemStateMachine::requestStart()` gating logic was not redesigned
- session/export/duration paths were not modified
- leave auto-stop / recoverable pause owner path was not redesigned
- reconnect ready merge protection remains active in Demo APP
- this round only added one new semantic export field:
  - `EVT:BASELINE.stable_weight_active`

## Build And Test Results

Firmware:

- `~/.platformio/penv/bin/pio run`
- result: `SUCCESS`

Notes:

- plain `pio run` failed because `pio` is not on PATH in this shell
- one earlier `python3 -m platformio run` attempt failed during link because two PlatformIO builds were started in parallel against the same build directory
- rerunning the firmware build with a single PlatformIO invocation succeeded

Android demo/protocol:

- `./gradlew :sonicwave-protocol:test :app-demo:test`
- result: `BUILD SUCCESSFUL`

Covered test additions/updates:

- protocol parsing now checks `EVT:BASELINE.stable_weight_active`
- Demo start-ready regression tests now verify that held stable evidence can keep start available even when the live stable lamp is off

## Shortest Hardware Validation Recommendation

Recommended shortest real-device pass:

1. stand still until ready turns green
2. apply only light sway without stepping off
3. confirm:
   - start remains allowed
   - stable card may move from “active” to “held”, but does not imply lost readiness
4. start a run, then stop normally
5. confirm:
   - duration still records
   - current test session still completes
   - export still works
6. leave platform during run
7. confirm:
   - recoverable pause still occurs
   - wave output stops
   - re-entering the platform can recover to ready as before

## Signs This WP3 Convergence Is Still Not Successful

These observations would mean WP3 still needs more work:

- minor sway still frequently clears the live stable indicator almost immediately
- start button drops out of ready during mild sway while the user is still clearly on platform
- snapshot refresh turns held stable value into `0.0` while `baseline_ready` is still true
- Demo shows stable/live/ready combinations that are obviously contradictory to operators

## Signs This Round Broke An Already-Closed Chain

These would count as regressions:

- start remains blocked after previously validated stand-still flow
- stop no longer finalizes session summary/export
- reconnect loses already-known ready truth and falls back to snapshot-only weakness
- leave recoverable pause stops being recoverable or fails to stop wave output
- duration/current test session/export stop reflecting normal start/stop flows

## Verification Conclusion

Build/test evidence supports non-regression for the scoped WP3 changes.

What remains open is not a build-integrity problem but a field-parameter problem:

- movement tolerance still needs hardware feel confirmation
- dual-zero defaults still need measured traces before final freeze
