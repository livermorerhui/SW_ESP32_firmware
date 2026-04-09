# BASE / PLUS Degraded Manual Validation

## Purpose

This document gives the operator-facing step-by-step validation flow for the
current deliverable subset:

- `BASE`
- `PLUS + laser installed + measurement unavailable`

Use this when running a real bench regression with the Android Demo APP.

For scope and pass/fail criteria, also see:

- `docs/system/base_plus_degraded_delivery_plan.md`
- `docs/system/base_plus_degraded_regression_checklist.md`
- `docs/system/current_hardware_validation_boundary.md`

## Test Preconditions

Before starting, confirm:

- the intended firmware build is flashed
- the intended Android Demo APP build is installed
- the ESP32-S3 bench is powered and advertising
- the current bench still has no healthy laser / MAX485 / PCM5102A path
- Android Bluetooth permissions are granted
- APP Raw Console is cleared before each scenario

Expected bench facts in this stage:

- repeated `Modbus read fail` is expected
- `laser_available=0` is expected for the degraded path
- no sustained valid `EVT:STREAM` is expected

## Shared Connect Validation

Run these checks after every fresh connect:

1. Open the Demo APP and tap `Search & Connect`.
2. Select the target BLE device and wait for connect completion.
3. Confirm notify setup succeeds.
4. Confirm the APP sends `CAP?`.
5. Confirm the device returns:
   `ACK:CAP fw=... proto=... platform_model=... laser_installed=...`
6. Confirm the APP sends `SNAPSHOT?`.
7. Confirm the device returns:
   `SNAPSHOT: top_state=... runtime_ready=... start_ready=... baseline_ready=... platform_model=... laser_installed=... laser_available=... degraded_start_available=... degraded_start_enabled=...`

Fail immediately if:

- connect-time disconnect reappears
- `ACK:CAP` is missing
- `SNAPSHOT` is missing
- `ACK:CAP` again contains non-bootstrap fields such as `fall_stop_*`

## Scenario A: `BASE`

### Setup

1. Connect to the device.
2. Open `Device Profile`.
3. Set:
   - `platform_model=BASE`
   - `laser_installed=0`
4. Write the profile.
5. Wait for the APP refresh sequence:
   - `CAP?`
   - `SNAPSHOT?`

### Expected APP State

Confirm:

- the APP shows `MODEL=BASE`
- the APP shows `LASER=no laser`
- the main page shows `Current Delivery Boundary`
- the profile line shows `Current profile: BASE` or the Chinese equivalent
- no degraded-start dialog is shown
- `Start` is available
- measurement chart is hidden
- calibration tools are hidden
- test session panel is hidden

### Control Validation

Run:

1. Send `WAVE:SET f=20,i=80`
2. Tap `START`
3. Confirm `EVT:WAVE_OUTPUT active=1` or equivalent running truth
4. Tap `STOP`
5. Confirm stop truth returns to inactive / armed

Repeat at least 3 cycles while varying presets:

- `20 / 80`
- `30 / 60`
- `40 / 100`

### Reconnect Validation

1. Disconnect the BLE session.
2. Reconnect to the same device.
3. Re-run the shared connect validation.
4. Confirm `BASE` truth is restored without rewriting the profile.
5. Confirm `Start` is still available immediately.

## Scenario B: `PLUS` Degraded-Start

### Setup

1. Connect to the device.
2. Open `Device Profile`.
3. Set:
   - `platform_model=PLUS`
   - `laser_installed=1`
4. Write the profile.
5. Wait for the APP refresh sequence:
   - `CAP?`
   - `SNAPSHOT?`

### Expected Pre-Authorization State

Confirm:

- the APP shows `MODEL=PLUS`
- the APP shows `LASER=laser installed`
- `SNAPSHOT` reports:
  - `laser_available=0`
  - `degraded_start_available=1`
  - `degraded_start_enabled=0`
- the APP shows the degraded-start / repair-needed dialog
- `Start` is not treated as immediately available before degraded-start

### Degraded Authorization Validation

1. Confirm the APP sends:
   `DEBUG:DEGRADED_START enabled=1`
2. Confirm the device returns:
   `ACK:DEGRADED_START enabled=1 available=1`
3. Confirm the APP refreshes `SNAPSHOT`.
4. Confirm refreshed runtime truth includes:
   - `runtime_ready=1`
   - `start_ready=1`
   - `degraded_start_enabled=1`

### Control Validation

Run:

1. Send `WAVE:SET`
2. Tap `START`
3. Confirm running truth arrives
4. Tap `STOP`
5. Confirm stop truth arrives

Repeat at least 3 cycles while varying presets:

- `20 / 100`
- `30 / 80`
- `40 / 60`

During this pass, it is acceptable to continue seeing:

- `Modbus read fail`
- `MEASUREMENT_UNAVAILABLE`
- invalid `EVT:STREAM` carrier samples

### Reconnect Validation

1. Disconnect the BLE session.
2. Reconnect to the same device.
3. Re-run the shared connect validation.
4. Confirm reconnect-time truth returns to:
   - `platform_model=PLUS`
   - `laser_installed=1`
   - `laser_available=0`
   - `degraded_start_available=1`
   - `degraded_start_enabled=0`
5. Confirm degraded-start can be authorized again.
6. Confirm the APP does not drift into `BASE` wording or no-laser wording.

## PASS / FAIL Recording

Record each scenario with:

- firmware identifier
- APP build identifier
- date/time
- scenario name
- whether connect succeeded
- whether `ACK:CAP` succeeded
- whether `SNAPSHOT` succeeded
- whether start became available at the correct time
- whether 3 start/stop cycles passed
- whether reconnect preserved the expected truth path
- any unexpected APP wording or UI branch

## Current High-Signal Failure Clues

If the run fails, capture:

- the exact `ACK:CAP` line
- the first `SNAPSHOT` after connect
- the `ACK:DEGRADED_START` line if applicable
- the first failing `WAVE:*` command around the failure
- the nearest `EVT:STATE`, `EVT:WAVE_OUTPUT`, and `EVT:STOP`
- the APP-visible error text

## Not Blockers In This Stage

Do not fail the current subset only because:

- valid measurement data never appears
- telemetry charts remain hidden
- calibration tools remain hidden
- `Modbus read fail` keeps repeating
- `MEASUREMENT_UNAVAILABLE` remains visible on the degraded path
