# Motion Safety Shadow / Offline Replay 实现切口审计

最后更新时间：2026-04-27

## 1. 结论

下一步不应直接改 runtime detector，也不应把新 detector 接到 `SystemStateMachine` 动作链。

推荐切口分两刀：

1. `offline replay`：先用 Python 离线跑现有 CSV / JSON 样本，输出 detector 候选、reason、feature 和验证矩阵结果。
2. `shadow runtime`：offline replay 通过后，再考虑在固件里接只读 shadow detector，低频输出诊断日志，但不触发停波、不发新的 BLE formal reason。

第一刀只允许新增工具和报告，不碰运行时。

## 2. 现有资产

可复用资产：

- `tools/session_analyzer_v1.py`
  - 已能读取 `data/session_exports` 的 CSV / JSON。
  - 已能归一化 `baselineReady`、`stableWeightKg`、`liveWeightKg`、`distanceMm`、`ratio`、`main_state`、`stop_reason` 等字段。
  - 已能输出 `session_summary.csv`、`group_statistics.csv`、`abnormal_sessions.csv`。
- `tools/detect_v0.py`
  - 已实现 v0.3 离线研究规则。
  - 可读取 `data/detect_esports` 的导出 CSV。
  - 当前输出 `NORMAL / FALL_DANGER`，但尚未分离 `USER_LEFT_PLATFORM`。
- `data/session_exports`
  - 覆盖正常律动、站立、下蹲站起、四周摇摆、手部摆动、调整站姿、律动离开、摔倒异常等。
- `data/detect_esports`
  - 覆盖 2026-03-23 的正常、离台、平台上 / 平台外摔倒样本。

不建议复用为第一刀入口：

- `tools/test_runner`
  - 这是 BLE 真机测试框架，适合后续集成 / 真机验证，不适合做离线 detector replay 第一刀。
- `SystemStateMachine`
  - 它是动作 owner，不应在 detector 未通过 replay 前被接入新逻辑。

## 3. 第一刀：Offline Replay

### 3.1 推荐新增文件

建议新增：

- `tools/motion_safety_shadow_replay.py`

职责：

- 读取 `data/session_exports` 与 `data/detect_esports`。
- 归一化为 `MotionSafetyReplaySample`。
- 按 `motion_safety_detector_contract.md` 计算 feature window。
- 输出 leave / fall shadow detector 的内部候选。
- 生成验证矩阵报告。

### 3.2 输入归一化

统一 sample 字段：

| 字段 | session_exports 来源 | detect_esports 来源 | 备注 |
| --- | --- | --- | --- |
| `timestamp_ms` | `timestamp_ms` / sample index fallback | `timestampMs` / `elapsedMs` | 缺失时用 sample index 推导 |
| `distance` | `distance` / `distanceMm` | `distanceMm` | direct truth |
| `weightKg` | `weight` / `liveWeightKg` | `liveWeightKg` | derived truth |
| `sampleValid` | `measurementValid` fallback true | `measurementValid` | 缺失时 true |
| `baselineReady` | `baseline_ready` / `baselineReady` | `stableWeightKg` 是否存在或前 N 帧推导 | replay 可推导，不改 runtime |
| `stableWeightKg` | meta `stable_weight` / sample `stableWeightKg` | 前 N 帧均值或列值 | replay 用 |
| `runtimeStateCode` | sample field | `runtimeStateCode` | 缺失时从文件类型推断为 RUNNING |
| `waveStateCode` | sample field | `waveStateCode` | 只做证据 |
| `label` | 文件名一级 / 二级标签 | 文件名一级 / 二级标签 | 用于矩阵评估 |

### 3.3 输出文件

建议输出目录：

- `analysis_output/motion_safety_shadow_replay/`

输出文件：

- `shadow_replay_summary.csv`
- `shadow_replay_matrix.csv`
- `shadow_replay_mismatches.csv`
- `shadow_replay_detail.csv`

输出字段至少包括：

- `file_name`
- `scenario_label`
- `expected_bucket`
- `leave_candidate`
- `leave_confirmed`
- `fall_candidate`
- `fall_confirmed`
- `fall_detail`
- `mapped_reason`
- `mapped_effect_if_enabled`
- `mapped_effect_if_disabled`
- `first_candidate_ms`
- `confirmed_at_ms`
- `evidence_weight_drop_kg`
- `evidence_distance_migration`
- `evidence_low_weight_run_ms`
- `evidence_unrecovered_ms`
- `matrix_result`

## 4. Shadow Detector 不得做的事

第一刀 offline replay 不得：

