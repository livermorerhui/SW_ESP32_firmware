# ESP32 Firmware Optimization Priority Table

最后更新时间：2026-06-07

## 1. 当前结论

ESP32 固件当前主链可继续作为联调和阶段交付基线。`PLUS + laser_installed=1 + measurement unavailable` 下的 degraded measurement circuit-breaker / low-frequency probe 已完成实现、本地验证和真机 capture 复核。

按成熟工程标准看，当前剩余工作主要是协议防漂移、owner 边界审计、文档真相源收口和发布硬化；没有新的必须立刻阻断联调的固件 blocker。2026-05-18 SW release hardening 窗口已完成 ESP32-plus minimum soak 真机 capture 复核，当前 SW `52f782f` + ESP32 `5bb7e0c` 组合可作为本轮 release hardening 基线。

截至 2026-05-18 `1759a12`，ESP32 固件低风险工程标准化重构已经收口：protocol / ACK / measurement probe / laser diagnostics / calibration runtime / stable contract diagnostics / stable window / presence counter wrapper 均已完成本地验证。当前没有“必须继续重构后才能联调或发版”的固件结构项。剩余 `BleTransport` 拆分、stable candidate owner、baseline latch owner、presence owner state carrier、occupied-cycle owner、motion safety runtime action 等均属于可选预研或中高风险专项，不应按普通自动重构继续推进。

2026-06-02 已完成 Demo APP 实时遥测 / 校准实时数据修复：`EVT:STREAM` 上行改为 BLE session scoped 显式订阅，合同入口为 `docs/system/esp32_realtime_stream_subscription_contract.md`。Demo APP 在能力支持时发送 `STREAM:SET enabled=1,rate_hz=10`，正式 SW APP 可默认不发送；关闭实时流只影响 BLE 上行，不影响固件内部测量、baseline 或 safety 判断。该包已合入 `main`：`bc18a74`。同日已补 Demo APP 消费层可观测性：`STREAM_SUBSCRIPTION_RESULT / MEASUREMENT_CONSUME_SUMMARY / CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT`，后续新版本真机 capture 应能自动证明 UI 消费和校准录点链路。

2026-06-04 已完成 Demo APP 快速 `Start -> Stop` 竞态修复与真机复核：commit `a7882f9` 新增 `WaveLifecycleCommandGate`，防止旧的 in-flight Start 在 Stop 后继续发送 `WAVE:START`。真机 capture `20260604_164648...demo_app_wave_stop_race` 证明 18 次 start / stop 对齐，ESP32 每轮均进入 `WAVE_STOP received -> STOP REQUEST -> wave.stopSoft -> i2s_stop`。当前 Demo APP 可以继续正常运行，不需要为了运行或联调立即重构。

2026-06-06 已完成 Demo APP 重构启动审计：`reports/task_20260606_demo_app_refactor_start_audit.md`。结论是可以自动重构，但只建议先做小包 `FW-OPT-013 Measurement display owner 抽取`；`FW-OPT-014 Calibration session owner 抽取` 继续按条件触发，`FW-OPT-015 DemoViewModel 大范围重构` 保持冻结。

2026-06-06 已完成 `FW-OPT-013 Measurement display owner 抽取`：新增 `MeasurementDisplayStore`，将 Demo APP `Event.StreamSample -> MeasurementDisplayUiState / TelemetryPointUi / moving average / carrier accept policy` 的纯状态计算从 `DemoViewModel` 抽出。`DemoViewModel` 继续保留日志、recording、motion sampling、test session、UI publish 和 watchdog 等副作用 owner。本包通过 focused JVM tests 和 `:sonicwave-protocol:test :app-demo:testDebugUnitTest`，未改 BLE 合同、固件行为或校准链路。

2026-06-06 已完成 Demo APP 后续重构总计划：`reports/task_20260606_demo_app_refactor_master_plan.md`。结论是继续采用“小 owner + focused tests + 阶段门禁”，不做全量 `DemoViewModel` 拆分。后续优先顺序为：低风险 `RawConsoleStore`、按条件触发的 `CalibrationSessionStore`、可审计的 `WaveControlStateReducer`，再评估 motion / test session bridge；连接生命周期、BLE client、完整 event reducer 和 Compose UI 大拆保持冻结。

2026-06-06 已完成 `FW-OPT-016 Raw console owner 抽取`：新增 `RawConsoleStore`，将 Demo APP raw log buffer、时间戳格式化、`EVT:STREAM` / legacy CSV raw 过滤、高优先级日志判定和 `RawConsoleUiState` 生成从 `DemoViewModel` 抽出。`DemoViewModel` 继续保留所有业务日志调用点和 raw flow collect。本包通过 focused JVM tests 和 `:sonicwave-protocol:test :app-demo:testDebugUnitTest`，未改 capture 日志文案、BLE 合同或固件行为。

2026-06-06 已完成 `FW-OPT-014 Calibration session owner 抽取`：新增 `CalibrationSessionStore`，将 Demo APP 校准 capture availability、点集 append、model comparison rebuild、prepared model parse、model option sync 和 reset 纯状态从 `DemoViewModel` 抽出。`DemoViewModel` 继续保留采集按钮前置分支、`client.send`、ACK/NACK/Error 接入顺序、资源文案和 capture 日志。本包通过 focused JVM tests 和 `:sonicwave-protocol:test :app-demo:testDebugUnitTest`，未改 `CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT` 文案、BLE 合同、固件校准算法或 MAX485 参数。

2026-06-06 已完成 `FW-OPT-017 Wave control state reducer` 第一阶段：新增 `WaveControlStateReducer`，将 Demo APP wave output transition、runtime clock、formal wave truth sync、wave control pending flags、snapshot start_ready merge、authoritative wave output、optimistic stop state 等纯状态规则从 `DemoViewModel` 抽出。`DemoViewModel` 继续保留 `PendingWaveStartRequest / PendingWaveStopRequest / PendingWaveStopCompletion` 生命周期、`client.send`、truth refresh job、`WaveLifecycleCommandGate` token 调用和 test session start/finish 副作用。本包通过 focused JVM tests、既有 start-ready 回归和 `:sonicwave-protocol:test :app-demo:testDebugUnitTest`，未改 BLE command 时序、固件协议或 capture 日志语义。

