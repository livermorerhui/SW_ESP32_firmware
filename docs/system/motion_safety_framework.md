# Motion Safety Framework

## Purpose

This document defines the next-stage framework for SonicWave motion safety detection.

It does not hard-code final thresholds yet.

The goal is to establish:

- a stable motion safety state model
- modular leave-platform and fall detection structure
- a Demo APP debug sampling workflow
- a data-driven path from real motion samples to compact firmware runtime parameters

## Current Baseline

Current source of truth:

- firmware runtime safety:
  - `src/modules/laser/LaserModule.*`
  - `src/core/SystemStateMachine.*`
- Demo APP engineering console:
  - `tools/android_demo/app-demo/`

Current baseline behavior:

- laser directly measures distance / displacement
- firmware derives weight through the calibration model
- `USER_LEFT_PLATFORM` currently comes from derived-weight hysteresis and a falling-edge user-off trigger
- `FALL_SUSPECTED` can be raised from motion-safety abnormal evidence during `RUNNING`; recent PLUS normal testing observed the baseline-main abnormal-hold path rather than a BLE failure
- `SystemStateMachine` maps runtime safety reasons into the existing public contract:
  - `EVT:STATE`
  - `EVT:FAULT`
  - `EVT:SAFETY`

## Design Principles

### Compatibility first

- keep public top-level runtime states unchanged:
  - `IDLE`
  - `ARMED`
  - `RUNNING`
  - `FAULT_STOP`
- keep the public safety contract centered on:
  - `USER_LEFT_PLATFORM`
  - `FALL_SUSPECTED`
  - `RECOVERABLE_PAUSE`
  - `ABNORMAL_STOP`
  - `WARNING_ONLY`

### Internal modularity

Add a separate internal/debug-facing motion safety framework layer.

Recommended motion safety states:

- `EMPTY`
- `OCCUPIED_IDLE`
- `RUNNING_OCCUPIED`
- `LEAVE_CANDIDATE`
- `LEFT_PLATFORM`
- `FALL_CANDIDATE`
- `FALL_CONFIRMED`
- `RECOVERING`

This layer should be owned by runtime measurement and safety evaluation, not by the UI.

### Measurement truth

- direct sensor truth is distance / displacement
- weight is model-derived
- both are useful for safety analysis
- runtime firmware should remain compact
- heavier analysis belongs in the debug and offline-derivation path

### Debug-first derivation

Do not jump straight to guessed thresholds.

First:

1. define framework and schema
2. collect real data
3. label representative cases
4. derive thresholds and timing windows
5. land compact parameters into firmware

## Runtime Architecture

Recommended four-layer structure:

1. measurement input and validity
2. feature extraction
3. detector modules
4. action arbitration and output mapping

### Measurement input and validity

Inputs include:

- timestamp
- raw/direct distance or displacement
- sample validity
- current top state
- current wave/running state
- latest safety output

### Feature extraction

Features may include:

- `dd/dt`
- `dw/dt`
- rolling range
- rolling stddev
- short-window min/max
- threshold crossings
- candidate flags

Some of these should be recorded in debug sessions and some may be derived later offline.

### Detector modules

#### Leave-platform detector

Recommended phases:

- candidate
- confirmation
- hysteresis / recovery
- action output

Relevant evidence:

- derived weight drop
- direct distance/displacement change
- sample validity
- running state

Leave detection should be allowed to behave differently in idle and running conditions.

#### Fall detector

Recommended phases:

- candidate
- confirmation
- classification
- action output

Relevant evidence:

- abnormal short-window feature patterns
- repeated suspicious evidence
- relation to leave-platform evidence
- running/vibration context

Fall should not be treated as a one-frame spike detector in the future framework.

### Action arbitration and output mapping

The detector layer should still map back into the existing firmware-facing contract:

- `LEFT_PLATFORM` -> `USER_LEFT_PLATFORM` -> usually `RECOVERABLE_PAUSE`
- `FALL_CONFIRMED` -> `FALL_SUSPECTED` -> usually `ABNORMAL_STOP`

### Protection switch boundary

The runtime protection switch currently exposed as `DEBUG:FALL_STOP enabled=0/1` only controls whether a `FALL_SUSPECTED` candidate executes the stop action.

It must not be interpreted as a global safety bypass:

- `enabled=1`: `FALL_SUSPECTED` can execute the configured stop policy, currently abnormal stop
- `enabled=0`: `FALL_SUSPECTED` remains detectable and observable, but the action is suppressed to `WARNING_ONLY`
- `USER_LEFT_PLATFORM` remains independent and must still stop / pause the wave through the leave-platform path

