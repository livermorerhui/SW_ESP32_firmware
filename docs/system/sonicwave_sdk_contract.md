# SonicWave SDK Contract

## Boundary Summary

The engineering demo in this repo uses the local modules under `tools/android_demo`:

- `sonicwave-protocol`
- `sonicwave-transport`
- `sonicwave-sdk`

These modules expose reusable device/protocol capability. They do not own product policy.

## Demo-Facing Capability Surface

`SonicWaveClient` is the demo-facing SDK entry point and exposes:

- BLE scan results
- connection state
- raw incoming lines / chunks
- transport logs
- parsed protocol events
- command send path
- capability probe / protocol-mode detection

## Event Contract

The local SDK/protocol stack now exposes these engineering-relevant events:

- `Event.State`
- `Event.Fault`
- `Event.Safety`
- `Event.StreamSample`
- `Event.Stable`
- calibration and capability events

`Event.Safety` carries:

- `reason`
- `code`
- `effect`
- `state`
- `wave`
- raw payload / extra fields

Unknown future events still decode to `null` and are ignored safely by the demo.

## Non-Goals

The SDK layer in this repo does not encode:

- SW APP training-state strategy
- paused vs abnormal-stop end-user UX
- auto-adjust policy
- customer attribution policy

Those belong above the SDK boundary.
