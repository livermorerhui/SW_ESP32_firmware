# BASE / PLUS Degraded Regression Checklist

## Purpose

This checklist is the focused regression set for the current deliverable subset:

- `BASE`
- `PLUS + laser_installed=1 + measurement unavailable`

It exists to keep APP, firmware, and bench validation aligned with the current
delivery boundary:

- no MAX485 dependency
- no laser range sensor dependency
- no PCM5102A dependency
- no claim of full measurement-plane validation

Use this together with:

- `docs/system/base_plus_degraded_delivery_plan.md`
- `docs/system/base_plus_degraded_manual_validation.md`
- `docs/system/current_hardware_validation_boundary.md`
- `docs/ble-init-contract.md`
- `docs/start-readiness-contract.md`

## Bench Preconditions

Before each regression pass, verify:

- ESP32-S3 firmware is the intended build under test
- Demo APP is the intended build under test
- BLE advertising is visible
- current bench still has no MAX485 / no laser / no PCM5102A
- APP log buffer is cleared before the pass

Expected bench facts for this stage:

- repeated `Modbus read fail` is not itself a blocker
- no sustained valid measurement stream is expected
- absence of live distance / weight truth is not itself a blocker for this
  subset

## Shared BLE Bootstrap Checks

Run these checks for both `BASE` and `PLUS degraded-start`:

- connect succeeds without forced disconnect during initialization
- TX notify subscription completes before APP sends `CAP?`
- `CAP? -> ACK:CAP` succeeds
- `SNAPSHOT? -> SNAPSHOT` succeeds
- APP shows the correct `platform_model`
- APP shows the correct `laser_installed`
- reconnect repeats the same init order and still receives both truth packets
- no `ACK:CAP` truncation is observed
- no `SNAPSHOT` truncation is observed
- no MTU-budget warning related to bootstrap truth appears

## `BASE` Scenario Checks

Set or confirm:

- `platform_model=BASE`
- `laser_installed=0`

Verify:

- APP `Device Profile` allows `BASE + no laser`
- APP does not require any measurement-specific prompt before start
- `Start` becomes available after connect
- reconnect still restores `Start` availability without rewriting the profile
- `WAVE:SET` remains usable
- `WAVE:START` succeeds
- `WAVE:STOP` succeeds
- APP does not surface hidden stable-weight gating on this path
- APP keeps measurement / calibration tools out of the main flow for this
  profile

## `PLUS` Degraded-Start Scenario Checks

Set or confirm:

- `platform_model=PLUS`
- `laser_installed=1`
- `laser_available=0`
- `degraded_start_available=1`

Verify:

- APP `Device Profile` allows `PLUS + laser`
- APP write-back for `PLUS + laser` succeeds
- APP shows the degraded-start / repair-needed prompt
- degraded-start confirmation succeeds
- `Start` becomes available after degraded-start confirmation
- reconnect still restores the degraded-start path without rewriting the
  profile or corrupting reconnect-time truth
- `WAVE:SET` remains usable
- `WAVE:START` succeeds
- `WAVE:STOP` succeeds
- APP keeps measurement / calibration tools out of the main flow for this
  profile

## APP Copy And UI Checks

Verify:

- `Device Profile` text describes the current subset correctly
- unsupported model / laser combinations show the updated failure text
- `PLUS` degraded-start uses repair/authorization wording rather than no-laser
  wording
- delivery-boundary text clearly states that measurement / calibration is out
  of scope for this stage
- start-state hints remain consistent with firmware truth

## Not Blockers For This Stage

Do not fail this checklist only because:

- no valid `EVT:STREAM` arrives
- telemetry charts stay empty in `BASE` or `PLUS` degraded-start
- calibration tools are hidden for the current subset
- laser availability remains false
- measurement-chain hardware is absent

## Blockers For Current Delivery

Fail the subset regression if any of the following is observed:

- connect-time BLE mis-disconnect returns
- `ACK:CAP` or `SNAPSHOT` is missing after connect
- APP shows the wrong `platform_model` or `laser_installed`
- `BASE` start path is blocked after connect
- `PLUS` degraded-start path is blocked after connect
- reconnect loses start readiness for either supported subset
- `PLUS + laser` is treated as an invalid device-profile write
- APP reintroduces measurement-dependent gating into these two paths
- control commands become unreliable during this subset flow
