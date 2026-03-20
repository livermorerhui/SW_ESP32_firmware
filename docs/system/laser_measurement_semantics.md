# Laser Measurement Semantics

## Summary

Laser Audit-1 documents the current firmware semantics for the laser path in the live worktree.

Current contract:

- Modbus register `REG_DISTANCE` is read as a 16-bit register
- firmware keeps both forms:
  - `uint16_t rawRegister`
  - `int16_t signedDistanceRaw`
- runtime distance is derived as `signedDistanceRaw / 100.0`
- sentinel handling runs before generic range checks
- only valid measurements can reach weight, stable, and stream logic

## Evidence Levels

Confirmed from code:

- the parser uses `int16_t`
- the runtime scaling divisor is `100.0`
- the laser stream path publishes the scaled signed value

Confirmed from repo artifacts:

- the default persisted zero baseline is negative (`-22.0`)
- protocol documentation includes a negative stream example
- calibration command examples accept a negative zero distance

Inferred from bench evidence:

- the device display behaves like a signed displacement range centered near zero
- the observed working display range is approximately `-35.70 .. +35.70`
- `32767` behaves like an over-range / invalid sentinel rather than a physical measured value

## End-to-End Path

```text
Modbus register read
-> rawRegister (uint16_t)
-> signedDistanceRaw (int16_t)
-> sentinel check
-> signed valid-range gate
-> scaled runtime distance = signedDistanceRaw / 100.0
-> calibration/weight evaluation
-> stable-state update
-> EVT:STREAM / EVT:STABLE
```

## Corrected Semantics

The live fix made in Laser Audit-1 establishes these rules:

- signed negative measurements are not inherently invalid
- `32767` is treated as a sentinel before generic range classification
- the valid working window is a signed raw range inferred from the observed device display:
  - `-3570 .. +3570`
- rate-limited diagnostics now expose raw, signed, scaled, and sentinel fields for bench confirmation

## Important Non-Changes

- BLE framing was not redesigned
- the weight model shape was not redesigned
- stable-state logic was not redesigned
- calibration workflow was not broadly redesigned

## Remaining Verification

- confirm on hardware that the true valid range is exactly `-35.70 .. +35.70` and not a nearby boundary
- confirm whether any additional sentinel values exist besides `32767`
- confirm whether legacy names such as `distanceMm` in calibration capture should be renamed in a future compatibility-managed cleanup
