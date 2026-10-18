# ESP32 Autonomous Training Session Contract (Historical Proposal; Not Applicable)

状态：历史提案；不代表当前固件合同、产品目标或施工授权
文档类型：历史 firmware / BLE 架构讨论
适用范围：仅用于理解曾讨论过的 DEVICE_OWNED_SESSION 方案
Owner：无当前实现 Owner
更新日期：2026-10-15（状态标注修正）
当前产品真相源：[SW APP 与 ESP32 会话执行模型](../../../SW/docs/system/安卓APP与ESP32律动会话执行模型说明.md)
当前专题施工入口：[手动律动实时调参蓝图](../../../SW/docs/system/安卓APP手动律动实时调参不中断施工蓝图-20261015.md)
证据源：现有 protocol 代码、[SW 倒计时后台调度审计](../../../SW/reports/task_20260718_android_countdown_background_execution_audit.md)

> 用户已确认 APP 持有会话生命周期、倒计时和方案步骤调度；APP 下发参数，ESP32执行输出并保留独立安全判断。ESP32不持有律动会话。以下章节仅记录历史方案，不得作为当前固件实现依据或施工授权。

## 1. Original proposal purpose

The historical proposal described DEVICE_OWNED_SESSION as an ESP32-owned training timeline, with Android/iOS submitting plans and rendering device-reported facts.

The current product contract remains APP_DRIVEN_SESSION: the APP owns session lifecycle, countdown, and plan scheduling; ESP32 executes APP parameters and keeps its independent safety behavior. The historical proposal was not adopted and does not authorize a firmware runtime, capability gate, or protocol change.

## 2. Historical capability and compatibility gate proposal

The proposal required new firmware to expose capability fields only after a session runtime became available:

```text
ACK:CAP ... session_execution_model=DEVICE_OWNED_SESSION session_runtime_version=1 plan_upload_supported=1
```

Under that proposal, new App behavior would require all three values to be present and supported. Otherwise the App would remain on the APP_DRIVEN_SESSION path.

## 3. Plan ownership and validation

The App compiles the final execution plan before upload. ESP32 does not infer product steps from a visible plan name.

Each normalized step contains:

```text
step_index
frequency_hz
base_intensity         # 1..120
duration_ms
```

The session plan also contains `session_id`, `plan_revision`, `plan_hash`, `mode`, and `total_duration_ms`. Only `EXPERT` plans carry initial `intensity_scale_pct` (`0..100`); `MANUAL` and `CUSTOM` plans use their fixed step intensities. Expert plan compilation always starts at template step zero: when total duration is shorter than one cycle, the step active at the total deadline is cut; when it is longer, the template loops from step zero until the same final cut. The sum of all compiled step durations must equal `total_duration_ms` exactly.

The firmware validates id/revision uniqueness, step count, duration, frequency, base intensity, complete hash, and safety/start gate before accepting a plan. No partial plan may start output.

## 4. Plan transfer

The exact text serialization may evolve only through `session_runtime_version`; transfer semantics are fixed:

```text
SESSION:PLAN_BEGIN sid=<id> rev=<rev> mode=<MANUAL|EXPERT|CUSTOM> total_ms=<ms> steps=<n> [scale_pct=<0..100 for EXPERT>]
SESSION:PLAN_CHUNK sid=<id> rev=<rev> index=<0..n-1> count=<n> payload=<encoded steps>
SESSION:PLAN_COMMIT sid=<id> rev=<rev> sha256=<64 hex>
ACK:SESSION_PLAN sid=<id> rev=<rev> accepted=<0|1> reason=<CODE>
SESSION:START sid=<id> rev=<rev>
```

Chunking is mandatory for plans that do not fit one BLE write. Each `PLAN_CHUNK` must stay within the current `BleTransport::ControlMsg::line[128]` input limit. The complete SHA-256 belongs only to `PLAN_COMMIT`; including it in `PLAN_BEGIN` would exceed the legacy RX budget for common identifiers. ESP32 must acknowledge rejection with a stable reason, discard incomplete/invalid transfers, and never begin execution before successful commit.

## 5. Runtime semantics

### 5.1 Manual

The plan contains one step. ESP32 applies the accepted frequency and intensity, owns the total deadline using a monotonic timer, and stops output at expiry.

### 5.2 Expert and custom

The plan contains the complete sequence already expanded/cut by the App. ESP32 advances `step_index` from its own monotonic timer and applies every step without waiting for the App.

### 5.3 Intensity scale