2026-06-06 已完成 `FW-OPT-018 Motion sampling session owner 抽取`：新增 `MotionSamplingSessionStore`，将 Demo APP motion sampling session start/stop/clear、row build、dd/dt、dw/dt、export metadata update 和 session snapshot 回填从 `DemoViewModel` 抽出。`DemoViewModel` 继续保留 `client.send(Command.MotionSamplingModeSet)`、`MotionSamplingExporter` 文件 IO、中文状态文案和 system log 调用点。本包通过 focused JVM tests 和 `:sonicwave-protocol:test :app-demo:testDebugUnitTest`，未改导出 CSV/JSON 格式、BLE command 时序、固件协议或 capture 日志语义。

2026-06-06 已完成 `FW-OPT-020 Device config write tracker 抽取`：新增 `DeviceConfigWriteTracker`，将 Demo APP device config 写入 pending request、observed truth match、generic ACK fallback、NACK/Error/timeout 清理和 confirmation refresh 判定从 `DemoViewModel` 抽出。`DemoViewModel` 继续保留 `client.send(Command.DeviceSetConfig)`、watchdog coroutine、中文状态文案、system log 和 snapshot/capability refresh 调用点。本包通过 focused JVM tests 和 `:sonicwave-protocol:test :app-demo:testDebugUnitTest`，未改 `DEVICE:SET_CONFIG` payload、`ACK:DEVICE_CONFIG` 解析合同、ESP32 固件行为或正式 SW APP。

2026-06-07 已完成 `FW-OPT-019 Demo APP UI presentation model 收口` 第二阶段：`SectionPresentationModels` 继续扩展 `DeviceToolsActions / WaveControlActions / TestSessionActions / RawConsoleActions`，`MainScreen` 统一收集 measurement / test session / raw console state 并组装 section actions；`DeviceToolsContent`、`RunDashboardContent`、`WaveControlBottomBar`、`TestSessionSection`、`RawConsoleSection` 不再直接消费 `DemoViewModel` 或散落回调。该包遵循 Compose state hoisting / plain state holder 边界：业务状态仍由 `DemoViewModel` / stores 暴露，简单 UI element state 继续留在 composable 内。本包通过 `:app-demo:compileDebugKotlin`，未改 UI 视觉、交互、BLE command、ESP32 协议或业务状态 owner。

2026-06-07 已完成 `FW-OPT-021 Demo APP quality baseline smoke` 真机复核：新增 `tools/demo_app_quality_smoke_capture.sh` 和 `tools/demo_app_quality_smoke_audit.py`，用于 B2-B8 多包重构后的轻量真机 smoke。capture `20260607_101316...demo_app_quality_smoke` 的专项 audit 为 `PASS_CANDIDATE`，证明连接后实时数据、Start -> Stop、校准工具、motion sampling 和日志入口可用；后续完整 UI / 信息架构 capture `20260607_154532...demo_app_quality_smoke` 与运行页设备闭环补采 `20260607_161021...demo_app_quality_smoke` 合并后，正式判定 quality baseline smoke 通过。观察项：device config 写入按现场安全条件未覆盖；Android focus logcat stop 时 stale，但有 stop snapshot、ESP32 串口和 visual evidence 旁证，不作为功能 blocker。

2026-06-07 已完成 `FW-OPT-022 Demo APP information architecture simplification` 第十阶段：顶部标题和操作文案已精简为 `SW调试 / 搜索 / 断开`；默认入口更新为固定顶部 `型号 / 校准 / 采样 / 运行 / 日志`。型号页主操作改为 `设置型号`，只开放 Base / Plus，Pro / Ultra 置灰；Base 显示 `无距离传感器`，Plus 显示 `有距离传感器`，新增 `当前设备：Base（无距离传感器）/ Plus（有距离传感器）` 直接显示设备回传真值，写入状态无需展开详情即可看到；多余配置真值和状态说明收进 `查看详情`。型号页连接状态卡片已移除。保护卡片默认只保留 `摔倒保护` 与 `律动离开` 两个开关，说明和状态收进详情；`律动离开` 复用固件既有 `SAFETY:LEAVE_PROTECTION` / `ACK:LEAVE_PROTECTION` 合同接入 Demo APP protocol / sdk / ViewModel，不改固件 BLE 语义。`SCALE:ZERO` / `CAL:ZERO` 发送前确认保持有效；已写入撤回暂不实现，因为当前固件 / APP 合同没有可逆事务。校准页已进一步压缩：删除重复标题和常驻说明，开始校准 / 设备归零同排，结束校准按钮化，参考重量 / 实时距离同排，记录 / 清空校准点同排，模型区显示线性/二次拟合状态并只保留待写入摘要和写入模型；高级工程区删除冗余说明和旧 Z/K 校准路径，只保留模型回读、工程归零、手动模型参数、详细日志和当前模型摘要。采样页已删除常驻工程说明、raw 行预览和 schema 提示，实时摘要/会话摘要压缩，工程信息和曲线参数收进详情；`开启/关闭采样模式` 从主流程移入 `采样设置`，直接 `开始采样` 即可记录 APP 采样会话。运行页删除旧交付边界卡片，曲线设置和测试会话导出路径默认收起，compact 系统状态卡片完成尺寸和文案密度优化。本包本地 compile/test/assemble 已通过，待用户看效果后补轻量 UI smoke。

