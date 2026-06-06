# Demo APP Refactor Master Plan

状态：总审计与详细计划
文档类型：长期重构计划
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
当前基线：`e283f25 refactor(demo): extract measurement display store` + 当前工作区 B2/B3 重构

## 1. 本轮判型

本轮属于审计 / 高风险冻结前置，不直接实现代码。

目标链路：

- Demo APP 后续重构路线。
- `DemoViewModel` owner 拆分。
- Demo APP telemetry / calibration / wave control / capture evidence 的可测试边界。

相关链路：

- `EVT:STREAM` 消费与 `STREAM:SET` 显式订阅。
- 校准录点、模型比较、写模型反馈。
- Wave start / stop pending 与 truth refresh。
- raw console / system log / capture audit。
- motion sampling、test session、recording/export。

不该动的层：

- ESP32 BLE wire payload。
- `STREAM:SET` 合同。
- 固件校准算法。
- MAX485 / Modbus 参数。
- 正式 SW APP 默认行为。
- `SonicWaveClient` / BLE transport 行为。
- Demo APP 一次性全量重构。

## 2. 成熟方案对齐

参考 Android 官方架构建议，本轮采用以下原则：

- ViewModel 继续作为屏幕级状态 holder，对 UI 暴露状态并接收 UI action。
- 对复杂、可独立测试、生命周期不依赖 Android SDK 的 UI 状态生产逻辑，抽成 plain class state holder。
- 保持单向数据流：事件进入 ViewModel，ViewModel 组合各 owner 输出 UI state；UI 不直接写业务 truth。
- 只有当逻辑需要复用、测试或降低 ViewModel 复杂度时才抽 owner；不为了“文件大”强拆。

## 3. 当前结构审计

当前代码形态：

- `DemoViewModel.kt` 仍是主 orchestration owner。
- 已完成 `MeasurementDisplayStore`，measurement display 纯状态逻辑已离开 ViewModel。
- 已完成 `RawConsoleStore`，raw console buffer / filter / high-priority publish 纯状态逻辑已离开 ViewModel。
- 已完成 `CalibrationSessionStore`，校准点集 / model comparison / prepared model / option sync 纯状态逻辑已离开 ViewModel。
- 已存在 `TelemetryRecorder`、`TestSessionManager`、`TestSessionExporter`、`MotionSamplingExporter`、`DemoMeasurementTrace`、`WaveLifecycleCommandGate`。
- UI 已按 section 拆分，但 `CalibrationToolsSection.kt` 和 `MotionSamplingSection.kt` 仍较大。

当前 ViewModel 仍承担：

- 连接、扫描、capability probe、snapshot refresh。
- `WAVE:SET / START / STOP` 发送、pending start/stop、truth refresh。
- 校准录点入口、校准模型写入、ACK/NACK/Error 接入、写模型反馈。
- raw flow collect、业务日志调用点、raw console publish throttle。
- motion sampling session start/stop/row build/export 状态。
- test session 与 formal wave truth 的桥接。
- safety/fault/snapshot/event -> UI state 映射。

## 4. 总体结论

可以继续自动重构，但只能按小包推进。

推荐策略：

1. 优先做低风险、纯状态、容易测试的 owner。
2. 每包只迁移一个 owner，不同时改 UI、BLE、校准、控制和导出。
3. 先抽 pure reducer / store，再考虑迁移 side effect。
4. Wave control 和完整 event reducer 属于中高风险，必须先补 focused tests 或 capture 门禁。
5. 连接生命周期和 BLE client 不进入当前自动重构范围。

不建议：

- 不建议直接拆 `DemoViewModel` 成多个 ViewModel。
- 不建议第一刀把 `AndroidViewModel` 改成普通 `ViewModel`，因为当前 recorder/exporter/text 仍依赖 `Application` / resources。
- 不建议把 `client.send`、truth refresh job、BLE connection callback 从 ViewModel 迁出，除非先做 command gateway 合同和真机 capture。
- 不建议一次性重写 Compose UI 或路由结构。

## 5. 分包路线

### B2：RawConsoleStore

状态：已完成。阶段报告见 `reports/task_20260606_demo_app_raw_console_store.md`。

目标：

