# ESP32 Firmware Optimization Priority Table

最后更新时间：2026-05-18

## 1. 当前结论

ESP32 固件当前主链可继续作为联调和阶段交付基线。`PLUS + laser_installed=1 + measurement unavailable` 下的 degraded measurement circuit-breaker / low-frequency probe 已完成实现、本地验证和真机 capture 复核。

按成熟工程标准看，当前剩余工作主要是协议防漂移、owner 边界审计、文档真相源收口和发布硬化；没有新的必须立刻阻断联调的固件 blocker。2026-05-18 SW release hardening 窗口已完成 ESP32-plus minimum soak 真机 capture 复核，当前 SW `52f782f` + ESP32 `5bb7e0c` 组合可作为本轮 release hardening 基线。

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

## 4. 已完成但仍需留证的事项

| 事项 | 当前状态 | 证据 | 后续注意 |
| --- | --- | --- | --- |
| SafetyActionContractEvaluator 最小实现 | 已完成 | `tools/run_evaluator_unit_tests.py`；PlatformIO 构建；真机 smoke | 不再作为待办；继续保持 `SystemStateMachine` 是最终 action owner |
| StopOutcomeSummaryEvaluator / STOP_SUMMARY 分类收口 | 已完成 | `reports/task_20260428_stop_reason_run_summary_evt_stop_consistency_audit.md`；本地验证 | `RunSummaryCollector` 仍是串口 evidence，不升级为 APP truth source |
| ESP32-base 整机 smoke 与串口补证据 | 已完成 | `esp32_base_platformio_serial_evidence` capture 记录 | base 当前不是 blocker |
| PLUS degraded-start 合同链路 | 已通过 | `esp32_plus_degraded_start_smoke` capture 记录 | 不代表 measurement unavailable 已自动健康 |
| Wave output startup observability | 已完成 | `reports/task_20260430_plus_degraded_wave_output_observability.md` | `WAVE_OUTPUT_STARTUP` 是串口 evidence，不改 BLE 合同 |
| MeasurementAvailabilityProbePolicy | 已完成 | commit `9fab932`；`reports/task_20260517_measurement_availability_probe_policy.md`；真机 capture；SW release hardening capture `20260518_103916...minimum_soak_release_esp32_plus` | `MEASUREMENT_PROBE` 只作为串口 evidence，不进入 BLE 正式合同 |

## 5. 推荐执行顺序

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

## 6. 后续协作默认读取顺序

1. `SW_ESP3_Firmware/AGENTS.md`
2. `SW_ESP3_Firmware/docs/system/esp32_firmware_optimization_priority_table.md`
3. `SW/docs/system/ESP32与APP联调优化优先级总表.md`
4. `SW_ESP3_Firmware/docs/system/esp32_ble_safe_refactor_freeze_checklist.md`
5. `SW_ESP3_Firmware/docs/system/firmware_safety_behavior.md`
6. `SW_ESP3_Firmware/docs/start-readiness-contract.md`