- 修改 `src/` runtime。
- 调用 `SystemStateMachine::onUserOff()`。
- 调用 `SystemStateMachine::applyFallSuspectedAction()`。
- 发 `EVT:SAFETY`。
- 发 `EVT:STOP`。
- 修改 `DEBUG:FALL_STOP` 行为。
- 修改 BLE 线格式。

第二刀 shadow runtime 也不得：

- 改变 wave output。
- 改变 `TopState`。
- 改变 `snapshot_reason_code`。
- 改变 SW APP / Demo APP 已消费的 formal reason。

## 5. Shadow Runtime 切口

只有 offline replay 通过后，才考虑新增固件 shadow runtime。

推荐新增内部模块：

- `src/modules/laser/MotionSafetyInput.h`
- `src/modules/laser/MotionSafetyFeatureWindow.h`
- `src/modules/laser/LeavePlatformDetector.h/.cpp`
- `src/modules/laser/FallDangerDetector.h/.cpp`
- `src/modules/laser/MotionSafetyShadowEvaluator.h/.cpp`

接入点：

- `LaserModule::taskLoop()` 生成 `RhythmStateJudgeInput` 附近。

输出方式：

- 串口低频诊断，例如：
  - `[MOTION_SHADOW] leave_candidate=... fall_candidate=... detail=...`
  - `[MOTION_SHADOW_MATRIX] ...`

禁止输出方式：

- 不新增 `EVT:SAFETY` 字段。
- 不新增 `EVT:*` 线格式。
- 不触发 stop / pause / fault。

默认开关：

- shadow runtime 默认关闭或仅 debug build 打开。
- 如果打开，必须有节流日志，不能刷屏影响 BLE / runtime。

## 6. Replay 验收标准

第一刀 offline replay 通过标准：

- 正常 / hard negative 样本不输出 `FALL_DANGER_CONFIRMED`。
- 离台样本优先输出 `LEFT_PLATFORM_CONFIRMED`，不应被主分类写成 fall/danger。
- 平台上危险倒伏 A 型可输出 `FALL_DANGER_CONFIRMED / DANGER_DEEP_LOW_LOAD`。
- 外部支撑分担 B 型可输出 `FALL_DANGER_CONFIRMED / DANGER_MIGRATION_UNRECOVERED`。
- `sampleValid=false` 或测量不可用不输出 fall/danger。
- base / 无激光形态不进入 detector formal 候选。
- 结果报告必须列出 mismatches，而不是只报汇总通过率。

不得把“有部分样本通过”写成整体通过。

## 7. 第一刀实现结果

已实现：

- `tools/motion_safety_shadow_replay.py`

执行命令：

```bash
python3 tools/motion_safety_shadow_replay.py
python3 -m py_compile tools/motion_safety_shadow_replay.py
```

默认行为：

- 读取 `data/session_exports` 与 `data/detect_esports`。
- 对 `session_exports` 中同一 stem 的 CSV / JSON 双份导出去重，避免重复计入矩阵。
- 如需排查 CSV / JSON 解析一致性，可追加 `--include-duplicates`。

第一版去重后 replay 结果：

- sessions：52
- pass：41
- mismatch：11
- error：0

已输出：

- `analysis_output/motion_safety_shadow_replay/shadow_replay_summary.csv`
- `analysis_output/motion_safety_shadow_replay/shadow_replay_matrix.csv`
- `analysis_output/motion_safety_shadow_replay/shadow_replay_mismatches.csv`
- `analysis_output/motion_safety_shadow_replay/shadow_replay_detail.csv`
- `analysis_output/motion_safety_shadow_replay/formal_validation_summary.csv`
- `analysis_output/motion_safety_shadow_replay/formal_validation_matrix.csv`
- `analysis_output/motion_safety_shadow_replay/formal_validation_mismatches.csv`
- `analysis_output/motion_safety_shadow_replay/formal_validation_excluded.csv`

当前 mismatch 结论：

- `MS-03 下蹲站起` 有正常动作误报 `FALL_SUSPECTED`。
- `MS-07 律动离开` 有 `USER_LEFT_PLATFORM` 欠检。
- `MS-09 平台上摔倒` 有 leave / fall 仲裁问题。
- `MS-10 抓扶手 / 屁股在平台` 有 B 型危险姿态欠检。

## 8. mismatch review 修正结果

已完成第一轮 offline detector 修正：

