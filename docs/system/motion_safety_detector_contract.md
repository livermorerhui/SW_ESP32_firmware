# Motion Safety Detector Contract

最后更新时间：2026-04-26

## 1. 目标

本文定义下一阶段 motion safety detector 的设计合同。

本合同只定义：

- detector 输入
- leave detector 确认窗口
- fall / danger detector 确认窗口
- 输出 reason / effect 映射
- 验证矩阵

本合同不改变当前 runtime 行为，不改变 BLE 线格式，不改变 SW APP / Demo APP 已消费的外部合同。

## 2. 不变外部合同

后续实现必须继续保持：

- `USER_LEFT_PLATFORM`：离开平台，运行条件失效，默认 `RECOVERABLE_PAUSE`。
- `FALL_SUSPECTED`：疑似危险运动 / 摔倒保护候选，默认 `ABNORMAL_STOP`。
- `DEBUG:FALL_STOP enabled=0`：只抑制 `FALL_SUSPECTED` 自动停波动作，输出 `WARNING_ONLY`；不抑制检测和日志。
- `DEBUG:FALL_STOP enabled=0` 不影响 `USER_LEFT_PLATFORM`。
- `EVT:SAFETY`、`EVT:STOP`、`EVT:FAULT`、`SNAPSHOT` 的字段名和消费含义不变。

## 3. 分层

目标结构分四层：

1. `MotionSafetyInput`：采样输入与运行上下文。
2. `MotionSafetyFeatureWindow`：短窗口特征。
3. `LeavePlatformDetector` / `FallDangerDetector`：只输出内部候选。
4. `MotionSafetyActionPolicy`：把内部候选映射到现有 `USER_LEFT_PLATFORM` / `FALL_SUSPECTED` 合同。

禁止 detector 直接发 BLE 事件或直接停波。

最终动作仍由 `SystemStateMachine` owner。

## 4. 通用输入合同

每次 detector update 应接收同一份输入快照：

| 字段 | 类型 | 来源 | 用途 |
| --- | --- | --- | --- |
| `nowMs` | `uint32_t` | firmware clock | 窗口计时 |
| `topState` | `TopState` | `SystemStateMachine` | 只在 `RUNNING` 中触发动作候选 |
| `sampleValid` | `bool` | measurement owner | 避免无效样本触发保护 |
| `distance` | `float` | laser direct truth | 直接位移 / 距离侧证据 |
| `weightKg` | `float` | calibration model derived truth | 承重侧证据 |
| `userPresent` | `bool` | stable/presence contract | leave / fall 条件门 |
| `baselineReady` | `bool` | stable contract | 是否可做相对偏移判定 |
| `stableWeightKg` | `float` | stable contract | 相对偏移基线 |
| `presenceEvidence` | enum / struct | presence owner / offline replay | 区分仍在平台上的危险姿态与离台 |
| `presenceEvidenceSource` | enum | evidence owner | 标记证据来源，防止 label/debug 证据进入正式 runtime |
| `startReady` | `bool` | formal start gate | leave detector 正式 gate |
| `waveOutputActive` | `bool` | wave owner | 验证和日志，不作为 detector 唯一真相 |

输入边界：

- direct truth 是 `distance`。
- `weightKg` 是模型推导值，不能单独承担所有安全语义。
- `baselineReady=false` 时，fall / danger detector 不得输出正式停波候选。
- `sampleValid=false` 时，detector 必须清理候选或进入 invalid 观察状态，不得升级正式 reason。
- `presenceEvidenceSource=LABEL_ONLY` 只能用于离线 replay 审查，禁止进入 runtime action policy。

## 5. 短窗口特征合同

统一 feature window 应至少提供：