2026-06-07 已完成 `FW-OPT-023 Demo APP TestSessionBridge` 第一阶段：新增 `TestSessionBridge`，将 Demo APP 运行页测试会话 start、clear、export metadata mark、sample append、finish、`TEST:START` / `STOP_SUMMARY` 接入、formal inactive truth stop plan 和 sample build 从 `DemoViewModel` 抽到窄 owner。`DemoViewModel` 继续保留 `client.send`、wave pending request、truth refresh job、`WaveLifecycleCommandGate` token 调用、日志、notice、panel publish 和 exporter IO。本包通过 focused JVM tests、`:app-demo:testDebugUnitTest`、`:app-demo:assembleDebug` 和 `git diff --check`；B6 运行页轻量真机 smoke `20260607_150634...demo_app_quality_smoke` 采到 `WAVE:START / WAVE:STOP / STOP_SUMMARY`，用户体感通过。专项 audit 为 `PARTIAL_PASS_EVIDENCE_GAP`，原因是缺少完整 quality baseline 的人工 marker，不是 B6 blocker。未改 ESP32 固件协议、BLE command 时序、导出格式或 Demo APP 信息架构。

2026-06-07 已完成 `FW-OPT-017 Wave control state reducer` 第二阶段：新增 `WavePendingLifecycleStore`，将 Demo APP 运行页 wave start / stop pending request、stop completion、pending lifecycle idle 判定和 timestamp 生成从 `DemoViewModel` 抽到纯状态 owner。`DemoViewModel` 继续保留 `client.send(Command.WaveSet / WaveStart / WaveStop)`、truth refresh coroutine、`WaveLifecycleCommandGate` token 调用、UI notice、system log 和 test session finish 副作用。本包通过 focused JVM tests、`:sonicwave-protocol:test :app-demo:testDebugUnitTest`、`:app-demo:assembleDebug` 和 `git diff --check`；运行页真机 smoke `20260607_152955...demo_app_quality_smoke` 采到 `WAVE:START / WAVE:STOP / STOP_SUMMARY`，用户体感通过。专项 audit 为 `PARTIAL_PASS_EVIDENCE_GAP`，原因是缺少完整 quality baseline marker，不是 B4 blocker。

2026-06-07 已完成 Demo APP quality baseline smoke 阶段收口：完整 UI / 信息架构 capture `20260607_154532...demo_app_quality_smoke` 采到 6 个用户确认点和 18 个 visual evidence 文件，覆盖默认型号页、固定顶部 Tab、型号页精简、校准页压缩、采样页、运行页和日志页；运行页设备闭环补采 `20260607_161021...demo_app_quality_smoke` 使用 PlatformIO monitor 采到 `WAVE:START / START ALLOW / WAVE:STOP / STOP REQUEST / i2s_stop / STOP_SUMMARY`，`STOP_SUMMARY result=NORMAL stop_reason=MANUAL_STOP`。两轮证据合并后可作为 B2-B8 重构与 B10 信息架构阶段的 quality baseline 通过结论。同步将 `tools/demo_app_quality_smoke_capture.sh` 默认 ESP32 串口采集改为 `platformio`，避免 macOS raw `stty` 乱码；`device config` 写入仍按现场安全条件可选，未作为 blocker。

本总表是 ESP32 固件后续优化的长期入口。一次性报告只作为证据来源，不作为 backlog 真相源。

## 2. 固定边界

后续优化默认冻结：

- `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*` 线格式和消费语义。
- `SystemStateMachine` 作为最终 start / stop / safety action owner。
- `MEASUREMENT_UNAVAILABLE` 默认 `WARNING_ONLY` 语义。
- Android APP 消费逻辑、session runtime、safety 主链。
- `WaveModule` ramp / I2S 输出时序，除非另起输出专项证据包。

如果某项工作必须改变上述边界，不能按普通重构推进，必须先升级为跨仓合同审计包。

## 3. 优先级总表