- `DANGER_FAST_DROP_CONFIRMED` 必须结合恢复窗口或其他危险证据；正常下蹲站起恢复到安全带后不再误报。
- 新增内部 detail：`DANGER_MIGRATION_PARTIAL_SUPPORT`，用于 B 型外部支撑 / 部分低载荷危险姿态。
- 输出 `review_bucket`，把剩余 mismatch 分成数据/标签缺口、detector recall 缺口、仲裁证据缺口。

修正后 replay 结果：

- sessions：52
- pass：47
- mismatch：5
- error：0

矩阵变化：

- `MS-03 下蹲站起`：6/6 pass，正常动作误报清零。
- `MS-10 抓扶手 / 屁股在平台`：从 3/7 pass 提升到 6/7 pass。
- `MS-09 平台上摔倒` 与 `MS-11 平台外摔倒`：通过 offline-only `presenceEvidence` 可完成仲裁。
- 剩余 5 条不继续靠阈值硬调：
  - 3 条 `MS-07`：旧 `session_exports` 导出截断，缺少 formal runtime / stop / event 证据。
  - 2 条 `MS-09 / MS-10`：fall/danger 信号弱，接近正常恢复。

### 8.1 presenceEvidence 约束

当前 `presenceEvidence` 只允许用于 offline replay 审查：

- `PRESENT_LABEL_HINT`：来自文件名标签，例如平台上摔倒、腿在平台、屁股在平台、抓住扶手。
- `ABSENT_LABEL_HINT`：来自文件名标签，例如平台外摔倒、离开平台、律动离开。
- `presence_evidence_source=LABEL_ONLY`：表示该证据不能直接迁移到 runtime。

runtime 若要使用类似能力，必须先有真实输入，例如：

- `userPresent`
- `stableVisible`
- 接触区 / 承重区分布
- 离台 stop reason
- 更明确的 presence owner 输出

禁止把 `LABEL_ONLY` 逻辑照搬进固件运行时。

## 9. MS-07 规范补测复核

2026-04-27 使用 Demo APP 补测 `MS-07 律动离开`，新增 3 条 `session_exports`：

| 文件 | Matrix | Replay 结果 | 证据来源 |
| --- | --- | --- | --- |
| `律动离开_律动离开_20260427_101306_10Hz120.csv/json` | MS-07 | PASS | `meta.stop_reason=USER_LEFT_PLATFORM` |
| `律动离开_律动离开_20260427_101401_20Hz120.csv/json` | MS-07 | PASS | sensor leave + `meta.stop_reason=USER_LEFT_PLATFORM` |
| `律动离开_律动离开_20260427_101440_6Hz120.csv/json` | MS-07 | PASS | `meta.stop_reason=USER_LEFT_PLATFORM` |

补测后 replay 结果：

- sessions：55
- pass：50
- mismatch：5
- error：0

矩阵变化：

- `MS-07` 从 6 条增加到 9 条，其中新增 3 条全部通过。
- 旧 3 条 `MS-07` 仍标为 `leave_export_truncated_before_formal_evidence`，应作为历史不可用样本处理，不再作为 detector 欠检强证据。
- 1 条旧 `MS-09` 样本的文件标签为 `腿在平台`，但导出正式 `stop_reason=USER_LEFT_PLATFORM`，已标为 `data_or_label_gap_conflicting_stop_reason`，不应误判为 runtime 仲裁失败。

配套 capture：

- `/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260427_100829__vivo_V2405A__esp32_plus_normal__ESP32-plus-normal__manual__ESP32_RUNTIME__motion_safety_ms07_retest`

注意：

- 本轮使用 Demo APP 导出数据，`runtime_events_session.jsonl` 为空、SW focus log 缺核心事件不是本轮 blocker。
- Android full log 和 ESP32 串口均能看到正式 `USER_LEFT_PLATFORM` 停波证据。

## 10. 剩余 fall/danger 数据质量结论

2026-04-27 已复核剩余 2 条非 MS-07 mismatch：

| 文件 | Matrix | 数据质量结论 | 处理 |
| --- | --- | --- | --- |
| `摔倒异常_屁股在平台_20260325_201134_10Hz120.csv` | MS-10 | `weak_fall_label_without_formal_evidence` | 不作为正式 fall/danger positive 验收样本 |
| `摔倒异常_腿在平台_20260325_201335_6Hz120.csv` | MS-09 | `conflicting_label_and_stop_reason` | 以正式 `stop_reason=USER_LEFT_PLATFORM` 为准，不作为 `FALL_SUSPECTED` positive 样本 |

审计依据：

- `屁股在平台_201134_10Hz120` 全程 `BASELINE_PENDING`，无 `event_aux` / `risk_advisory`，`stop_reason=IDLE`，尾部恢复接近 baseline。
- `腿在平台_201335_6Hz120` 文件标签为 fall/danger，但导出正式 `stop_reason=USER_LEFT_PLATFORM`，属于标签与正式 stop reason 冲突。