| 特征 | 用途 |
| --- | --- |
| `windowMs` | 当前窗口长度 |
| `sampleCount` | 样本数 |
| `minWeightKg` / `maxWeightKg` | 承重范围 |
| `minDistance` / `maxDistance` | 位移范围 |
| `weightDropKg` | 相对窗口起点或 baseline 的承重下降 |
| `distanceMigration` | 距离侧迁移 |
| `weightRateKgPerSec` | 承重变化率 |
| `distanceRatePerSec` | 距离变化率 |
| `baselineDeviationRatio` | `abs(windowMeanWeight - stableWeightKg) / stableWeightKg` |
| `lowWeightRunMs` | 低承重持续时长 |
| `unrecoveredMs` | 离开安全带后未恢复时长 |
| `recoveredToSafeBand` | 是否回到安全带 |

实现要求：

- feature window 只做计算，不决定最终 reason。
- 窗口参数必须集中配置，不允许散落魔法数字。
- 日志应能输出核心 feature，但默认不刷高频日志。

## 6. LeavePlatformDetector 合同

### 6.1 语义

leave detector 判断的是：

> 用户离开平台，设备运行条件失效。

它不是 fall detector 的子集，也不是 fall detector 的 fallback。

### 6.2 输入门

允许进入 leave candidate 的条件：

- `topState == RUNNING`
- `startReady == true`
- `baselineReady == true`
- `sampleValid == true`

如果这些条件不满足，只允许输出 debug 状态，不得输出 `USER_LEFT_PLATFORM`。

### 6.3 Candidate 条件

候选应至少满足其中一类证据：

- 承重侧：`weightKg` 进入离台低承重区，并持续超过短确认窗口。
- 距离侧：`distance` 出现与离台一致的持续迁移。
- 组合侧：承重下降与距离迁移方向一致，并且未在恢复窗口内回到安全带。

### 6.4 确认窗口

建议窗口语义：

- `candidateWindowMs`：短窗口观察，不短于多个采样周期。
- `confirmWindowMs`：确认离台，必须跨多个有效样本。
- `recoveryWindowMs`：如果在确认前恢复到安全带，取消候选。
- `clearWindowMs`：用户重新站稳后清除 `RECOVERABLE_PAUSE` 的观察窗口。

具体毫秒值不在本合同硬编码，必须由已有样本和下一轮验证矩阵推导。

### 6.5 输出

确认后输出内部状态：

- `LEFT_PLATFORM_CONFIRMED`

Action policy 映射为：

- reason：`USER_LEFT_PLATFORM`
- effect：`RECOVERABLE_PAUSE`
- stop source：`FORMAL_SAFETY_OTHER`

## 7. FallDangerDetector 合同

### 7.1 语义

fall / danger detector 判断的是：

> 用户未完全离台，但进入危险倒伏 / 危险塌陷 / 危险失姿，持续振动可能造成二次伤害。

它不负责 generic fall detection，也不负责医学意义的跌倒确认。

### 7.2 输入门

允许进入 fall / danger candidate 的条件：

- `topState == RUNNING`
- `userPresent == true`
- `baselineReady == true`
- `sampleValid == true`

如果 `userPresent == false`，优先交给 leave detector。

### 7.3 Candidate 条件

候选应至少区分三类证据：

| 内部候选 | 说明 | 典型证据 |
| --- | --- | --- |
| `DANGER_DEEP_LOW_LOAD` | 深低承重危险 | 低承重持续、恢复不足 |
| `DANGER_MIGRATION_UNRECOVERED` | 明显迁移后未恢复 | 距离 / 承重迁移明显，尾段不一定极低 |
| `DANGER_MIGRATION_PARTIAL_SUPPORT` | 外部支撑 / 部分低载荷危险 | 明显迁移、部分低载荷、未完全恢复 |
| `REVIEW_FAST_DROP_ONLY` | 仅快速掉重，缺少强摔倒证据 | 多样本变化率，但没有深低载荷 / 未恢复迁移 / 部分支撑迁移 |

禁止单帧 spike 直接升级为正式停波候选。

### 7.4 确认窗口

建议窗口语义：