| ID | 事项 | 类型 | 当前状态 | Blocker | 推荐优先级 | 推荐窗口 / 主线 | 证据来源 | 下一步动作 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| FW-OPT-001 | 同步固件剩余事项真相源 | 文档收口 / 防误导 | 已完成 | 否 | P1 | ESP32 固件文档治理 | `reports/task_20260517_measurement_availability_probe_policy.md`；本文件 | 已更新 `esp32_firmware_remaining_work_and_lessons.md`，后续只需保持本总表为优先级入口 |
| FW-OPT-002 | 固件协议合同 host-side 测试 / golden frame canonical fixture | 协议防漂移 / 工程门禁 | 已完成 | 否 | P2 | ESP32 固件低风险工程质量包 | `src/core/ProtocolCodec.h`；`src/HubAckBuilder.h`；`tools/run_evaluator_unit_tests.py`；`docs/protocol/golden_frames/sonicwave_ble_frames_v1.jsonl`；BLE freeze 文档 | 已覆盖 `CAP? / SNAPSHOT / WAVE:* / EVT:STREAM / EVT:STOP / EVT:SAFETY`、legacy parser 和 `HubAckBuilder` focused ACK/NACK 文本；2026-05-18 已新增 canonical golden frame fixture，并让 host evaluator 读取 fixture 校验 schema、payload budget、`ACK:CAP` 和 slim `SNAPSHOT` 当前生成输出；未改 BLE 线格式或固件 command 行为 |
| FW-OPT-003 | `HubHandler` 命令分发责任矩阵 / ACK builder 抽取 | Command owner 审计 / 低风险内部重构 | ACK builder 已完成 | 否 | P2 | ESP32 固件审计包 | `src/main.cpp`；`src/HubAckBuilder.h`；`docs/system/esp32_firmware_owner_boundary_audit.md` | 后续如继续拆，只允许继续按窄 helper 推进，不改 action owner 和 ACK 线格式 |
| FW-OPT-004 | `BleTransport` owner 边界审计 | BLE transport 结构债 | 审计已完成 / 拆分待决策 | 否 | P3 | ESP32 BLE 安全重构预研 | `src/transport/ble/BleTransport.cpp`；`docs/system/esp32_ble_safe_refactor_freeze_checklist.md`；`docs/system/esp32_firmware_owner_boundary_audit.md` | 暂不直接拆；进入实现前先跑 BLE freeze checklist 和真机 capture 计划 |
| FW-OPT-005 | `LaserModule` 深层 owner 拆分预研 | Laser measurement / start gate 结构债 | diagnostics / calibration runtime / stable contract diagnostics / stable window helper / presence counter wrapper 已完成；stable / baseline / presence 深拆审计已完成 | 否 | P3 | ESP32 Laser 结构审计 | `src/modules/laser/LaserModule.cpp`；`src/modules/laser/LaserDiagnostics.*`；`src/modules/laser/CalibrationRuntime.*`；`src/modules/laser/BaselineContractDiagnostics.*`；`src/modules/laser/LaserStableWindow.*`；`src/modules/laser/PresenceContractEvaluator.*`；已抽取 pure evaluators；`docs/system/esp32_firmware_owner_boundary_audit.md`；`docs/system/esp32_laser_stable_baseline_presence_owner_refactor_plan.md` | 已将 measurement probe / distance validity 串口 evidence 抽到 `LaserDiagnostics`，将校准模型算重和 effective-zero 选择/夹紧抽到 `CalibrationRuntime`，将 baseline contract 串口 evidence / writeback 节流抽到 `BaselineContractDiagnostics`，将 stable window metrics / trimmed mean 抽到 `LaserStableWindow`，将有效样本下的 presence enter/exit counter + evaluator 输入组装收口到 `PresenceContractEvaluator::evaluateWithCounters()`，并补 host-side tests。后续不建议继续自动深拆 stable candidate / baseline latch / presence owner state carrier / occupied-cycle owner |
| FW-OPT-006 | 固件日志 facade / release log level 评估 | 可观测性 / 发布噪声治理 | 待评估 | 否 | P3 | ESP32 日志治理 | `docs/system/firmware_log_policy.md`；现有 capture 依赖 | 仅当 release 串口噪声影响采集或用户使用时再做；禁止全仓替换 `Serial.printf` |
| FW-OPT-007 | motion safety shadow 是否进入 runtime action | 高风险行为决策 | 待数据审计 | 否 | P3 | Motion safety 专项 | `docs/system/motion_safety_*`；replay 工具 | 先审样本分布和 action gate，禁止直接把 shadow 接停波 |
| FW-OPT-008 | 旧协议 / 旧开发文档状态标注 | 文档过期治理 | 已完成第一轮 | 否 | P4 | ESP32 文档治理 | `docs/protocol.md`；`docs/firmware_developer_guide.md`；`docs/safety_design.md` | 已在旧入口标注 current / legacy / historical 状态和当前真相源；后续只在具体旧文档被继续使用时增量清理 |
| FW-OPT-009 | release hardening / minimum soak validation | 发布硬化 | 已完成当前基线 | 否 | P4 | 跨仓 release hardening | SW capture `20260518_103916...minimum_soak_release_esp32_plus`；SW release hardening 文档；本总表 | 当前 SW `52f782f` + ESP32 `5bb7e0c` 组合已通过 ESP32-plus minimum soak，audit `PASS_CANDIDATE`，无 hard failure / evidence gap；如固件或 APP commit 变化需重新跑最小 capture |
| FW-OPT-010 | 固件大 owner 受控技术债记录 | 长期结构债 / 后续治理 | 观察项 | 否 | P3 | ESP32 固件结构审计窗口 | `src/modules/laser/LaserModule.cpp`；`src/transport/ble/BleTransport.cpp`；`src/core/SystemStateMachine.cpp`；SW 报告 `reports/task_20260518_sw_app_esp32_controlled_tech_debt_record.md`；本总表 | 当前已确认 `LaserModule`、`BleTransport`、`SystemStateMachine` 仍是大 owner，但不是当前 blocker；后续只按真实问题或窄 helper 继续治理。允许优先做 diagnostics、纯计算、ACK / payload builder、host-side tests、日志证据 helper；暂不因“文件大”直接拆 BLE 生命周期、start / stop action owner、stable / baseline / presence 动作时序、motion safety runtime action 或 wave ramp / I2S 输出时序 |
| FW-OPT-011 | `STREAM:SET` 显式实时流订阅合同 | 协议合同 / Demo 调试链路 | 已完成 / 新可观测性待真机复核 | 否 | P1 | ESP32 / Demo APP 遥测与校准链路 | `docs/system/esp32_realtime_stream_subscription_contract.md`；`reports/tasks/stream_set_realtime_subscription_closure/`；capture `20260602_152844...esp32_demo_telemetry_calibration`；commit `bc18a74`；commit `a7882f9` | 已恢复 Demo 曲线和校准实时数据；旧真机 audit 为 `PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP`。已补 Demo APP `STREAM_SUBSCRIPTION_RESULT / MEASUREMENT_CONSUME_SUMMARY / CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT` 结构化日志和 audit 兼容识别；下一步只需用新 Demo APP 真机 capture 复核 evidence gap 是否消除，不改固件协议、校准算法或 MAX485 参数 |
| FW-OPT-012 | Demo APP 快速 `Start -> Stop` 控制竞态 | Demo APP 控制链修复 / 真机复核 | 已完成 | 否 | P1 | Demo APP 控制链收口 | commit `a7882f9`；`reports/task_demo_app_wave_stop_command_race_fix.md`；SW capture `20260604_164648...demo_app_wave_stop_race` | 已修复旧 Start 覆盖后发 Stop 的明确风险，真机证明 18 次 start / stop 对齐；后续如果再次复现，先采 APP raw TX、ESP32 串口接收和 `i2s_stop` 三段证据，不直接改固件协议或 `SystemStateMachine` |
| FW-OPT-013 | Demo APP Measurement display owner 抽取 | 可选重构 / 可测性提升 | 已完成第一包 | 否 | P3 | Demo APP 小包重构 | `MeasurementDisplayStore.kt`；`MeasurementDisplayStoreTest.kt`；`DemoViewModel.kt`；`DemoMeasurementTrace.kt`；`reports/task_20260606_demo_app_refactor_start_audit.md`；`reports/task_20260606_demo_app_measurement_display_store.md`；本总表 | 已抽取 `Event.StreamSample -> TelemetryPointUi / measurement display state` 小 owner，并补 JVM tests：carrier policy、valid sample、invalid sample、moving average reset、buffer trim、reset；后续不得把此项解读为校准、连接、导出或 BLE 合同已经重构 |
| FW-OPT-014 | Demo APP Calibration session owner 抽取 | 可选重构 / 校准流程治理 | 已完成 | 否 | P3 | Demo APP 小包重构 | `CalibrationSessionStore.kt`；`CalibrationSessionStoreTest.kt`；`DemoViewModel.kt`；`CalibrationToolsSection.kt`；`reports/task_20260606_demo_app_refactor_start_audit.md`；`reports/task_20260606_demo_app_refactor_master_plan.md`；`reports/task_20260606_demo_app_calibration_session_store.md`；本总表 | 已抽 `CalibrationSessionStore`，迁移 capture availability、点集 append、comparison rebuild、prepared model parse、model option sync 和 reset；保留 APP live snapshot / legacy calibration point 两条入口行为、`client.send`、ACK/NACK/Error 接入顺序和 capture 日志文案。测试覆盖：capture availability、手动模型解析失败/成功、有效点触发 fit、切换二次模型、无效点不进 fit、reset 清理 |
| FW-OPT-015 | DemoViewModel 大范围重构 | 高风险冻结项 / 架构债 | 冻结 | 否 | P4 | Demo APP 架构预研 | `DemoViewModel.kt`；FW-OPT-013；FW-OPT-014；`reports/task_20260606_demo_app_refactor_start_audit.md` | 当前不作为待办，不因“文件大”直接拆。只有完成 FW-OPT-013 / FW-OPT-014 或出现新的真实 blocker 后，才评估连接 owner、protocol event reducer、test-session/export owner 等更大范围拆分；禁止一次性全量重构 |
| FW-OPT-016 | Demo APP Raw console owner 抽取 | 低风险重构 / 日志 UI 可测性 | 已完成 | 否 | P3 | Demo APP 小包重构 | `RawConsoleStore.kt`；`RawConsoleStoreTest.kt`；`DemoViewModel.kt`；`RawConsoleSection.kt`；`reports/task_20260606_demo_app_refactor_master_plan.md`；`reports/task_20260606_demo_app_raw_console_store.md`；本总表 | 已抽 `RawConsoleStore`，迁移 raw log buffer、`MAX_RAW_LOG_LINES`、stream raw 过滤、high-priority publish 判定和 `RawConsoleUiState` 生成；保留 `appendSystemLog` 业务调用点和 capture log 文案。测试覆盖：普通行追加、`EVT:STREAM` 默认过滤、verbose 保留、CSV fallback 过滤、high-priority 强制 publish、max lines trim |
| FW-OPT-017 | Demo APP Wave control state reducer | 中风险重构 / 控制状态可测性 | 第二阶段已完成 / 运行页 smoke 已通过 | 否 | P3 | Demo APP 控制链小包 | `WaveControlStateReducer.kt`；`WaveControlStateReducerTest.kt`；`WaveLifecycleCommandGate.kt`；`WaveLifecycleCommandGateTest.kt`；`WavePendingLifecycleStore.kt`；`WavePendingLifecycleStoreTest.kt`；`DemoStartReadyRegressionTest.kt`；`DemoViewModel.kt`；capture `20260607_152955...demo_app_quality_smoke`；capture `20260607_161021...demo_app_quality_smoke`；`reports/task_20260606_demo_app_refactor_master_plan.md`；`reports/task_20260606_demo_app_wave_control_state_reducer.md`；`reports/task_20260607_demo_app_wave_pending_lifecycle_store.md`；本总表 | 第一阶段已抽 pure reducer：wave runtime transition、formal wave truth sync、pending flags、snapshot start_ready merge、authoritative wave output、optimistic stop state。第二阶段已抽 `WavePendingLifecycleStore`：start request、stop request、stop completion、pending idle 判定和 timestamp 生成。保留 `client.send`、truth refresh job、BLE command 时序、`WaveLifecycleCommandGate` token 调用、UI notice、system log 和 test session finish 副作用。运行页 Start -> Stop 有 ESP32 `WAVE:START / WAVE:STOP / STOP_SUMMARY` 证据；完整 quality baseline 已通过。不得继续迁 command send |
| FW-OPT-018 | Demo APP Motion sampling session owner | 可选重构 / 采样链可测性 | 已完成 | 否 | P4 | Demo APP 采样链小包 | `MotionSamplingSessionStore.kt`；`MotionSamplingSessionStoreTest.kt`；`MotionSamplingExporter.kt`；`MotionSamplingSection.kt`；`DemoViewModel.kt`；`reports/task_20260606_demo_app_refactor_master_plan.md`；`reports/task_20260606_demo_app_motion_sampling_session_store.md`；本总表 | 已抽 `MotionSamplingSessionStore`：start/stop/clear、row build、dd/dt/dw/dt、export metadata update、session snapshot 回填。保留 exporter IO、`client.send(MotionSamplingModeSet)`、中文状态文案和 system log 调用点。测试覆盖 start metadata、first/second row delta、inactive no append、stop endedAt、clear gating、export metadata |
| FW-OPT-019 | Demo APP UI presentation model 收口 | 可选重构 / Compose 参数瘦身 | 第二阶段已完成 | 否 | P4 | Demo APP UI 小包 | `SectionPresentationModels.kt`；`MainScreen.kt`；`CalibrationToolsSection.kt`；`MotionSamplingSection.kt`；`WaveControlBottomBar.kt`；`TestSessionSection.kt`；`RawConsoleSection.kt`；`reports/task_20260606_demo_app_refactor_master_plan.md`；`reports/task_20260606_demo_app_presentation_model_stage1.md`；`reports/task_20260607_demo_app_presentation_model_stage2.md`；本总表 | 已新增 section-specific actions DTO：`CalibrationToolsActions` / `MotionSamplingActions` / `DeviceToolsActions` / `WaveControlActions` / `TestSessionActions` / `RawConsoleActions`，收口 calibration、motion sampling、型号/保护、底部运行控制、测试会话和日志 section 参数面。保留 UI local `rememberSaveable` 状态、视觉交互、ViewModel 行为、BLE command 和业务 owner。后续如继续，只做小范围 section props 或局部 composable 拆分，不做导航 / 多 ViewModel / 视觉重写 |
| FW-OPT-020 | Demo APP Device config write tracker | 可选重构 / 配置写入反馈可测性 | 已完成 | 否 | P4 | Demo APP 配置写入小包 | `DeviceConfigWriteTracker.kt`；`DeviceConfigWriteTrackerTest.kt`；`DemoViewModel.kt`；`reports/task_20260606_demo_app_refactor_master_plan.md`；`reports/task_20260606_demo_app_device_config_write_tracker.md`；本总表 | 已抽 `DeviceConfigWriteTracker`：pending request、observed config match、mismatch keep pending、generic ACK fallback、NACK/Error/timeout clear pending、confirmation refresh gate。保留 `client.send(DeviceSetConfig)`、watchdog job、snapshot/capability refresh、中文状态文案和 system log 调用点 |
| FW-OPT-021 | Demo APP quality baseline smoke | 真机 smoke / 重构阶段证据链 | 已通过 / 有观察项 | 否 | P4 | Demo APP 阶段收口 | `tools/demo_app_quality_smoke_capture.sh`；`tools/demo_app_quality_smoke_audit.py`；capture `20260607_101316...demo_app_quality_smoke`；capture `20260607_154532...demo_app_quality_smoke`；capture `20260607_161021...demo_app_quality_smoke`；`reports/task_20260607_demo_app_quality_baseline_smoke_prep.md`；本总表 | 完整 UI baseline 与运行页设备闭环补采合并通过：信息架构 / 型号 / 校准 / 采样 / 运行 / 日志 marker 已覆盖，ESP32 采到 `WAVE:START / START ALLOW / WAVE:STOP / STOP REQUEST / i2s_stop / STOP_SUMMARY`。`tools/demo_app_quality_smoke_capture.sh` 默认改用 PlatformIO monitor 采 ESP32 串口；device config 写入仍按现场安全条件可选，未作为 blocker。后续若 UI 瞬态异常复现，先补采集底座或启用更完整 logcat |
| FW-OPT-022 | Demo APP information architecture simplification | UI 信息架构 / 默认首页精简 | 第十阶段已完成 / baseline smoke 已通过 | 否 | P3 | Demo APP 可用性提升 | `MainScreen.kt`；`DeviceProfileSection.kt`；`FallStopProtectionSection.kt`；`CalibrationToolsSection.kt`；`MotionSamplingSection.kt`；`TelemetryChartSection.kt`；`TestSessionSection.kt`；`SystemStatusSection.kt`；`DemoViewModel.kt`；`ProtocolCodec.kt`；`SonicWaveClient.kt`；capture `20260607_154532...demo_app_quality_smoke`；capture `20260607_161021...demo_app_quality_smoke`；`reports/task_20260607_demo_app_information_architecture_simplification.md`；本总表 | 已把默认入口改为固定顶部 `型号 / 校准 / 采样 / 运行 / 日志`，默认进入型号页；型号页主操作为设置 Base / Plus，Pro / Ultra 置灰，新增 `当前设备` 直接显示设备回传的 Base/Plus 与距离传感器状态，写入状态主卡片可见，多余状态收进详情，连接状态卡片移除。保护卡片默认只保留摔倒保护 / 律动离开两个开关，律动离开按固件既有 `SAFETY:LEAVE_PROTECTION` 合同接入 Demo protocol/sdk/ViewModel。`SCALE:ZERO` / `CAL:ZERO` 增加发送前确认，取消不发送；已写入撤回不在当前合同内。校准页已删除重复标题和常驻说明，主操作压缩为开始校准/设备归零、结束校准、参考重量/实时距离、记录/清空校准点、采集数据、曲线、线性/二次拟合状态、待写入摘要和写入模型；高级工程区已删除冗余说明和旧 Z/K 校准路径。采样页完成第一轮内部深压缩，直接 `开始采样` 即可记录 APP 采样会话，设备侧 `DEBUG:MOTION_SAMPLING` 开关收进 `采样设置`。运行页删除旧交付边界卡片，曲线设置和测试会话导出路径默认收起，compact 系统状态完成尺寸和文案密度优化。当前 happy path 已通过，不需要重复同一 baseline |
| FW-OPT-023 | Demo APP TestSessionBridge | 中风险重构 / 运行页测试会话 owner | 第一阶段已完成 / B6 smoke 已通过 | 否 | P3 | Demo APP 运行页小包 | `TestSessionBridge.kt`；`TestSessionBridgeTest.kt`；`DemoViewModel.kt`；`tools/demo_app_quality_smoke_capture.sh`；capture `20260607_150634...demo_app_quality_smoke`；capture `20260607_161021...demo_app_quality_smoke`；`reports/task_20260607_demo_app_test_session_bridge_stage1.md`；本总表 | 已抽运行页测试会话与 formal wave truth 的窄 bridge：start、clear、export metadata、sample append、finish、`TEST:START` / `STOP_SUMMARY`、inactive truth stop plan、sample build。保留 `client.send`、pending request、truth refresh、日志、notice、panel publish 和 exporter IO 在 ViewModel。B6 Start -> Stop 有 ESP32 `WAVE:START / WAVE:STOP / STOP_SUMMARY` 证据，完整 quality baseline 已通过；不建议在同一包继续迁 command send 或完整 event reducer |

