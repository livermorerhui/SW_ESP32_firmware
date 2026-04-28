# ESP32 Firmware Remaining Work And Lessons

最后更新时间：2026-04-28

## 1. 当前可交付点

当前 `ESP32-plus 正常版` 可以作为阶段性交付基线：

- SW APP 与 ESP32-plus 正常版的连接、开始、停止、手动断开重连已通过真机 smoke。
- Android connect gate 已收紧到 `protocolReady=true` 后才算连接完成。
- baseline / start gate / reconnect snapshot 补偿已有 APP 日志和 ESP32 串口证据。
- PLUS measurement / RS485 在 Demo APP 只连接不律动观察中未复现 transient。

当前不应对外承诺：

- `ESP32-base` 已完成新一轮整机复核。
- `ESP32-plus degraded / 485 或 laser 故障形态` 已完成新一轮整机复核。
- motion safety 新 detector 已接入 runtime 停波动作。
- 四端 release hardening、长时间 soak、发布矩阵已全部完成。

## 2. 剩余工作优先级

### P1: SafetyActionContractEvaluator 最小实现

目标：

- 把 `FALL_SUSPECTED`、律动保护开关、stop reason/source fallback 的纯判定逻辑抽成 `SafetyActionContractEvaluator`。
- 继续保持 `SystemStateMachine` 作为唯一 final safety action / stop action owner。

当前状态：

- 2026-04-28 已完成最小实现。
- `git diff --check` 通过。
- `python3 tools/run_evaluator_unit_tests.py` 通过。
- `python3 -m platformio run -e esp32s3` 通过。
- 2026-04-28 最小真机 smoke `safety_action_log_policy_smoke_retry` 通过：SW APP 连接、开始、停止、手动断开/重连体感正常；Android 侧 `CONNECT_SUCCESS=2`、`DEVICE_SNAPSHOT_SYNCED=20`、`CONNECT_SNAPSHOT_REFRESH_FAILED=0`、`start_confirmed_by_device=2`、`stop_confirmed_by_device=1`；ESP32 串口真实可用，采到 `START ALLOW=1`、`STOP_SUMMARY=1`、`reconnect_snapshot_compensated=2`，无 reset/panic/Brownout/Guru、无 `MEASUREMENT_TRANSIENT`、无 Modbus read fail。
- 同包补充 BLE TX 高频日志成熟化第一刀：新增 `FirmwareLogPolicy`，让非关键 `send_skipped` 与 `BLE_TX_PRESSURE` 走统一节流；关键 reconnect truth 仍保留。
- `send_skipped` 断链阶段已从高频刷屏降为约 2 秒一条；`BLE_TX_PRESSURE` 仍作为周期性诊断摘要保留，后续如需要发布态更安静，可继续做 debug/release log level facade，不抢占本包。

允许迁移：

- `FallStopActionDecision` DTO。
- fall stop enabled / disabled 判定。
- stop reason / stop source fallback 的纯函数。

禁止迁移：

- `enterBlockingFault`
- `enterRecoverablePause`
- `requestStop`
- `emitStopEvent`
- `emitSafety`
- `emitFault`
- `WaveModule::stopSoft`
- `onUserOff`
- `onBleDisconnected`
- `setSensorHealthy`

后续验证要求：

- 已完成自动化验证。
- 已完成最小真机 smoke；后续不需要为了本包重复长时间 soak。
- 不需要长时间 soak。

### P2: StopReason / RunSummary / EVT:STOP 一致性审计

目标：

- 对齐 `EVT:STOP`、`STOP_SUMMARY`、`SNAPSHOT.current_reason_code`、Android session stop reason 的语义边界。
- 确认哪些是正式合同，哪些只是串口 evidence。

当前状态：

- 2026-04-28 审计完成，结论见 `reports/task_20260428_stop_reason_run_summary_evt_stop_consistency_audit.md`。
- 2026-04-28 已完成 `StopOutcomeSummaryEvaluator` 最小纯函数抽取。
- `RunSummaryCollector` 仍只输出串口 evidence，不成为正式 stop owner。
- `STOP_SUMMARY / ABORT_SUMMARY` 的字段名不变；分类由 `FaultCode != NONE` 改为基于已发布 stop context 的 `SafetySignalKind`：
  - `NONE` -> `STOP_SUMMARY result=NORMAL`
  - `RECOVERABLE_PAUSE` -> `STOP_SUMMARY result=RECOVERABLE_PAUSE`
  - `WARNING_ONLY` -> `STOP_SUMMARY result=WARNING_ONLY`
  - `ABNORMAL_STOP` -> `ABORT_SUMMARY result=ABNORMAL_STOP`