- 抽 `RawConsoleStore`。
- 管理 raw log ring buffer、max lines、`RawConsoleUiState`。
- 管理 incoming raw line 是否应显示：`EVT:STREAM` 默认过滤、legacy CSV 默认过滤、verbose 保留。
- 管理 high-priority log 是否应强制 publish。

保留在 ViewModel：

- 哪些业务点调用 `appendSystemLog()`。
- 日志文案本身。
- `client.rawLines / rawChunks / outgoingLines / transportLogs` collect。
- `publishRawConsole()` 的 throttle 调度可先保留，或只把 dirty state 返回给 ViewModel。

新增测试：

- 普通 SYS/RX/TX 行追加。
- 超过 `MAX_RAW_LOG_LINES` 裁剪旧行。
- verbose=false 时过滤 `EVT:STREAM`。
- verbose=true 时保留 raw stream。
- legacy CSV fallback 默认过滤。
- `[MEASUREMENT_CONSUME_SUMMARY] / [CAL_CAPTURE_RESULT] / [STREAM_SUBSCRIPTION_RESULT]` 判为 high-priority。

门禁：

```bash
git diff --check
cd tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.RawConsoleStoreTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

真机：

- 只做 B2 且测试通过，不要求立即真机。
- 若调整 capture 日志文案或过滤语义，必须跑 Demo APP telemetry/calibration capture。

### B3：CalibrationSessionStore

状态：已完成。阶段报告见 `reports/task_20260606_demo_app_calibration_session_store.md`。

目标：

- 抽 capture availability、dataset append、model comparison rebuild、prepared model parse、model option sync、reset。
- 统一 legacy `ACK:CAL_POINT` 和 APP live snapshot 两条录点进入同一 append 合同。
- ViewModel 继续负责 `_uiState.update` 触发点、日志、`client.send`、文案。

保留在 ViewModel：

- `sendCalibrationCapture()` action 入口和前置失败分支。
- `client.send(Command.CalibrationSetModel)`。
- ACK/NACK/Error 到 write/capture feedback 的事件接入顺序。
- `text(...)` 资源文案。
- `CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT / CAL_UI` 日志文案。

新增测试：

- capture availability 需要 connected / recording / valid reference / distance。
- model input parse 成功生成 manual prepared model。
- model input parse 失败清空 prepared model。
- valid points 触发 comparison rebuild 和 auto prepared fit。
- selected comparison model 切换更新 options / prepared。
- invalid calibration point 仍保留在 UI 点集，但不进入 fit dataset。
- reset 清空点集、comparison、feedback 和 prepared model。

门禁：

```bash
git diff --check
cd tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.CalibrationSessionStoreTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

真机：

- 如果只迁移 pure store 且 tests 通过，可先不真机。
- 如果触碰 `CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT / CAL_UI_BIND` 日志或 capture audit 语义，必须跑 `tools/esp32_demo_telemetry_calibration_capture.sh`。

### B4：WaveControlStateReducer

状态：第一阶段已完成。阶段报告见 `reports/task_20260606_demo_app_wave_control_state_reducer.md`。

目标：

- 已抽纯 reducer，管理 wave output transition、runtime clock、formal wave truth sync、wave control flags。
- 已把 `resolveAuthoritativeWaveOutput`、`resolveSnapshotStartReady`、`resolveOptimisticStopState` 纳入可测试 owner。
- pending start/stop lifecycle 和 formal session action 暂不迁移。

保留在 ViewModel：

- `client.send(Command.WaveSet / WaveStart / WaveStop)`。
- `viewModelScope` job、truth refresh scheduling。
- `WaveLifecycleCommandGate` token 调用。
- test session start/finish 的真实副作用。
- `PendingWaveStartRequest / PendingWaveStopRequest / PendingWaveStopCompletion` 生命周期。

新增测试：

- `WaveLifecycleCommandGateTest` 继续覆盖 Stop invalidates Start、新 Start fresh token。
- `DemoStartReadyRegressionTest` 继续覆盖 pending truth refresh 下 snapshot `start_ready=false` 不清掉已知 ready、Stop ACK fallback optimistic state、authoritative wave output。
- `WaveControlStateReducerTest` 新增覆盖 Start pending、Stop pending、Running、Ready、Safety blocked、runtime start/stop clock、formal wave truth sync。