## 4. 重构收口与真机测试原则

当前重构收口结论：

- 没有必须继续重构的 ESP32 固件 blocker。
- 没有必须继续重构的 Demo APP blocker；当前 Demo APP 可继续运行、联调和真机验证。
- 不继续深拆不会阻断联调、release hardening 或 minimum soak。
- 剩余偏重 owner 主要是动作编排 owner，保留比强拆更安全。
- 后续只有在真机证据证明存在实际问题时，才提升对应专项优先级。
- B2-B8 多包重构与 B10 信息架构阶段已经通过 Demo APP quality baseline smoke；后续不需要为了当前阶段继续补测同一 happy path。
- Demo APP 信息架构第十阶段已完成本地 compile/test/assemble 和真机 baseline smoke，已确认固定 Tab、默认型号页、型号设置精简、当前设备真值显示、保护双开关、内容不重不漏、归零确认弹窗、校准页压缩、采样页默认紧凑、直接开始采样可用、运行页无旧交付边界且系统状态更紧凑。
- Demo APP `TestSessionBridge` 第一阶段已完成本地 test/assemble，并已补 B6 运行页轻量 smoke；Start -> Stop 有 ESP32 `WAVE:START / WAVE:STOP / STOP_SUMMARY` 证据。完整 Demo APP quality baseline 已在 `20260607_154532` + `20260607_161021` 两轮证据合并后通过。
- Demo APP `WavePendingLifecycleStore` 已完成本地 test/assemble，并已补运行页真机 smoke；Start -> Stop 有 ESP32 `WAVE:START / WAVE:STOP / STOP_SUMMARY` 证据。完整 Demo APP quality baseline 已在 `20260607_154532` + `20260607_161021` 两轮证据合并后通过。