- `git diff --check`、`python3 tools/run_evaluator_unit_tests.py`、`python3 -m platformio run -e esp32s3` 通过。

不应直接做：

- 不改 BLE 线格式。
- 不改 Android session owner。
- 不把串口 summary 升级成 APP 正式 truth source。

后续验证要求：

- 因为本包只影响串口 summary 分类，不改 BLE 线格式和 action timing，自动化验证已覆盖构建与纯函数分支。
- 如果进入正式收口，建议做一次最小真机 smoke，重点看手动停止仍为 `STOP_SUMMARY result=NORMAL`，离台可恢复暂停不再被归到 `ABORT_SUMMARY result=ABNORMAL_STOP`。

### P2: motion safety shadow 到 runtime action 决策

目标：

- 判断新 detector 是继续保持 shadow / offline replay，还是进入 runtime action。
- 重点解决 `USER_LEFT_PLATFORM` 与 `FALL_SUSPECTED` 的仲裁边界。

前置条件：

- 使用已有导出样本和必要的定向补样本。
- 先审数据分布，再决定 runtime gate。
- 接 runtime 前必须有更明确的 action gate 和回滚方案。

不应直接做：

- 不把 shadow 结果直接接停波。
- 不用单次体感事件升级正式分类。

### P3: ESP32-base 整机复核

目标：

- 在 PLUS 正常主链稳定后，复核 base 形态连接、开始、停止、离台、安全状态。

原因：

- base 是 PLUS 正常形态的简化形态。
- 之前 base 测试可能只覆盖 ESP32 模块，不足以代表整机链路。

### P3: ESP32-plus degraded / 故障形态复核

目标：

- 复核 485 / laser 故障、measurement unavailable、degraded start、APP 展示、repair reminder。

原因：

- plus degraded 是 PLUS 正常链路的降级形式。
- PLUS 正常链路跑通后，再看 degraded 会更容易定位是外设异常、measurement 不可用，还是 APP 展示问题。

### P4: release hardening 补齐

目标：

- 固定 Android / backend-api / admin-web / ESP32 的发布回归矩阵。
- 固定 debug / release / wireless ADB / ESP32 串口 capture 兼容性。
- 做最小 soak validation。

不应抢占：

- 只要 PLUS 正常主链仍在重构阶段，release hardening 不应反过来抢占安全动作 owner 拆分。

### P4: 旧文档清理

目标：

- 清理或标注旧文档中与当前合同不一致的描述。

重点风险：

- `docs/safety_design.md` 里仍可能有旧描述，例如把 user off / sensor fault 写成旧的 `FAULT_STOP` 语义。
- 当前正式语义应以 `docs/system/firmware_safety_behavior.md`、`docs/start-readiness-contract.md` 和 BLE freeze 文档为准。

## 3. 经验教训

### 3.1 连接成功不能只看 `CONNECTED`

这轮真实问题不是“APP 点了连接就算成功”，而是 BLE GATT 可能先进入 `CONNECTED`，但协议层还没完成 snapshot / capability 同步。

长期规则：

- Android 侧必须以 `protocolReady=true` 作为连接完成 gate。
- `CONNECTED / protocolReady=false` 只能算传输层阶段，不能提前取消 reconnect flow。

### 3.2 体感通过不等于证据通过

多次真机测试里，用户体感正常，但 capture 可能缺 mark、缺 ESP32 串口、或某一路日志为空。

长期规则：

- 用户负责真实操作和体感判断。
- AI 必须复核 capture 的 `notes / session_meta / warnings / Android 日志 / ESP32 串口`。
- 如果某一路日志没采到，结论只能写“体感通过，证据缺口存在”，不能写成完整通过。

### 3.3 真机步骤必须写清时机

用户之前被“先操作、再 mark、再 stop”的含糊步骤浪费了时间。

长期规则：

