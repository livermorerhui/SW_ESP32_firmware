# SonicWave System Architecture

## Task-4A Safety View

Relevant firmware path:

```text
LaserModule / BleTransport
    -> SystemStateMachine
    -> EventBus
    -> BleTransport
    -> tools/android_demo/sonicwave-sdk
    -> tools/android_demo/app-demo
```

## Safety Responsibilities

- `LaserModule`
  - reads the laser Modbus register as a signed fixed-point measurement
  - applies sentinel detection and validity gating before weight / stable / stream logic
  - produces measurement-health, user-presence, and fall-suspected inputs
- `BleTransport`
  - produces BLE session connect / disconnect inputs
- `SystemStateMachine`
  - is the only wave gate
  - chooses whether a reason is warning-only, recoverable-pause-like, or abnormal-stop-like
  - emits `EVT:STATE`, `EVT:FAULT`, and `EVT:SAFETY`
- `WaveModule`
  - starts or stops only through the state machine
- `tools/android_demo/sonicwave-protocol`
  - decodes canonical command/event framing for the engineering demo
  - now understands `EVT:SAFETY` in addition to `EVT:STATE`, `EVT:FAULT`, `EVT:STREAM`, and `EVT:STABLE`
- `tools/android_demo/sonicwave-sdk`
  - exposes connection state, raw lines, and parsed protocol events to the engineering console
- `tools/android_demo/app-demo`
  - remains an engineering/debug console
  - surfaces safety reason / effect / runtime state / wave state without importing SW APP product policy layers

## Architecture Decision

Task-4A intentionally does not redesign:

- DDS / I2S path
- BLE framing
- laser measurement pipeline
- top-level state enum

Instead, it adds a semantic safety layer on top of the existing runtime path.

## Laser Measurement Semantics

Current audited laser path:

```text
Modbus register 0x0064
    -> uint16_t rawRegister
    -> int16_t signedDistanceRaw
    -> sentinel check (32767)
    -> signed valid-range check
    -> scaled runtime distance = signedDistanceRaw / 100.0
    -> weight model
    -> stable logic
    -> EVT:STREAM / EVT:STABLE
```

Important architecture note:

- the laser path is now explicitly documented as signed-value-first
- sentinel handling happens before generic range gating
- invalid measurement classification exits the laser task before stream / stable / weight publication
- see `docs/system/laser_measurement_semantics.md` for the detailed semantics contract

## Audit Note

Laser Audit-1 confirmed that the previous local validity gate conflicted with repo and bench evidence by assuming a positive-only range and by comparing unscaled `65..135` constants against the raw fixed-point register value. That regression has been corrected in the laser module only, without changing BLE or higher-level product behavior.

## Demo Alignment Note

Task-Demo-Align keeps the engineering console separate from SW APP product strategy:

- firmware owns protocol semantics
- local demo SDK exposes reusable device/protocol capability
- demo UI shows engineering visibility only
- product pause/abnormal-stop UX, auto-adjust behavior, and attribution policy remain outside the demo boundary