Demo APP 后续重构启动原则：

- 先复核 FW-OPT-011 的新可观测性证据链，证明 `STREAM_SUBSCRIPTION_RESULT / MEASUREMENT_CONSUME_SUMMARY / CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT` 能消除旧 evidence gap。
- 如果复核通过，Demo APP 可保持现状，不需要马上重构。
- 如果复核仍暴露消费层或校准录点归因缺口，优先启动 FW-OPT-013 或 FW-OPT-014 的小包重构。
- 禁止第一刀直接做 Demo APP 全量重构；不得在同一包里同时拆连接、控制、遥测、校准、采样、导出和 UI 状态。
- 重构包默认不改 `EVT:STREAM` wire payload、`STREAM:SET` 合同、固件校准算法、MAX485 参数或正式 SW APP 默认行为。

下一轮真机测试是否需要收集日志：

- 需要。只要烧录了 `1759a12` 或之后的新固件并要给出“通过 / 不通过”结论，就应使用 capture 收集日志。
- 原因是本轮虽然主要是内部 helper 抽取，但已改变固件 commit 组合；真机测试目标不是证明代码逻辑 diff，而是证明现场行为没有回归。
- 日志至少要覆盖 APP focus/runtime events、ESP32 串口、session meta、notes / marker。
- 用户只需要按页面和设备真实流程操作；AI 负责复核 capture 产物后再给正式结论。