- 每次真机测试都要明确是否需要重装 APP、重烧 ESP32、重启手机/设备、清数据、重新登录。
- 每条命令都要说明是开始记录、标记关键时刻还是结束记录。
- 要写清“看到什么现象后回电脑输入哪条命令”。

### 3.4 工具链先复用成熟方案

串口、ADB、PlatformIO monitor、capture 这类工具链不应先自写底层能力。

长期规则：

- 优先使用 PlatformIO、ADB 官方链路和项目已有 `device_test_capture.sh`。
- 如果确实自写，必须先说明替代哪一层、为什么既有方案不够、失败时如何回退。

### 3.5 PLUS 正常主链优先于 base / degraded

base 和 degraded 都是 PLUS 正常形态的简化或降级形态。

长期规则：

- 先让 PLUS 正常版连接、开始、停止、保护、measurement 基础稳定。
- 再复核 base 和 degraded，这样问题归因更清楚。

### 3.6 `关闭律动保护` 不是关闭所有安全逻辑

当前产品语义已经固定：

- 关闭律动保护只抑制 `FALL_SUSPECTED` 自动停波动作。
- `USER_LEFT_PLATFORM` 离开平台自动停波仍保留。
- 检测、告警、日志仍可保留。

这个语义必须同时约束固件、SW APP、Demo APP 和文档文案。

### 3.7 candidate owner 不能冒充 action owner

`LaserModule` 可以产生 presence / baseline / rhythm danger 候选和 evidence，但最终 stop/safety/fault 对外语义必须由 `SystemStateMachine` 统一收口。

长期规则：

- 抽纯函数时只迁移判定，不迁移动作时机。
- 任何涉及 `EVT:STOP / EVT:SAFETY / EVT:FAULT` 的改动，必须先审 owner 边界。

### 3.8 BLE 外部合同冻结是安全重构前提

这轮重构能连续推进，是因为内部抽取没有改变：

- `CAP?`
- `SNAPSHOT?`
- `WAVE:*`
- `EVT:*`
- `ACK:*`
- `NACK:*`

长期规则：

- 内部 owner 拆分可以做，但不得顺手改 BLE 线格式。
- 如确实要改协议，必须另起跨仓合同变更包。

### 3.9 `reports/` 和 `docs/` 不能混用

过程报告适合留在 `reports/`，但长期恢复工作不能依赖日期型报告。

长期规则：

- `reports/` 记录过程、审计、阶段交付。
- `docs/` 记录长期有效规则、合同、冻结边界和回归清单。
- 继续工作时先读 `SW/docs/system/ESP32与APP联调优化优先级总表.md`，再读本文。

### 3.10 长周期任务要按整包推进

这轮协作的主要摩擦来自步骤碎、进度不清、建议缺失。

长期规则：

- 长周期优化重构默认采用整包推进。
- 每包收口时必须给出当前进度、剩余事项、下一步建议。
- 不要把每个小切口都拆成一次确认往返。

## 4. 后续恢复入口

如果后续换窗口继续，默认读取顺序：

1. `SW/docs/system/ESP32与APP联调优化优先级总表.md`
2. `SW_ESP3_Firmware/docs/system/esp32_plus_normal_delivery_freeze.md`
3. `SW_ESP3_Firmware/docs/system/esp32_firmware_remaining_work_and_lessons.md`
4. `SW_ESP3_Firmware/docs/system/firmware_safety_behavior.md`
5. `SW_ESP3_Firmware/docs/start-readiness-contract.md`

默认下一步：

- `SafetyActionContractEvaluator` 最小真机 smoke 已通过，可按固件实现、固件文档、SW 总表同步分类提交。
- 下一项再进入 `StopReason / RunSummary / EVT:STOP` 一致性审计。
- 仍不迁移 action timing、不改 BLE 线格式、不改 Android `SessionCoordinator`。

## 5. 当前进度判断

按“可先交付”口径：

- ESP32-plus 正常主链：可先交付。
- A 级固件内部重构：约 `75% - 80%`。
- 全 ESP32 变体发布准备：约 `65%`。

差距主要不在 PLUS 正常主链，而在：

- SafetyAction / StopReason action owner 已完成第一刀纯函数抽取和最小真机 smoke，当前可收口提交。
- base / degraded 还没做新一轮整机复核。
- motion safety shadow 还没决定是否接 runtime。
- release hardening 矩阵和 soak 还没最终补齐。