`base_intensity` is always `1..120`. Only EXPERT plans use `intensity_scale_pct` (`0..100`); ESP32 computes their actual output as `floor(base_intensity * intensity_scale_pct / 100)`, clamped to `0..120`. Therefore `0%` formally produces actual output intensity `0`.

`SESSION:SET_SCALE sid=<id> rev=<rev> scale_pct=<0..100>` immediately recalculates the current step's actual intensity and applies it to the wave output. All remaining steps use the same new scale. Firmware then publishes a full `EVT:SESSION` state with the new applied intensity; `0%` means output intensity `0` while the session timeline continues.

### 5.4 Terminal priority

ESP32 must stop output before publishing a terminal event. Safety/fault terminal behavior keeps its existing upstream semantics and outranks normal countdown completion. `COUNTDOWN_COMPLETE` is used only when the device deadline wins.

## 6. Reported runtime truth

Do not enlarge legacy connect-time `SNAPSHOT` until its single-notify budget is re-audited. Add a versioned session runtime truth frame that fits one normal notification:

```text
EVT:SESSION sid=<id> rev=<rev> seq=<n> state=<READY|RUNNING|PAUSED|STOPPED|FAULT_STOP> step=<index> f=<applied_hz> i=<applied_intensity> scale=<pct> step_remaining_ms=<ms> total_remaining_ms=<ms> wave=<0|1> terminal_reason=<CODE>
```

- This is one complete state frame. Do not send separate frequency, intensity, step, and time frames for one state transition.
- Emit immediately after plan acceptance, start, step change, pause, resume, scale-effective change, terminal transition, and output confirmation.
- While `RUNNING`, emit one complete state frame at the initial maximum rate of `1Hz` when a BLE client is subscribed.
- `seq` increases inside one `session_id`; older or duplicate frames are valid to transport but must be ignored by consumers.
- The reconnect query `SESSION?` returns the latest full session state. This is authoritative after reconnect and replaces stale App cache.
- A disconnected client receives no historical events. On reconnect, fresh `SESSION?` state, not retained Android data, restores presentation.

The runtime must answer `SESSION?` while a session is `RUNNING`, `PAUSED`, or terminal. It returns either the complete current state or an explicit no-active-session state; it must never leave the App to infer missing frequency, intensity, step, or remaining time from a prior session. The longest `EVT:SESSION` frame must be measured against `ProtocolCodec::kSingleNotifyPayloadBudget` before the wire format is frozen; it must not rely on implicit BLE fragmentation.

## 7. Disconnect, reset, and clear rules

For this model, BLE disconnect does not pause or terminate an accepted running session. The device continues time, steps, safety detection, and terminal processing.

The product permits only one connected mobile client. The device-owned runtime must preserve this existing single-client BLE boundary; it must not add a second read-only mobile connection during an active session.

Explicit no-active-session truth must contain `session_active=0` (or an equivalent terminal state) and clear id, step, remaining-time, pending-scale, and terminal-event fields. Firmware reboot, failed plan commit, or protocol incompatibility must reject continuation; no auto-resume across firmware reset is allowed.

## 8. Android/iOS consumption boundary

Clients display only firmware reported `f`, `i`, step and remaining time. They may use local monotonic time only to smooth the displayed remaining seconds between `EVT:SESSION` frames. Local prediction never advances a device step, stops output, or overwrites a newer firmware terminal state.

For a parameter or scale command, the client may show a pending state but must switch its main displayed value only after the matching reported session state confirms application.

When BLE is disconnected, clients may retain the last frame only as non-live context: they immediately show a disconnect notice and grey out frequency, intensity, time and step values. On foreground return or reconnect, clients must call `SESSION?` before restoring active styling or presenting an active session as current.

## 9. Verification

Firmware tests and capture must cover:

1. incomplete/chunk-corrupt/hash-mismatch plan never starts;
2. compiled expert total duration is executed exactly;
3. expert scale update immediately changes the current actual output and reports it, including `0% -> actual intensity 0` while the timeline continues;
4. backgrounded/killed App does not alter device countdown;
5. BLE disconnect -> device expiry -> reconnect `SESSION?` is consistent;
6. same terminal event is idempotent and a new session is not suppressed;
7. safety-vs-countdown race keeps the actual device terminal reason;
8. legacy clients remain on `APP_DRIVEN_SESSION` without new wire-format assumptions.

No feature is considered delivered before firmware validation, Android/iOS mapper tests, and timestamped device capture prove the same contract.

## 10. Runtime implementation boundary

The historical proposal assigned the accepted plan, current step, scale, deadline, sequence and terminal state to a future TrainingSessionRuntime. This description is retained for history only; it is not the current firmware architecture or an implementation instruction.