建议下一轮真机测试关注：

- BLE connect / reconnect / snapshot refresh。
- `CAP? / SNAPSHOT / WAVE:START / WAVE:STOP` 主链。
- PLUS normal / degraded measurement 行为。
- `MEASUREMENT_PROBE` 串口 evidence 是否仍可读。
- `BASELINE_CONTRACT` / start_ready / baseline_ready 是否无异常抖动。
- Android `CONNECT_SNAPSHOT_REFRESH_FAILED=0`。

不建议只做人工体感测试后直接写“通过”。如果 capture 文件为空、缺 ESP32 串口、缺 APP runtime events 或无法对齐 marker，只能写成“体感通过但证据不足”。

## 5. 已完成但仍需留证的事项

| 事项 | 当前状态 | 证据 | 后续注意 |
| --- | --- | --- | --- |
| SafetyActionContractEvaluator 最小实现 | 已完成 | `tools/run_evaluator_unit_tests.py`；PlatformIO 构建；真机 smoke | 不再作为待办；继续保持 `SystemStateMachine` 是最终 action owner |
| StopOutcomeSummaryEvaluator / STOP_SUMMARY 分类收口 | 已完成 | `reports/task_20260428_stop_reason_run_summary_evt_stop_consistency_audit.md`；本地验证 | `RunSummaryCollector` 仍是串口 evidence，不升级为 APP truth source |
| ESP32-base 整机 smoke 与串口补证据 | 已完成 | `esp32_base_platformio_serial_evidence` capture 记录 | base 当前不是 blocker |
| PLUS degraded-start 合同链路 | 已通过 | `esp32_plus_degraded_start_smoke` capture 记录 | 不代表 measurement unavailable 已自动健康 |
| Wave output startup observability | 已完成 | `reports/task_20260430_plus_degraded_wave_output_observability.md` | `WAVE_OUTPUT_STARTUP` 是串口 evidence，不改 BLE 合同 |
| MeasurementAvailabilityProbePolicy | 已完成 | commit `9fab932`；`reports/task_20260517_measurement_availability_probe_policy.md`；真机 capture；SW release hardening capture `20260518_103916...minimum_soak_release_esp32_plus` | `MEASUREMENT_PROBE` 只作为串口 evidence，不进入 BLE 正式合同 |

## 6. 推荐执行顺序

### 已完成：P1 文档真相源收口

目标：

- 已更新 `docs/system/esp32_firmware_remaining_work_and_lessons.md`。
- 已把 measurement probe 从“建议实现”改为“已完成 + 本地验证 + 真机 capture 通过”。
- 已将后续工作入口指向本总表。

验证：

- `git diff --check`

### 已完成：P2 协议合同测试

目标：

- 已在 host-side evaluator test 入口补 `ProtocolCodec` focused tests。
- 已覆盖命令解析和关键输出字段，不改协议实现语义。
- 已新增 canonical golden frame fixture，并由 host-side evaluator 读取。

建议覆盖：

