# Phase 2 WP3 Convergence Update Report

## Scope

This round stayed inside the requested WP3 boundary:

- parameter convergence for pre-run stable feel
- state semantic refinement for `baseline_ready` / `start_ready` / `stable_weight_active`
- non-regression closure against the already-closed start/session/export chain

Changed files:

- `src/config/GlobalConfig.h`
- `src/config/LaserPhase2Config.h`
- `src/core/EventBus.h`
- `src/core/ProtocolCodec.h`
- `src/modules/laser/LaserModule.h`
- `src/modules/laser/LaserModule.cpp`
- `tools/android_demo/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/Model.kt`
- `tools/android_demo/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/ProtocolCodec.kt`
- `tools/android_demo/sonicwave-protocol/src/test/kotlin/com/sonicwave/protocol/ProtocolCodecTest.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/UiModels.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/StableWeightSection.kt`
- `tools/android_demo/app-demo/src/main/res/values/strings.xml`
- `tools/android_demo/app-demo/src/main/res/values-zh-rCN/strings.xml`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/DemoStartReadyRegressionTest.kt`
- `docs/demo_app_protocol.md`
- `docs/system/esp32_app_communication_semantics.md`

## Locked Problem Points

This focused audit intentionally did not reopen the already-closed `start_ready/session/export` branch. It locked onto these four WP3 issues instead:

1. `stable_ready_live` was still easier to clear than the intended user feel for “minor sway but still on platform”.
2. firmware already distinguished `baseline_ready_latched` from live stable internally, but protocol/UI still exposed an easy-to-misread combination.
3. Demo APP was using `baseline_ready` to keep `stableWeightActive` lit, which blurred:
   - live stable window active
   - latched baseline still held
   - start gate still formally ready
4. Snapshot merge behavior could overwrite a held baseline value with `0.0` because snapshot `stable_weight` is live-stable-shaped, not baseline-latched-shaped.

## Minimal Convergence Applied

### 1. Stable-exit parameter convergence

- kept existing leave, dual-zero, start-ready, and baseline ownership intact
- changed stable exit confirmation from one shared rule into two scoped rules:
  - leave exit confirm stays `2`
  - movement/delta exit confirm becomes `3`

Why this is worth doing:

- it targets the real WP3 symptom: light sway should not clear the live stable indicator too eagerly
- it does not weaken the formal `baseline_ready/start_ready` owner path
- it keeps leave handling faster than generic movement wobble handling

### 2. State semantic refinement

Protocol/export now distinguishes:

- `baseline_ready`
  - baseline has been established and retained for the current occupied cycle
- `stable_weight_active`
  - live stable window is currently active
- `start_ready`
  - formal firmware-owned pre-start gate truth

Implementation detail:

- firmware now exports `stable_weight_active=<0|1>` on recurring `EVT:BASELINE`
- Demo protocol model and parser consume that field
- Demo UI now treats:
  - `stableWeightActive` as live stable
  - `stableWeight` as held baseline evidence when available
  - start readiness as `deviceStartReady + deviceBaselineReady + stable-weight evidence`, not “live stable lamp must currently be on”

### 3. Snapshot merge hardening

- Demo snapshot merge no longer lets `SNAPSHOT.stable_weight=0.0` wipe an already-known held baseline value while `baseline_ready` remains true
- snapshot still updates live-stable activity truth
- this keeps reconnect/control refresh aligned with the formal contract instead of accidentally degrading it

### 4. UI wording refinement

- stable card now has three meanings instead of two:
  - live stable active
  - baseline held but live stable inactive
  - waiting for next stable event

This removes the previous “value exists but meaning unclear” presentation.

## Why These Changes Stay Safe

- `SystemStateMachine::requestStart()` and formal start gate owner logic were not redesigned.
- stop/session/export chain was not reopened.
- reconnect restore logic was not relaxed; snapshot/start-ready merge remains protective.
- leave auto-stop / recoverable pause logic was not redesigned.
- duration/current test session/export paths were untouched.

## Impact On Already-Normal Chains

Expected impact on already-closed paths:

- `start/stop confirmation`: no contract regression; start still requires firmware-owned `start_ready`
- `reconnect restore`: preserved and slightly clarified by better snapshot merge behavior
- `leave auto-stop / recoverable pause`: preserved; movement wobble filtering does not change leave owner
- `duration`: untouched
- `current test session capture`: untouched structurally
- `export`: untouched structurally
- `ready recovery contract`: preserved; no return to snapshot-only truth

## Recommendation

This WP3 round meaningfully improved Phase 2 clarity and reduced one important false-instability surface without expanding blast radius.

However, some parameters still need real-device data before calling the entire Phase 2 base fully closed:

- dual-zero refresh thresholds
- stable movement exit sensitivity under real users with different stance styles
- leave vs near-leave feel around the `3 kg` boundary

So the correct decision is not `Yes`, but `Partial`.

Decision:
Current Phase: Phase 2
Current Work Package: WP3
Convergence Status: Partial
State Semantics Status: Improved and materially clearer; `baseline_ready`, `stable_weight_active`, and `start_ready` are now explicitly separated in firmware export and Demo consumption.
Non-Regression Status: Passed on firmware build and Android protocol/app test/build verification; closed start/session/export chain was not reopened.
Can Treat Phase 2 Base As Closed: Partial
Blocked By: Final hardware-side confirmation of minor-sway tolerance, leave feel, and dual-zero defaults.
Open Risks: Movement-exit confirmation is now intentionally less twitchy, but the exact “best feel” still lacks measured hardware traces across more users.
Registered Debts: No dedicated canonical ready/stable-state event yet; `stable_weight_active` is currently exported as an additive `EVT:BASELINE` field rather than a new standalone plane.
Why this decision applies: The code now better matches the intended Phase 2 semantics and passes non-regression builds/tests, but the remaining unresolved items are parameter-quality questions that require hardware evidence rather than more speculative code changes.