这些样本继续保留在原始 replay mismatch 中，用于暴露数据质量问题；但发布级 detector 验收不应把它们计为 detector 失败。

## 11. Formal Validation 输出

`formal_validation` 是正式验收视图，不替代原始 replay：

- 原始 `shadow_replay_*` 保留所有样本，用于发现 detector 和数据质量问题。
- `formal_validation_*` 排除已定性为数据质量问题的样本，用于观察 release-grade detector 验收结果。

当前排除规则：

- `leave_export_truncated_before_formal_evidence`
- `leave_export_missing_formal_evidence`
- `weak_fall_label_without_formal_evidence`
- `conflicting_label_and_stop_reason`
- `no_valid_samples`

2026-04-27 输出结果：

- 原始 replay：55 sessions，50 pass，5 mismatch，0 error。
- formal validation：49 sessions，49 pass，0 mismatch，0 error。
- formal excluded：6 条。

formal excluded 组成：

- 3 条旧 `MS-07`：导出截断，缺少正式离台证据。
- 2 条 `MS-09`：fall 标签样本缺少正式事件证据，其中 1 条虽然 replay PASS，但仍不作为发布级 positive 验收样本。
- 1 条 `MS-10`：弱 fall 标签样本，缺少正式事件证据且恢复接近 baseline。

## 12. fall/danger 正式验证样本补采

2026-04-27 使用 Demo APP 补采 3 条 `fall/danger positive`：

| 文件 | Matrix | Replay 结果 | 正式证据 |
| --- | --- | --- | --- |
| `摔倒异常_腿在平台_20260427_105643_20Hz120.csv/json` | MS-09 | PASS | `FALL_SUSPECTED / ABNORMAL_STOP` |
| `摔倒异常_屁股在平台_20260427_105826_10Hz120.csv/json` | MS-10 | PASS | `FALL_SUSPECTED / ABNORMAL_STOP` |
| `摔倒异常_其他_20260427_105947_6Hz120_抓住扶手.csv/json` | MS-10 | PASS | `FALL_SUSPECTED / ABNORMAL_STOP` |

补采后 replay 结果：

- 原始 replay：58 sessions，53 pass，5 mismatch，0 error。
- formal validation：52 sessions，52 pass，0 mismatch，0 error。
- formal excluded：6 条，仍为历史数据质量样本。

formal validation 矩阵变化：

- `MS-09`：13/13 pass。
- `MS-10`：8/8 pass。
- 新增 3 条 fall/danger 样本均为 `usable_for_shadow_replay`，全部纳入 formal validation。

关键动作边界：

- `屁股在平台` 样本中，普通蹲下再坐在平台上不会触发停波。
- 后续上半身趴在椅子上，模拟摔倒后仅屁股仍在平台、头和脚在平台外，才触发 `FALL_SUSPECTED`。
- 该边界支持当前设计：普通蹲坐不应误停，持续危险姿态才进入 `ABNORMAL_STOP`。

配套 capture：

- `/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260427_105555__vivo_V2405A__esp32_plus_normal__ESP32-plus-normal__manual__ESP32_RUNTIME__motion_safety_fall_positive_retest`

## 13. C++ shadow runtime 只读实现

2026-04-27 已完成第一包实现：

- 新增 `src/modules/laser/MotionSafetyShadowEvaluator.h`
- 新增 `src/modules/laser/MotionSafetyShadowEvaluator.cpp`
- `LaserModule::taskLoop()` 在 `RhythmStateJudge` 更新后调用 shadow evaluator。
- `GlobalConfig.h` 增加 shadow 开关和日志节流参数。

只读保证：

- 只输出串口 `[MOTION_SHADOW]`。
- 不调用 `SystemStateMachine`。
- 不触发 `onUserOff()`。
- 不触发 `applyFallSuspectedAction()`。
- 不发布 BLE 事件。
- 不改变 `EVT:* / SNAPSHOT / DEBUG:FALL_STOP` 线格式。

日志示例：

```text
[MOTION_SHADOW] action=observe_only top_state=RUNNING user_present=1 baseline_ready=1 mapped_reason=FALL_SUSPECTED effect_if_enabled=ABNORMAL_STOP effect_if_disabled=WARNING_ONLY detail=DANGER_MIGRATION_UNRECOVERED ... note=no_runtime_action
```

构建验证：

```bash
python3 -m platformio run -e esp32s3
```

结果：通过。

