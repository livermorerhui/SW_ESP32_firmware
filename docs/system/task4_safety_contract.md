# Task-4 Safety Contract

## Source-of-Truth Summary

The firmware is aligned to the following Task-4 contract:

| reason | contract meaning | default product-visible result |
| --- | --- | --- |
| `USER_LEFT_PLATFORM` | recoverable interruption | paused |
| `FALL_SUSPECTED` | serious abnormality | abnormal stop |
| `BLE_DISCONNECTED` | reconnect reminder only by default | warning / transport disconnect |
| `MEASUREMENT_UNAVAILABLE` | warning only by default | warning |

## Firmware Alignment Rules

- firmware must expose explicit reason names
- firmware must expose enough APP-facing meaning to distinguish:
  - recoverable pause-like interruption
  - abnormal stop
  - warning-only degradation
- configurable product-policy points belong in firmware config, not hidden ad-hoc branches

## Final Firmware Mapping

- `USER_LEFT_PLATFORM` -> `EVT:SAFETY ... effect=RECOVERABLE_PAUSE`
- `FALL_SUSPECTED` -> `EVT:SAFETY ... effect=ABNORMAL_STOP`
- `BLE_DISCONNECTED` -> default warning policy, transport-visible first
- `MEASUREMENT_UNAVAILABLE` -> `EVT:SAFETY ... effect=WARNING_ONLY`

## Demo APP Engineering Consumption

The engineering demo in `tools/android_demo` now consumes the safety contract with this rule set:

- `EVT:SAFETY` is the preferred semantic signal for reason / effect / runtime / wave visibility
- transport disconnect is still treated as the primary observable signal for `BLE_DISCONNECTED` when no protocol safety line is available first
- baseline `EVT:STATE` and `EVT:FAULT` remain visible for compatibility and regression checking
- the demo remains an engineering console and does not implement SW APP product recovery flows
