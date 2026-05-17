# ESP32 Firmware Optimization Priority Table

最后更新时间：2026-05-17

## 1. 当前结论

ESP32 固件当前主链可继续作为联调和阶段交付基线。`PLUS + laser_installed=1 + measurement unavailable` 下的 degraded measurement circuit-breaker / low-frequency probe 已完成实现、本地验证和真机 capture 复核。

按成熟工程标准看，当前剩余工作主要是协议防漂移、owner 边界审计、文档真相源收口和发布硬化；没有新的必须立刻阻断联调的固件 blocker。

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
| FW-OPT-002 | 固件协议合同 host-side 测试 | 协议防漂移 / 工程门禁 | 已完成 | 否 | P2 | ESP32 固件低风险工程质量包 | `src/core/ProtocolCodec.h`；`tools/run_evaluator_unit_tests.py`；BLE freeze 文档 | 已覆盖 `CAP? / SNAPSHOT / WAVE:* / EVT:STREAM / EVT:STOP / EVT:SAFETY` 和 legacy parser；后续触碰协议时继续扩展 |
| FW-OPT-003 | `HubHandler` 命令分发责任矩阵 | Command owner 审计 | 审计已完成 / 拆分待决策 | 否 | P2 | ESP32 固件审计包 | `src/main.cpp`；`docs/system/esp32_firmware_owner_boundary_audit.md` | 如继续实现，优先只抽 ACK builder，不改 action owner 和 ACK 线格式 |
| FW-OPT-004 | `BleTransport` owner 边界审计 | BLE transport 结构债 | 审计已完成 / 拆分待决策 | 否 | P3 | ESP32 BLE 安全重构预研 | `src/transport/ble/BleTransport.cpp`；`docs/system/esp32_ble_safe_refactor_freeze_checklist.md`；`docs/system/esp32_firmware_owner_boundary_audit.md` | 暂不直接拆；进入实现前先跑 BLE freeze checklist 和真机 capture 计划 |
| FW-OPT-005 | `LaserModule` 深层 owner 拆分预研 | Laser measurement / start gate 结构债 | 审计已完成 / 拆分待决策 | 否 | P3 | ESP32 Laser 结构审计 | `src/modules/laser/LaserModule.cpp`；已抽取 pure evaluators；`docs/system/esp32_firmware_owner_boundary_audit.md` | 如继续实现，优先抽日志 / evidence helper；不迁移 safety / stop action |
| FW-OPT-006 | 固件日志 facade / release log level 评估 | 可观测性 / 发布噪声治理 | 待评估 | 否 | P3 | ESP32 日志治理 | `docs/system/firmware_log_policy.md`；现有 capture 依赖 | 仅当 release 串口噪声影响采集或用户使用时再做；禁止全仓替换 `Serial.printf` |
| FW-OPT-007 | motion safety shadow 是否进入 runtime action | 高风险行为决策 | 待数据审计 | 否 | P3 | Motion safety 专项 | `docs/system/motion_safety_*`；replay 工具 | 先审样本分布和 action gate，禁止直接把 shadow 接停波 |
| FW-OPT-008 | 旧协议 / 旧开发文档状态标注 | 文档过期治理 | 待处理 | 否 | P4 | ESP32 文档治理 | `docs/protocol.md`；`docs/firmware_developer_guide.md`；`docs/safety_design.md` | 标注 legacy / historical / current truth source，避免后续 AI 误读旧文档 |
| FW-OPT-009 | release hardening / minimum soak validation | 发布硬化 | 待规划 | 否 | P4 | 跨仓 release hardening | 真机 capture 报告；SW 优先级总表 | 固定 Android + ESP32 组合、capture profile、最小 soak 入口 |

## 4. 已完成但仍需留证的事项

| 事项 | 当前状态 | 证据 | 后续注意 |
| --- | --- | --- | --- |
| SafetyActionContractEvaluator 最小实现 | 已完成 | `tools/run_evaluator_unit_tests.py`；PlatformIO 构建；真机 smoke | 不再作为待办；继续保持 `SystemStateMachine` 是最终 action owner |
| StopOutcomeSummaryEvaluator / STOP_SUMMARY 分类收口 | 已完成 | `reports/task_20260428_stop_reason_run_summary_evt_stop_consistency_audit.md`；本地验证 | `RunSummaryCollector` 仍是串口 evidence，不升级为 APP truth source |
| ESP32-base 整机 smoke 与串口补证据 | 已完成 | `esp32_base_platformio_serial_evidence` capture 记录 | base 当前不是 blocker |
| PLUS degraded-start 合同链路 | 已通过 | `esp32_plus_degraded_start_smoke` capture 记录 | 不代表 measurement unavailable 已自动健康 |
| Wave output startup observability | 已完成 | `reports/task_20260430_plus_degraded_wave_output_observability.md` | `WAVE_OUTPUT_STARTUP` 是串口 evidence，不改 BLE 合同 |
| MeasurementAvailabilityProbePolicy | 已完成 | commit `9fab932`；`reports/task_20260517_measurement_availability_probe_policy.md`；真机 capture | `MEASUREMENT_PROBE` 只作为串口 evidence，不进入 BLE 正式合同 |

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

### 下一包：可选低风险实现

建议：

- 如果继续工程质量优化，优先做 `HubHandler` ACK builder 内部抽取。
- 不默认进入 `BleTransport` 实际拆分。
- 不默认进入 `LaserModule` start gate / safety timing 拆分。

## 6. 后续协作默认读取顺序

1. `SW_ESP3_Firmware/AGENTS.md`
2. `SW_ESP3_Firmware/docs/system/esp32_firmware_optimization_priority_table.md`
3. `SW/docs/system/ESP32与APP联调优化优先级总表.md`
4. `SW_ESP3_Firmware/docs/system/esp32_ble_safe_refactor_freeze_checklist.md`
5. `SW_ESP3_Firmware/docs/system/firmware_safety_behavior.md`
6. `SW_ESP3_Firmware/docs/start-readiness-contract.md`