门禁：

```bash
git diff --check
cd tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.WaveControlStateReducerTest" --no-daemon --stacktrace
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.WaveLifecycleCommandGateTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

真机：

- B4 触碰控制链，建议复用 `demo_app_wave_stop_race` capture 做最小回归。

### B5：MotionSamplingSessionStore

状态：可选；只有继续扩展 motion sampling 或导出字段时启动。

目标：

- 抽 start/stop/clear 状态。
- 抽 `buildMotionSamplingRow()` 的 row 计算，包括 elapsed、dd/dt、dw/dt、runtime/safety/model metadata。
- 抽 export path update。

保留在 ViewModel：

- `client.send(Command.MotionSamplingModeSet)`。
- `MotionSamplingExporter` IO。
- UI 文案和 `motionSamplingStatus` 资源文本。

新增测试：

- start metadata 从 current UiState 正确冻结。
- 第一条 row 无 delta。
- 第二条 row 计算 dd/dt 和 dw/dt。
- stop 写入 endedAtMs。
- active 时不能 clear/export。
- export 后回填 csv/json path。

### B6：TestSessionBridge

状态：中风险；建议排在 B4/B5 后。

目标：

- 把 formal wave truth 与 test session start/finish 的桥接逻辑抽成可测试 owner。
- 管理 pending start/stop completion、sessionCaptureSignals merge、finish reason。

保留在 ViewModel：

- `TestSessionManager` 可继续作为 session data owner。
- ViewModel 继续负责日志、UI notice、publish panel。
- 不改 exporter。

风险：

- 容易影响 Start/Stop 体感和 capture 结果。
- 启动前必须复用 `WaveLifecycleCommandGateTest` 和 wave stop race capture 经验。

### B7：DeviceConfigWriteTracker

状态：可选小包。

目标：

- 抽 pending device config request、confirmation match、timeout state。
- 保留 `client.send` 与 watchdog job 在 ViewModel。

测试：

- observed config match 后 success。
- mismatch 保持 pending。
- ACK fallback status。
- NACK/Error clears pending。
- timeout clears pending。

### B8：Presentation model / Compose 参数收口

状态：最后做；不作为业务 owner 第一阶段。

目标：

- 对 `CalibrationToolsSection`、`MotionSamplingSection`、`MainScreen` 做参数分组。
- 引入 section-specific presentation DTO 和 callback group。

边界：

- 不改视觉、不改交互、不改状态 owner。
- 不把 UI 内 `rememberSaveable` 的导出弹窗状态搬到 ViewModel。

## 6. 冻结项

以下不进入自动重构，除非出现真实 blocker 或用户明确点名：

- `SonicWaveClient` / BLE transport 重构。
- `observeClient()` 完整 event reducer 一次性迁移。
- 多 ViewModel / navigation 架构重写。
- `AndroidViewModel` -> `ViewModel` 全面迁移。
- Compose UI 视觉重做。
- ESP32 固件协议、校准算法、MAX485 参数。

## 7. 推荐执行顺序

推荐：

1. B2 `RawConsoleStore`：已完成。
2. B3 `CalibrationSessionStore`：已完成。
3. B4 `WaveControlStateReducer`：第一阶段已完成；第二阶段只在先审 pending lifecycle / formal session action 后继续。
4. B5 `MotionSamplingSessionStore`：下一建议包。
5. B7 `DeviceConfigWriteTracker`。
6. B8 Presentation model 收口。
7. B6 `TestSessionBridge`，只有 wave control reducer 稳定后再做。

暂不建议：

- 直接做 B6 或完整 event reducer。
- 直接拆 BLE connection owner。
- 为了降低行数而拆 UI/连接/控制/校准多个 owner。

## 8. 每包固定交付格式

每包必须输出：

- 改动点。
- 抽象点。
- 参数点。
- 验证结果。
- 未覆盖范围。
- 是否需要真机。
- 下一包建议。

每包默认门禁：

```bash
git diff --check
cd tools/android_demo
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

触碰控制链、capture 语义、BLE subscription、校准日志时，再加专项 capture。
