# Start Readiness Contract

## Purpose

This document fixes the start-button truth contract between firmware and the
Demo APP.

The goal is to keep the APP start gate aligned with firmware truth and avoid
blocking start because a reconnect-time runtime packet was delayed, oversized,
or partially lost.

## Truth Sources

### Bootstrap truth

Source:

- `ACK:CAP`

Used for:

- `platform_model`
- `laser_installed`

This layer decides whether the device is operating as a laserless profile.

### Runtime truth

Source:

- `SNAPSHOT`
- `EVT:BASELINE`
- dedicated ACK/event flows such as degraded-start confirmation

Used for:

- `start_ready`
- `baseline_ready`
- `laser_available`
- `degraded_start_available`
- `degraded_start_enabled`

## Start Button Rules

### `BASE`

`BASE` is a laserless profile.

Expected behavior:

- after connect, `Start` should be ready
- reconnect should not require rewriting the device profile

APP rule:

- `platform_model=BASE` is sufficient to treat the profile as laserless

### `PLUS` with `laser_installed=0`

This is not a supported product profile in the current product definition.

Expected behavior:

- it must not be documented as a normal delivery path
- the APP must not treat it as an officially supported laserless profile

APP rule:

- only `BASE` is treated as the formal laserless product profile

### `PLUS` with `laser_installed=1` and healthy measurement chain

Expected behavior:

- `Start` becomes ready only after firmware reports formal ready truth

APP rule:

- trust firmware `start_ready`
- do not re-derive readiness from local UI heuristics

### `PLUS` with `laser_installed=1` and measurement unavailable

Expected behavior:

- APP shows a repair/degraded-start prompt
- after degraded-start is available and confirmed, `Start` should be allowed

APP rule:

- rely on `degraded_start_available`
- require explicit degraded-start authorization before sending `WAVE:START`

## Non-Goals

The APP must not:

- require stable-weight UI data before honoring laserless start profiles
- require `SNAPSHOT.start_ready=true` for `BASE`

## Review Checklist

Before changing start-button behavior, verify:

- does the APP still trust firmware `start_ready` for laser-equipped profiles
- does `BASE` still become ready immediately after reconnect
- does degraded-start still require confirmation only for laser-equipped fault
  paths
- does connect-time `SNAPSHOT` remain small enough for the common single-notify
  MTU path