- `candidateWindowMs`：记录异常开始。
- `confirmSamples`：至少多个连续或近连续有效样本。
- `holdWindowMs`：危险证据持续超过该窗口才确认。
- `recoveryWindowMs`：若恢复到安全带，取消候选。
- `cooldownWindowMs`：确认后避免重复触发。

具体参数必须通过真实样本推导，不能凭单次体感直接写死。

### 7.5 输出

确认后输出内部状态：

- `FALL_DANGER_CONFIRMED`

同时保留内部 detail：

- `DANGER_DEEP_LOW_LOAD`
- `DANGER_MIGRATION_UNRECOVERED`
- `DANGER_MIGRATION_PARTIAL_SUPPORT`

快速掉重只能作为 review-only 线索，不能单独输出 `FALL_SUSPECTED`：

- `REVIEW_FAST_DROP_ONLY`

Action policy 映射为：

- reason：`FALL_SUSPECTED`
- effect：
  - `fall_stop_enabled=true` -> `ABNORMAL_STOP`
  - `fall_stop_enabled=false` -> `WARNING_ONLY`
- stop source：优先使用结构化来源，例如 `BASELINE_MAIN_LOGIC`

内部 detail 暂不新增 BLE formal reason。

## 8. 仲裁规则

同一窗口内 leave 与 fall/danger 同时出现时：

1. 如果有正式 `presenceEvidence` 证明用户仍在平台上，且 fall/danger 已确认，输出 `FALL_SUSPECTED`。
2. 如果有正式 `presenceEvidence` 证明用户已经离台，或 leave 已确认且无在台证据，输出 `USER_LEFT_PLATFORM`。
3. 如果 userPresent 已经 false，但 leave 尚未确认，不升级 fall/danger。
4. 如果 userPresent 仍 true，且 fall/danger 已确认，输出 `FALL_SUSPECTED`。
5. 如果只有 warning / advisory，不触发停波。

原因：

- 离台代表运行条件失效，应优先作为可恢复暂停处理。
- fall/danger 代表未离台危险状态，应独立补足 leave 无法覆盖的风险。
- 平台上危险姿态与平台外离台在深低承重特征上可能非常接近；没有 presence 证据时，不应通过放宽阈值伪装成已可靠区分。

## 9. 验证矩阵

| ID | 场景 | 期望 detector | 期望 reason / effect | 必须保留证据 |
| --- | --- | --- | --- | --- |
| MS-01 | 静止站立 | 无正式候选 | `NONE` | baseline ready、stable weight |
| MS-02 | 正常律动 | 无正式候选 | `NONE` | window feature 不越界或可恢复 |
| MS-03 | 下蹲站起 | 无正式候选 | `NONE` | hard negative 不误停 |
| MS-04 | 左右 / 四周摇摆 | 无正式候选 | `NONE` | hard negative 不误停 |
| MS-05 | 仅下肢 / 双脚放平台，律动保护开启 | 允许 warning/advisory，不能单帧误停 | 不应因单帧 spike 进入 `ABNORMAL_STOP` | 多样本窗口证明是否恢复 |
| MS-06 | 仅下肢 / 双脚放平台，律动保护关闭 | 若触发 fall candidate，只能 warning | `FALL_SUSPECTED / WARNING_ONLY` | `fall_stop_enabled=0`、无停波 |
| MS-07 | 正常离开平台 | `LEFT_PLATFORM_CONFIRMED` | `USER_LEFT_PLATFORM / RECOVERABLE_PAUSE` | leave 确认窗口、停波事实 |
| MS-08 | 离开后重新站稳 | clear recoverable pause | 恢复 ready，但不自动恢复会话 | clear window、manual continue |
| MS-09 | 平台上危险倒伏 A 型 | `FALL_DANGER_CONFIRMED` | `FALL_SUSPECTED / ABNORMAL_STOP` | deep low-load 证据 |
| MS-10 | 外部支撑分担 B 型 | `FALL_DANGER_CONFIRMED` | `FALL_SUSPECTED / ABNORMAL_STOP` | migration unrecovered 证据 |
| MS-11 | 平台外摔倒 / 完全离台 | 优先 leave | `USER_LEFT_PLATFORM / RECOVERABLE_PAUSE`，除非证据证明未离台危险 | leave 与 fall 仲裁证据 |
| MS-12 | BLE 断连 | 不由 detector 归类 | 保持现有 BLE disconnect policy | 断连事件与 safety detector 分离 |
| MS-13 | 测量不可用 | 不由 fall detector 归类 | `MEASUREMENT_UNAVAILABLE` policy | sample invalid 不误触发 fall |
| MS-14 | base / 无激光形态 | detector 不输出正式候选 | 不影响 base 启停 | platform model / laser installed |

