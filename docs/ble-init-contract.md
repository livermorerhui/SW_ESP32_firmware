# BLE Init Contract

## Purpose

This document fixes the responsibilities and boundaries of the BLE initialization flow between the ESP32 firmware and the Demo APP.

The goal is to keep connection bootstrap stable, short, and extensible, and to avoid reintroducing failures caused by oversized first-response packets.

## Responsibility Split

### `ACK:CAP`

`ACK:CAP` is reserved for bootstrap truth only.

It answers one question:

- What device is this?

Allowed fields:

- `fw`
- `proto`
- `platform_model`
- `laser_installed`

If compatibility requires temporary retention of additional fields, they must remain few, short, and explicitly reviewed.

### `SNAPSHOT`

`SNAPSHOT` is reserved for runtime truth.

It answers one question:

- What is the device's current runtime state?

Current connect-time/start-gate subset:

- `top_state`
- `runtime_ready`
- `start_ready`
- `baseline_ready`
- `platform_model`
- `laser_installed`
- `laser_available`
- `degraded_start_available`
- `degraded_start_enabled`

This subset is intentionally constrained to stay within the common
single-notify MTU path.

Fields that are useful but not allowed to push `SNAPSHOT` beyond the
single-frame safety budget include:

- `wave_output_active`
- `current_reason_code`
- `current_safety_effect`
- `stable_weight`
- `current_frequency`
- `current_intensity`
- `protection_degraded`

If richer runtime health needs to grow again, it must move to a dedicated
query/event plane rather than inflating connect-time `SNAPSHOT`.

## Prohibited Additions To `ACK:CAP`

The following classes of fields must not be added to `ACK:CAP`:

- runtime state
- readiness state
- peripheral health state
- mutable feature flags
- protection degradation state
- measurement-plane state
- future peripheral status fields

Examples of prohibited fields:

- `laser_available`
- `protection_degraded`
- `runtime_ready`
- `start_ready`
- `baseline_ready`
- `degraded_start_available`
- `degraded_start_enabled`
- `motion_sampling_mode`
- `fall_action_suppressed`
- `max485_available`
- `pcm5102_available`
- `temperature_sensor_available`

## APP Initialization Order

After BLE connection is established, the APP must initialize in this order:

1. Connect GATT.
2. Complete TX notify subscription.
3. Send `CAP?`.
4. Update bootstrap truth from `ACK:CAP`.
5. Send `SNAPSHOT?`.
6. Update runtime truth from `SNAPSHOT`.
7. Enter normal control and data flow.

`CAP?` must not be treated as the only initialization source.

## Device Profile Refresh Order

After every successful `DEVICE:SET_CONFIG`, the APP must refresh both layers:

1. `CAP?`
2. `SNAPSHOT?`

This ensures:

- bootstrap truth is updated
- runtime truth is updated

## First-Packet Size Rule

The first critical control response after connection must satisfy:

- preferably single-frame
- comfortably below `MTU - 3`
- not used as an extensible dump for future fields

Do not assume that a larger negotiated MTU makes an oversized bootstrap packet acceptable.

The same rule now applies to connect-time `SNAPSHOT` truth that the APP relies
on for start-gate decisions.

## Extension Rules

When adding new information:

- identity-level information belongs to `CAP`
- runtime health belongs to `SNAPSHOT`
- continuous changing data belongs to `EVT:*`
- command results belong to dedicated ACKs or events

Default rule:

- If a new field is not clearly bootstrap truth, it must not be added to `ACK:CAP`.

## Review Checklist

Before merging protocol changes, verify:

- whether `ACK:CAP` length increased
- whether runtime truth was pushed back into the bootstrap packet
- whether initialization again depends on a single oversized first response
- whether the `CAP? -> SNAPSHOT?` APP flow still holds

## Fixed Principle

`CAP` answers "what device is this".

`SNAPSHOT` answers "what is the device doing right now".
