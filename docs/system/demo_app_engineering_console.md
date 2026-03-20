# Demo App Engineering Console

## Purpose

`tools/android_demo/app-demo` is the SonicWave engineering console for:

- firmware/debug validation
- protocol compatibility checks
- SDK capability verification

It is not the product app.

## Guaranteed Baseline

The demo is expected to preserve:

- connect / disconnect
- wave parameter set / start / stop
- live distance / weight telemetry
- stable weight visibility
- raw protocol and transport logging

## Engineering Visibility Added By Task-Demo-Align

The demo now shows:

- safety reason
- safety effect
- runtime state
- wave state
- warning-only vs recoverable-pause vs abnormal-stop meaning
- transport-derived BLE disconnect reminder

## Chinese Semantic Layer Added By Task-C

Task-C keeps the demo engineering-oriented but makes the main status surface easier to read in Chinese:

- Chinese meaning is primary for:
  - system state
  - fault
  - safety reason
  - effect
  - runtime state
  - wave state
- raw enum/code remains visible as secondary support text
- telemetry and calibration sections now include concise Chinese guidance

Key intent:

- easier for testers and maintainers to understand on first read
- still safe for protocol/debug work because raw code identity is not removed

## Out Of Scope

The demo intentionally does not become:

- SW APP product UX
- auto-adjust controller UI
- customer attribution workflow UI
- CH341-first control surface
