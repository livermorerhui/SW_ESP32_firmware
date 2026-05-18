# Motion-Safety Sampling Tool

## 2026-03-21 导出会话自动化（采样标签与自动命名）

本次改动只增强 Demo APP 的“导出会话”流程，目标是让高频采样阶段的样本更容易标准化交接，服务于当前的两个主研究方向：

- 区分正常使用
- 区分使用中摔倒 / 危险状态

这次增强不改变以下内容：

- 固件运行时逻辑
- leave / fall 判定逻辑
- sampling mode 行为
- stop / pause 执行逻辑
- moving-average 研究层主逻辑

### 用户操作

点击 `导出会话` 后，APP 不再立刻导出，而是先弹出一个轻量标签表单：

- 主标签：
  - `NORMAL_USE`
  - `FALL_DURING_USE`
- 细分类标签：
  - `NORMAL_VIBRATION`
  - `LEAVE_PLATFORM`
  - `PARTIAL_LEAVE`
  - `FALL_ON_PLATFORM`
  - `FALL_OFF_PLATFORM`
  - `LEFT_RIGHT_SWAY`
  - `SQUAT_STAND`
  - `RAPID_UNLOAD`
  - `OTHER_DISTURBANCE`
- 备注：
  - 可选文本输入
  - 只写入 JSON metadata，不进入文件名

确认后，导出流程会自动继续，不需要人工再重命名文件。

### 自动命名规则

CSV 和 JSON 共用同一个基础文件名：

- `<主标签>_<细分类>_<频率>hz_<强度>_<时间戳>.csv`
- `<主标签>_<细分类>_<频率>hz_<强度>_<时间戳>.json`

示例：

- `NORMAL_USE_NORMAL_VIBRATION_20hz_80_202603211650.csv`
- `FALL_DURING_USE_FALL_OFF_PLATFORM_20hz_80_202603211655.json`

命名时会自动带入：

- 当前会话快照中的频率 `waveFrequencyHz`
- 当前会话快照中的强度 `waveIntensity`
- 导出确认时生成的时间戳

### JSON metadata 新增字段

为了让离线分析、样本交接和脚本读取都能直接理解样本语义，JSON metadata 现在会新增以下字段：

- `primaryLabel`
- `subLabel`
- `notes`
- `frequencyHz`
- `intensity`
- `exportedAt`

同时保留已有的兼容字段：

- `scenarioLabel`
- `scenarioCategory`
- `waveFrequencyHz`
- `waveIntensity`
- `exportTimestampMs`

其中：

- `scenarioCategory` 对应主标签
- `scenarioLabel` 对应细分类标签
- `sessionNotes` 保留原有会话级备注语义，避免和新的导出备注冲突

### 边界说明

这次“导出会话自动化”是采样标签标准化增强，不是运行时 safety 功能改造。

它只做三件事：

- 导出前收集标签
- 统一生成文件名
- 把标签和上下文写入 JSON metadata

它不会把标签或备注写回原始采样行，也不会参与当前运行时安全判定。

## Purpose

The Motion-Safety Sampling Tool is the first usable Demo APP MVP for collecting real motion data before final leave-platform and fall parameters are chosen.

It is an engineering/debug tool, not a consumer feature.

Current source of truth:

- Demo APP module:
  - `tools/android_demo/app-demo/`
- live runtime/safety inputs:
  - `DemoViewModel`
  - current BLE stream sample handling
  - current runtime/safety UI state

## MVP Scope

The MVP focuses on:

- start sampling
- stop sampling
- keep a reviewable in-memory session
- capture structured time-series rows
- show basic recorded-session charts
- show compact session review data
- export structured session files

The MVP does not yet attempt to provide:

- full label editing
- advanced segmentation workflows
- threshold derivation inside the app
- firmware leave/fall logic redesign

## Session Flow

### Start

When the operator presses `开始采样`:

- a new in-memory session is created
- a unique `sessionId` is assigned using the current timestamp
- `startedAtMs` is recorded
- current metadata snapshot is captured when available:
  - `appVersion`
  - `capabilityInfo` as firmware metadata proxy
  - connected device name
  - protocol mode
  - wave frequency
  - wave intensity
  - sampling-mode flag
  - running-at-session-start flag
  - latest calibration model metadata

### Stop

When the operator presses `停止采样`:

- the active session is closed cleanly
- `endedAtMs` is recorded
- the captured session remains visible and reviewable in-app

Sampling also stops automatically on disconnect so an unfinished session does not remain active silently.

### Clear

When the operator presses `清空会话`:

- the inactive in-memory session is removed
- no export file is produced automatically
- if the current stopped session has not yet been exported, the app now requires an explicit discard confirmation first

### Export

When the operator presses `导出会话`:

- the current inactive session is exported
- the operator must first choose a scenario label in the export dialog
- CSV is generated for row-based time-series analysis
- JSON is generated as a metadata sidecar

Current export scenarios:

- `静止站立`
- `正常律动`
- `离开平台`
- `异常动作-快速减载`
- `异常动作-左右摇摆`
- `异常动作-下蹲站起`
- `异常动作-半离台`
- `自定义`

Current Android export target:

- `Downloads/SonicWave/`

- Current filename format:

  - `<场景>_<频率>hz_<强度>_<YYYYMMDDHHMM>.csv`
  - `<场景>_<频率>hz_<强度>_<YYYYMMDDHHMM>.json`

## Recorded Row Schema

Each sampled row is stored as structured data, not raw log text.

Current row fields:

- `sampleIndex`
- `timestampMs`
- `elapsedMs`
- `distanceMm`
- `liveWeightKg`
- `stableWeightKg` nullable
- `measurementValid`
- `stableVisible`
- `runtimeStateCode`
- `waveStateCode`
- `safetyStateCode`
- `safetyReasonCode`
- `safetyCode` nullable
- `connectionStateCode`
- `modelTypeCode` nullable
- `userMarker` nullable
- `motionSafetyState` nullable
- `ddDt` nullable
- `dwDt` nullable

Current field semantics:

- distance is direct live measurement from the stream
- weight is the currently displayed model-derived live weight
- stable weight is only filled when the stable-weight indicator is active
- runtime/wave/safety values come from the current app runtime state
- `ddDt` and `dwDt` are lightweight per-row extension fields for later analysis

## Review UI

The MVP review surface includes:

- session controls
- current live summary
- session summary
- basic recorded-session charts
- last recorded values
- recent row preview

### Current live summary

Shows the latest visible app state while sampling:

- distance
- weight
- stable weight
- measurement validity summary
- runtime / wave / safety context

### Session summary

Shows:

- session id
- start time
- end time or in-progress state
- row count
- wave frequency / intensity snapshot
- sampling-mode-at-start and running-at-start flags
- last export scenario when present
- last export paths when present
- export recommendation when the current stopped session has not yet been exported

### Basic charts

Current charts render from recorded session rows, not only from the live display:

- distance vs time
- live weight vs time

This is enough for the MVP to inspect basic motion patterns without building a full analytics dashboard yet.

### Row preview

The recent row preview gives a compact check that data is really being captured and includes runtime/safety context with each row.

## Export Format

### CSV

CSV is the primary export for later threshold and envelope analysis.

It contains one row per sample and is intended to be easy to import into scripts, spreadsheets, or later offline tooling.

### JSON

JSON is the metadata sidecar.

Current JSON includes:

- schema version
- session id
- original session id
- start/end timestamps
- export timestamp
- row count
- app version
- firmware metadata proxy
- connected device name
- protocol mode
- scenario label
- scenario category
- wave frequency
- wave intensity
- sampling mode enabled
- wave-running-at-session-start flag
- top-level model type
- top-level model coefficients
- model metadata
- exported column list
- extensibility placeholders

## Bounded Diagnostics

Sampling diagnostics are intentionally bounded:

- session started
- periodic row-count progress every 50 rows
- export scenario / filename / wave metadata snapshot
- session stopped
- export destination

The row data itself is stored structurally in memory and export files, rather than being spammed into the normal raw log.

## Extension Points

The MVP intentionally leaves room for later motion-safety work.

Reserved/nullable areas include:

- `userMarker`
- `motionSafetyState`
- `ddDt`
- `dwDt`
- JSON extensibility descriptors for segment labels and event markers

This keeps the MVP simple now while avoiding a dead-end schema.

## Engineering Sampling Mode

Task-MOTION-1A adds an explicit engineering mode around the sampling workflow.

Purpose:

- allow longer motion-safety sampling sessions to continue running
- preserve visibility into fall detections
- avoid repeatedly stopping the waveform because of known false-positive-prone fall behavior during data collection

### What it suppresses

When sampling mode is enabled:

- fall detection still runs
- fall events remain visible
- the final fall-triggered wave stop / abnormal-stop action is suppressed

### What it does not suppress

- leave-platform action is still active
- normal runtime fall protection is still active when sampling mode is off
- this is not a permanent safety-policy change

### Visibility

The current implementation makes the mode explicit through:

- firmware capability fields
- firmware serial diagnostics
- Demo APP engineering controls and status text in the motion-sampling section

## Moving-Average Research Overlay

Task-MOTION-ANALYSIS-1 adds a lightweight in-app moving-average research overlay on top of the captured-session review UI.

Purpose:

- compare raw vs smoothed captured curves directly inside the Demo APP
- support quick engineering exploration after capture without requiring export first
- keep motion-safety runtime ownership unchanged while engineering reviews captured data

Current implementation boundary:

- the moving average is derived only from `motionSamplingSession.rows`
- the moving average is computed only inside the Demo APP motion-sampling UI
- the moving average does not change:
  - firmware leave detection
  - firmware fall detection
  - sampling-mode behavior
  - stop / pause execution behavior
  - session capture/export schema

### Overlay Controls

The motion-sampling section now includes one shared moving-average point-count control.

Current behavior:

- default value: `5`
- presets:
  - `3`
  - `5`
  - `7`
- accepted range: `1..50`
- blank or invalid text keeps the last valid applied value
- no explicit apply button is required

Changing the applied point count immediately recomputes the overlay from the already captured session rows.

### Covered Signals

Current MVP moving-average coverage:

- `distanceMm`
- `liveWeightKg`

Current MVP exclusions:

- `stableWeightKg`

`stableWeightKg` remains visible in raw latest-value summaries, but it is not included in the overlay because it is nullable and only meaningful while stable visibility is active.

### Chart Behavior

The session chart keeps the original raw curves visible and overlays moving-average curves on the same combined chart.

Current rendering behavior:

- thin lines: raw distance and raw live weight
- bold lines: moving-average distance and moving-average live weight
- recompute/redraw occurs automatically when the moving-average point count changes

### Latest MA Values

The motion-sampling section now extends the `Last recorded values` block with explicit latest moving-average values for:

- distance MA
- live-weight MA

The labels are rendered as `MA(n)` so they are not confused with current runtime values or firmware-owned stable-weight output.
