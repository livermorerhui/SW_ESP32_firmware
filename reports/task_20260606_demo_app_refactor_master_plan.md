# Demo APP Refactor Master Plan

状态：总审计、详细计划与阶段进度
文档类型：长期重构计划
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-07
当前基线：`148789a feat(demo): simplify information architecture` + 当前工作区 B6 第一阶段重构

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

状态：已完成。阶段报告见 `reports/task_20260606_demo_app_motion_sampling_session_store.md`。

目标：

- 已抽 start/stop/clear 状态。
- 已抽 row build，包括 elapsed、dd/dt、dw/dt、runtime/safety/model metadata。
- 已抽 export metadata update。
- 已补 session snapshot 回填，避免 UI 只能看到 active/status 而拿不到 session 数据。

保留在 ViewModel：

- `client.send(Command.MotionSamplingModeSet)`。
- `MotionSamplingExporter` IO。
- UI 文案和 `motionSamplingStatus` 资源文本。
- system log 调用点。

新增测试：

- start metadata 从 current UiState 正确冻结。
- 第一条 row 无 delta，并携带 runtime/safety/model metadata。
- 第二条 row 计算 dd/dt 和 dw/dt。
- inactive 状态不追加 row。
- stop 写入 endedAtMs。
- active 时不能 clear。
- export 后回填 csv/json path。

### B6：TestSessionBridge

状态：第一阶段已完成，B6 运行页轻量 smoke 通过候选。阶段报告见 `reports/task_20260607_demo_app_test_session_bridge_stage1.md`。

目标：

- 已把 formal wave truth 与 test session start/finish 的桥接逻辑抽成 `TestSessionBridge`。
- 已迁移 test session start、clear、export metadata mark、sample append、finish、`TEST:START` / `STOP_SUMMARY` 接入、inactive truth stop plan 和 sample build。
- 已让 session frequency fallback、recording/finished gate 和 stop reason fallback 进入 focused tests。

保留在 ViewModel：

- `client.send`、wave start/stop pending request、truth refresh job、`WaveLifecycleCommandGate` token 调用仍在 ViewModel。
- ViewModel 继续负责日志、UI notice、publish panel、`SessionCaptureSignals` 上游 merge。
- `TestSessionExporter` 文件 IO 和导出格式不改。

风险：

- 该包触碰运行页 Start/Stop 与 test session 绑定关系，已补 B6 运行页轻量 smoke：用户体感通过，ESP32 采到 `WAVE:START / WAVE:STOP / STOP_SUMMARY`。完整 quality baseline 仍缺信息架构和工具区人工 marker，不作为 B6 blocker。
- 不应继续在同一包迁移 BLE command send、完整 event reducer 或 exporter。

### B7：DeviceConfigWriteTracker

状态：已完成。阶段报告见 `reports/task_20260606_demo_app_device_config_write_tracker.md`。

目标：

- 已抽 pending device config request、confirmation match、timeout state。
- 保留 `client.send` 与 watchdog job 在 ViewModel。

测试：

- `DeviceConfigWriteTrackerTest` 覆盖 observed config match 后 success、mismatch 保持 pending、缺字段保持 pending、ACK fallback、NACK/Error clear、timeout clear、confirmation refresh gate。

### B8：Presentation model / Compose 参数收口

状态：第一阶段已完成。阶段报告见 `reports/task_20260606_demo_app_presentation_model_stage1.md`。

目标：

- 已对 `CalibrationToolsSection`、`MotionSamplingSection`、`MainScreen` 做 callbacks 参数分组。
- 已引入 section-specific actions DTO：`CalibrationToolsActions` / `MotionSamplingActions`。

边界：

- 不改视觉、不改交互、不改状态 owner。
- 不把 UI 内 `rememberSaveable` 的导出弹窗状态搬到 ViewModel。
- 不引入多 ViewModel / navigation 重写。

### B9：Demo APP quality baseline smoke

状态：真机 smoke 已复核，结论为通过候选（有采集观察项）。阶段报告见 `reports/task_20260607_demo_app_quality_baseline_smoke_prep.md`。

目标：

- 对 B2-B8 多包重构做轻量真机 smoke，而不是继续扩大重构。
- 用专项入口 `tools/demo_app_quality_smoke_capture.sh` 固定用户步骤、capture scenario 和 audit。
- 复核连接、实时流、Start -> Stop、校准工具入口、motion sampling 启停和可选 device config 写入。

边界：

- 不改 Demo APP 业务行为。
- 不改 ESP32 协议、BLE wire payload、固件行为或正式 SW APP。
- `device config` 写入只在现场确认安全时执行；否则作为未覆盖观察项。
- 如果 audit 只显示证据缺口，不直接扩大业务代码改动，先补采集或日志证据。

### B10：Demo APP information architecture simplification