Product meaning:

- disabling fall-stop protection is intended for lower-risk lower-limb-only use cases where a large rhythmic load shift can be acceptable
- leaving the platform is different: the runtime condition is no longer valid, so leave-platform auto-stop remains active

Audit rule:

- do not describe `DEBUG:FALL_STOP enabled=0` as “turning off rhythm safety”
- describe it as “suppressing fall-suspected auto-stop while keeping detection, warning, logs, and leave-platform stop”

## Demo APP Debug Sampling

The Demo APP should act as the engineering front end for motion data collection.

Recommended capabilities:

- start sampling
- stop sampling
- continuous time-series capture
- live chart review
- recent sample table review
- segment labeling
- point/event markers
- export session data

## MVP Landing In Demo APP

Task-MOTION-1 implements the first usable MVP of that debug-sampling direction inside:

- `tools/android_demo/app-demo/`

Current MVP scope:

- start / stop motion-sampling sessions explicitly
- keep the active session in app memory for immediate review
- record structured row-based time-series samples from the live stream
- review the captured session through:
  - session summary
  - recent row preview
  - basic session charts
- export the session as:
  - CSV row data
  - JSON metadata sidecar

Current MVP intentionally does not yet implement:

- full labeling workflow
- event-marker editing UI
- advanced derived-feature dashboards
- firmware-side motion safety parameter landing

That means the architecture path is now:

1. collect real sessions in the Demo APP MVP
2. inspect/export the data
3. derive leave/fall parameters from real samples
4. land compact parameters into firmware later

Primary scenarios to collect:

- idle standing
- normal vibration
- leave-platform
- abnormal/fall-like motion

## Sampling Record Model

Primary record format:

- row-based time-series table

Secondary format:

- charts over the same time-series

Recommended export:

- CSV for per-sample rows
- JSON for session metadata, labels, markers, and analysis context

Current MVP export shape:

- CSV is the primary artifact for time-series analysis
- JSON sidecar currently carries:
  - schema version
  - session metadata
  - app / device / protocol / model context
  - exported column list
  - reserved extensibility markers for labels and event markers

## Sampling-Mode Fall Suppression

Task-MOTION-1A adds a temporary engineering mode for data collection.

Purpose:

- keep fall detection computation alive
- keep fall-related visibility alive
- avoid repeated false fall stops from breaking long sampling sessions

This mode is explicitly limited to engineering/debug sampling use.

### Normal mode

When sampling mode is off:

- fall behavior stays unchanged
- `FALL_SUSPECTED` still follows the normal firmware stop/pause policy

### Sampling mode

When sampling mode is on:

- fall detection still runs
- fall observability is still emitted
- final fall-triggered stop/pause action is suppressed
- leave-platform behavior remains active

This means the framework now distinguishes between:

- fall detection/evidence
- fall action policy

which is important for collecting representative runtime datasets before the final modular fall detector is redesigned.

## Leave Action Closure

Audit-MOTION-1C clarified an important runtime rule:

- `RECOVERABLE_PAUSE` is not only a visible label
- when entered from `RUNNING`, it must also close the running waveform path immediately

The audited bug was that `USER_LEFT_PLATFORM` could reach `RECOVERABLE_PAUSE` while the state machine still remained in `RUNNING`, which left the app observing:

- reason=`USER_LEFT_PLATFORM`
- effect=`RECOVERABLE_PAUSE`
- state=`RUNNING`
- wave=`RUNNING`

until a later external stop command arrived.

The minimal fix reuses the internal `requestStop()` path when recoverable pause is entered from `RUNNING`, so leave-platform action closure is now:

1. leave detected
2. pause reason latched
3. running path closed automatically
4. non-running/stopped state propagated visibly

## Firmware Parameter Landing

The final runtime landing should use compact parameter groups such as:

- `LeaveDetectionParams`
- `FallDetectionParams`
- `MotionSafetyRuntimeControls`

These should contain thresholds, confirmation windows, hysteresis values, and runtime enable/profile controls, but not heavyweight offline analysis logic.

## Staged Direction

Recommended sequence:

1. define framework and record schema
2. extend Demo APP debug sampling
3. collect and label real motion data
4. derive parameter candidates
5. land modular leave logic in firmware
6. land modular fall logic in firmware
7. validate and refine with field data

## Related Documents

- `docs/system/fall_detection_logic.md`
- `docs/system/firmware_safety_behavior.md`
- `docs/system/task4_safety_contract.md`
- `reports/design_motion_safety_framework/`