注意：本机 `pio` 不在 shell PATH 中，已使用 `python3 -m platformio`。

## 14. 下一步实现建议

下一包仍只改 offline detector，不接 `src/` runtime：

- `session_exports` 链路的规范 `MS-07 律动离开` 已有 3 条可用补测证据，不需要继续补测同类样本。
- 下一步进入 C++ shadow runtime 真机观察。
- 真机观察仍只验证 `[MOTION_SHADOW]` 与正式 `FALL_SUSPECTED / USER_LEFT_PLATFORM` 是否对齐，不接 `SystemStateMachine` action，不改 BLE 线格式。

只有离线 replay 输出可解释，并且 mismatches 可接受后，才进入 C++ shadow runtime。

## 15. Shadow runtime 首轮真机观察

2026-04-27 首轮真机观察结论：

- 只读接入通过：`[MOTION_SHADOW]` 只输出串口日志，正式 BLE `EVT:*` 行为未改变。
- 正常律动手动停止、离开平台自动停波、摔倒保护自动停波均有正式日志证据。
- 离开平台场景暴露出 runtime 仲裁缺口：`userPresent=1` 在离台确认前有滞后，shadow 曾提前输出 `mapped_reason=FALL_SUSPECTED`，随后正式 reason 为 `USER_LEFT_PLATFORM`。

修正策略：

- runtime shadow 不再把 `userPresent=1` 直接当作强 presence evidence。
- 对“fall candidate 与离台相似强特征重叠，但 leave 尚未正式确认”的窗口，输出 review-only：

```text
[MOTION_SHADOW] action=observe_only mapped_reason=NONE detail=REVIEW_LEAVE_FALL_AMBIGUOUS ... note=no_runtime_action
```

该 detail 只用于串口诊断，不是 BLE formal reason。

修正后构建：

```bash
python3 -m platformio run -e esp32s3
```

结果：通过。

下一步复测：

- 重新烧录修正后的固件。
- 复测离开平台自动停波。
- 复测摔倒保护自动停波。
- 观察 `[MOTION_SHADOW]` 是否不再在离台场景抢先输出 `FALL_SUSPECTED`，同时在摔倒场景仍输出 `FALL_SUSPECTED`。

2026-04-27 二次复测结论：

- 正式 BLE 行为仍正确：离台正式 reason 为 `USER_LEFT_PLATFORM`，摔倒正式 reason 为 `FALL_SUSPECTED`。
- 设备未 reset / panic，BLE 未断连。
- 观察到一次 PLUS measurement 瞬断：Android 收到 `MEASUREMENT_UNAVAILABLE / WARNING_ONLY`，约 3 秒后恢复为 `NONE`；ESP32 串口对应一次 `Modbus read fail (0xE2)`，`last_read_ms=2010`，随后恢复到正常读数。
- shadow 仍未完全通过：离台场景中，在正式 `USER_LEFT_PLATFORM` 前仍出现一次 `mapped_reason=FALL_SUSPECTED detail=DANGER_FAST_DROP_CONFIRMED`。

二次修正策略：

- 快速掉重单独成立时，不再映射为 `FALL_SUSPECTED`。
- 输出 review-only：

```text
[MOTION_SHADOW] action=observe_only mapped_reason=NONE detail=REVIEW_FAST_DROP_ONLY ... note=no_runtime_action
```

- 只有深低载荷、未恢复迁移、部分支撑迁移等更强证据才允许 shadow 输出 `FALL_SUSPECTED`。
- 该修正仍只影响串口 shadow 日志，不接 runtime action，不改变 BLE 线格式。
- 修正后执行 `python3 -m platformio run -e esp32s3`，结果通过。

2026-04-27 带串口复测通过：

- capture：`20260427_114427...motion_safety_shadow_fastdrop_retest_with_serial`
- 离台正式 BLE 仍为 `USER_LEFT_PLATFORM / RECOVERABLE_PAUSE`。
- 离台 shadow 输出 `mapped_reason=NONE detail=REVIEW_LEAVE_FALL_AMBIGUOUS`，未再抢先输出 `FALL_SUSPECTED`。
- 摔倒正式 BLE 仍为 `FALL_SUSPECTED / ABNORMAL_STOP`。
- fast-drop-only shadow 输出 `mapped_reason=NONE detail=REVIEW_FAST_DROP_ONLY`。
- 未发现 reset / panic / BLE 断连；measurement diagnostic 未复现 Modbus `0xE2` 瞬断。

结论：observe-only shadow runtime 第一阶段通过；后续若要接 runtime action，必须另起包做 action gate 合同化审计。