状态：第十阶段已完成，本地验证通过，待用户看效果后补一次 UI smoke。阶段报告见 `reports/task_20260607_demo_app_information_architecture_simplification.md`。

目标：

- 将默认首页从所有 section 纵向堆叠改为 `型号 / 校准 / 采样 / 运行 / 日志` 分区入口，并固定在顶部 app bar 下方。
- 默认进入 `型号` 页，优先完成 Base / Plus 设定。
- `运行` 页只保留系统主状态、遥测曲线、测试会话；连接详情归到 `型号`。
- 将低频或高风险入口移到对应页：型号设定与保护开关进入 `型号`，校准工具进入 `校准`，motion sampling 进入 `采样`，raw console 进入 `日志`。
- `DeviceConnectSection` 与 `SystemStatusSection` 支持 compact 默认视图，工程字段仍可展开查看。
- `SCALE:ZERO` / `CAL:ZERO` 发送前增加确认弹窗，取消时不发送；已写入撤回暂不实现，因为当前固件合同没有可逆事务。
- 顶栏文案精简为 `SW调试 / 搜索 / 断开`。
- `型号` 页主操作精简为 `设置型号`：Base / Plus 可选，Pro / Ultra 置灰，距离传感器提示随选择变化，多余状态放入详情。
- 保护卡片默认只保留 `摔倒保护` 与 `律动离开` 两个开关；`律动离开` 复用固件既有 `SAFETY:LEAVE_PROTECTION` 合同接入 Demo protocol / sdk / ViewModel。
- 型号页连接状态卡片已删除，连接仍由固定顶栏处理。
- `采样` 页完成第一轮内部深压缩，工程说明、raw 行预览和 schema 提示不再常驻；会话详情和曲线设置默认收起。
- `运行` 页旧 `当前交付边界` 卡片已删除，曲线设置和测试会话导出路径默认收起。
- `运行` 页 compact 系统状态卡片完成尺寸和文案密度优化。
- `采样` 页主流程只保留 `开始采样 / 停止采样 / 清空会话 / 导出会话`；设备侧 `DEBUG:MOTION_SAMPLING` 开关收进 `采样设置`。
- `型号` 页主卡片显示 `当前设备` 真值，写入状态不再藏在详情里。

边界：

- 不改 `DemoViewModel` business owner。
- 不改 stores/reducers 合同。
- 不改固件已有 BLE command 语义、ESP32 固件行为或正式 SW APP。
- 不删除任何调试能力，只降低默认首页信息密度。

真机：

- 第一阶段已复用 B9 smoke。用户确认连接、实时数据、Start -> Stop、校准工具、motion sampling 和日志入口可用；AI 复核 Android transport、SNAPSHOT/STREAM、ESP32 start/stop/STOP_SUMMARY 和 visual evidence 后判定为 `PASS_CANDIDATE`。第二阶段修正 Tab 固定、顺序、内容去重和归零确认。第三阶段完成型号页精简、连接卡片删除和保护双开关接入。第四阶段完成校准页移动端竖向主流程压缩：开始校准、设备归零、记录、点表、曲线、线性/二次和写入边界已重新组织，并以圆圈信息弹窗承载必要说明。第五阶段按用户反馈继续压缩：删除重复标题、删除常驻录制状态/文件路径/解释按钮、把实时距离和记录同排、模型区只保留线性/二次/写入主操作。第六阶段完成结束按钮化、清空校准点、参考重量/实时距离并排和模型选项拟合状态展示。第七阶段删除高级工程区冗余说明和旧 Z/K 校准路径。第八阶段完成采样页和运行页第一轮深压缩，旧交付边界卡片已删除，工程详情默认收起。第九阶段完成运行页系统状态紧凑化，并将采样模式设备开关从主流程移入采样设置。第十阶段完成型号页当前设备真值和写入状态主卡片反馈；已通过本地 compile/test/assemble，建议用户先看效果后补一次轻量 UI smoke。

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
4. B5 `MotionSamplingSessionStore`：已完成。
5. B7 `DeviceConfigWriteTracker`：已完成。
6. B8 Presentation model 收口：第一阶段已完成。
7. B9 Demo APP quality baseline smoke：真机 smoke 已复核，当前不再阻塞。
8. B10 Demo APP information architecture simplification：第十阶段已完成，本地验证通过，待用户看效果后补轻量 UI smoke。
9. B6 `TestSessionBridge`：第一阶段已完成，B6 运行页轻量 smoke 通过候选。

暂不建议：

- 继续扩大 B6 到 command send、exporter 或完整 event reducer。
- 直接拆 BLE connection owner。
- 为了降低行数而拆 UI/连接/控制/校准多个 owner。
- 在 B6 smoke 前继续开 B4 第二阶段或连接 owner。

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