## 10. 真机验证要求

进入实现包后必须真机验证。

每轮真机验证必须采集：

- Android logcat / SW diagnostic log。
- ESP32 serial log。
- BLE raw lines。
- 操作标记：开始、停止、离台、恢复、开关切换。
- 采样导出 CSV / JSON，若本轮涉及 detector 参数推导。

用户负责真实操作和体感判断。

AI 负责复核 capture 产物，确认日志证据是否支持结论。

## 11. 实现前冻结项

实现前不得改变：

- BLE formal wire format。
- `USER_LEFT_PLATFORM` 的自动停波语义。
- `FALL_SUSPECTED` 的开关边界。
- SW APP `WARNING_ONLY` 不推进会话 danger stop 的规则。
- Demo APP 采样导出字段。

实现时允许新增：

- 固件内部 detector 类。
- 固件内部 debug detail。
- 低频结构化串口日志。
- 离线分析脚本。

但新增内部 detail 不等于新增 BLE formal reason。

## 12. C++ shadow runtime 只读合同

2026-04-27 已进入 C++ shadow runtime 第一包。

只读边界：

- `MotionSafetyShadowEvaluator` 只能读取 `LaserModule` 已有测量上下文。
- 输入只包括：`topState`、`userPresent`、`baselineReady`、`distance`、`weightKg`、`baselineDistance`、`baselineWeightKg`。
- 只在 `RUNNING + userPresent + baselineReady + valid sample` 时观察。
- 不调用 `SystemStateMachine`。
- 不调用 `WaveModule`。
- 不发布 `EventBus` 事件。
- 不新增 BLE line format。
- 不改变 `EVT:STOP / EVT:FAULT / EVT:SAFETY / SNAPSHOT / DEBUG:FALL_STOP`。

串口日志格式：

```text
[MOTION_SHADOW] action=observe_only ... mapped_reason=<NONE|USER_LEFT_PLATFORM|FALL_SUSPECTED> ... note=no_runtime_action
```

日志语义：

- `mapped_reason=FALL_SUSPECTED` 只表示 shadow detector 的只读候选。
- `effect_if_enabled=ABNORMAL_STOP` / `effect_if_disabled=WARNING_ONLY` 只表示如果未来接 action policy 时的合同映射。
- `note=no_runtime_action` 是冻结要求；看到该日志不代表固件已经通过 shadow detector 触发停波。

真机验证前不得把 `[MOTION_SHADOW]` 当作正式业务事件消费。

## 13. Runtime presence evidence 限制

2026-04-27 首轮 shadow runtime 真机观察确认：

- `userPresent=1` 在离台确认前可能短暂滞后。
- 因此 runtime shadow 不得把 `userPresent=1` 单独当作“用户仍在平台上”的强 presence evidence。
- 当 fall-like 与 leave-like 特征重叠，且没有更强 presence evidence 时，必须输出 review-only，不得直接升级为 `FALL_SUSPECTED`。

当前 review-only detail：

```text
REVIEW_LEAVE_FALL_AMBIGUOUS
REVIEW_FAST_DROP_ONLY
```

这些 detail 只允许出现在 `[MOTION_SHADOW]` 串口日志中，不得进入 BLE formal reason。