- `CAP?` / `SNAPSHOT?` query 识别。
- `WAVE:SET / WAVE:START / WAVE:STOP`。
- `DEBUG:DEGRADED_START`。
- `SAFETY:LEAVE_PROTECTION` / `DEBUG:FALL_STOP`。
- legacy `F/I/E` 不绕过状态机 owner 的解析形态。
- `ProtocolCodec::encodeSnapshot()` 必须包含 APP 当前依赖的字段。
- `EVT:STREAM valid=0` 必须带 `reason`。
- `EVT:STOP` / `EVT:SAFETY` 必须带 reason / code / effect / state。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`
- 如触碰 simulator shared headers，再跑 `python3 -m platformio run -e esp32_plus_laser_sim`

### 已完成：P2 / P3 结构审计

目标：

- 已审 `HubHandler`、`BleTransport`、`LaserModule` 的责任边界。
- 已输出 owner 矩阵、可安全抽取项、禁止迁移项、验证矩阵。
- 未在审计包里拆 BLE 合同或 action timing。

### 已完成：HubHandler ACK builder 最小抽取

建议：

- 已完成 `HubHandler` ACK builder 内部抽取。
- `HubHandler` 仍负责 action owner 调用和命令时机。
- `HubAckBuilder` 只负责稳定 `ACK:* / NACK:*` 文本。
- 不默认进入 `BleTransport` 实际拆分。
- 不默认进入 `LaserModule` start gate / safety timing 拆分。

### 已完成：A1-A3 低风险工程标准化收口

建议：

- 已同步 minimum soak 通过状态到固件长期优先级入口。
- 已补强 `HubAckBuilder` focused tests，锁住 ACK/NACK 文本。
- 已标注旧协议 / 旧开发文档入口的 current / legacy / historical 状态。
- 不默认进入 `BleTransport` 实际拆分。
- 不默认进入 `LaserModule` start gate / safety timing 拆分。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

### 已完成：A4 Laser diagnostics / evidence helper

目标：

- 新增 `src/modules/laser/LaserDiagnostics.h/.cpp`。
- 将 `MEASUREMENT_PROBE` 串口 evidence 和 distance valid / invalid diagnostics 从 `LaserModule` 迁到 helper。
- `LaserModule` 仍负责何时调用、节流状态、measurement/probe/runtime 行为。

边界：

- 不改 measurement 读取节奏。
- 不改 `MeasurementAvailabilityProbePolicy` 状态机。
- 不改 `EVT:STREAM` 发布语义。
- 不改 stable / baseline / start-ready 时机。
- 不改 safety / stop action。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

### 已完成：A5 CalibrationRuntime / runtime-zero pure helper

目标：

- 新增 `src/modules/laser/CalibrationRuntime.h/.cpp`。
- 将 calibration weight 计算、negative clamp、effective-zero fallback / runtime-zero clamp / locked zero 选择从 `LaserModule` 迁到纯 helper。
- `LaserModule` 仍负责校准模型存储、何时 reset runtime-zero、何时 observe runtime-zero、何时 refresh effective-zero。

边界：

- 不改 `Preferences` 存储 key。
- 不改 calibration model validation。
- 不改 runtime-zero refresh eligibility / window 判定。
- 不改 stable / baseline / start-ready 时机。
- 不改 BLE 线格式和 APP 消费语义。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

### 已完成审计：Laser stable / baseline / presence owner plan

目标：

- 已新增 `docs/system/esp32_laser_stable_baseline_presence_owner_refactor_plan.md`。
- 已明确 stable / baseline / presence 后续只能先做日志、evidence、纯 helper 抽取。
- 已冻结 `setRuntimeReady`、`setStartReadiness`、`onUserOff`、baseline latch/clear、
  occupied-cycle lock/release 等动作时序。

低风险下一包：

- `LSP-001 Stable Contract Logging Helper` 已完成。
- `LSP-002 Stable Window Metrics Helper Tests` 已完成。

边界：

- 不改 `SNAPSHOT.start_ready` / `SNAPSHOT.baseline_ready` / `EVT:BASELINE`。
- 不改 Android APP 消费语义。
- 不改 `SystemStateMachine` final start / stop / safety action owner。

验证：

- 文档审计包只需 `git diff --check`。

### 已完成：LSP-001 / LSP-002 Stable contract diagnostics + stable window tests

目标：

- 新增 `src/modules/laser/BaselineContractDiagnostics.h/.cpp`。
- 将 `BASELINE_CONTRACT` latch / clear / start_ready_writeback 串口 evidence 和
  writeback 节流状态从 `LaserModule` 迁到 helper。
- 新增 `src/modules/laser/LaserStableWindow.h/.cpp`。
- 将 stable window metrics 和 trimmed mean 从 `LaserModule` 匿名 helper 迁到
  pure helper，并在 host-side evaluator tests 中锁住现有行为。

边界：

- 不移动 `clearStableContractBridge`。
- 不移动 `sm->setRuntimeReady`、`sm->setStartReadiness`、`sm->onUserOff`。
- 不改 `SNAPSHOT.start_ready` / `SNAPSHOT.baseline_ready` / `EVT:BASELINE`。
- 不改 stable latch 阈值、stable build interval 或 baselineReady latch 条件。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

### 已完成：LSP-003 Presence counter decision wrapper

目标：

- 在 `PresenceContractEvaluator` 中新增 `evaluateWithCounters()`。
- 将有效测量样本下的 enter / exit confirm count 更新和 evaluator 输入组装从
  `LaserModule::updatePresenceState()` 收口到 pure helper。
- 补 host-side focused tests，覆盖 enter pending / confirmed、exit pending /
  confirmed、deadband reset 和 counter saturation。

边界：

- 不处理 `invalidPresenceSamples`。
- 不调用 `sm->onUserOff()`。
- 不调用 `sm->setRuntimeReady()`。
- 不改变 RUNNING 下 invalid presence 保护行为。
- 不改 baseline latch / clear。
- 不改 occupied-cycle lock / release 时机。

验证：

- `git diff --check`
- `python3 tools/run_evaluator_unit_tests.py`
- `python3 -m platformio run -e esp32s3`

## 7. 后续协作默认读取顺序

1. `SW_ESP3_Firmware/AGENTS.md`
2. `SW_ESP3_Firmware/docs/system/esp32_firmware_optimization_priority_table.md`
3. `SW/docs/system/ESP32与APP联调优化优先级总表.md`
4. `SW_ESP3_Firmware/docs/system/esp32_ble_safe_refactor_freeze_checklist.md`
5. `SW_ESP3_Firmware/docs/system/firmware_safety_behavior.md`
6. `SW_ESP3_Firmware/docs/start-readiness-contract.md`
