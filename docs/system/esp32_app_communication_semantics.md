# ESP32 -> SW APP Communication Semantics

## 目的与适用范围

本文是当前 ESP32 firmware 与正式 SW APP 之间的通讯语义说明。

它回答三件事：

1. ESP32 当前真实导出了哪些上行语义。
2. 正式 SW APP 当前实际消费了其中哪些。
3. 后续开发者应该把哪一层当成语义 owner，避免把内部状态、兼容输出、demo console 信号混用。

本文适用于：

- firmware 开发
- SW APP 接入
- 联调排障
- 新维护者交接

不适用于：

- demo console UI 说明
- 单次任务过程记录

单次任务证据与过程请看 `reports/tasks/TASK-202603-ESP32-STOP-FAULT-STATE-SEMANTICS-AUDIT-AND-DOCS/`。

## 1. 语义分层

### 1.1 顶层运行/停波语义

由 `SystemStateMachine` owner：

- `EVT:STATE`
- `EVT:FAULT`
- `EVT:SAFETY`
- `EVT:STOP`

相关代码：

- `src/core/SystemStateMachine.cpp:139-228`
- `src/core/ProtocolCodec.h:203-279`

### 1.2 测量与基线语义

由 `LaserModule` 和 `RhythmStateJudge` owner：

- `EVT:STABLE`
- `EVT:PARAM`
- `EVT:STREAM`
- `EVT:BASELINE`

相关代码：

- `src/modules/laser/LaserModule.cpp:443-489`
- `src/modules/laser/LaserModule.cpp:941-946`
- `src/modules/laser/LaserModule.cpp:1063-1067`
- `src/modules/laser/RhythmStateJudge.h:7-17`

### 1.3 APP 产品会话语义

由 SW APP 的 `ProductController + SessionCoordinator` owner，不等同于固件 `EVT:STATE`：

- `READY`
- `RUNNING`
- `PAUSED_RECOVERABLE`
- `STOPPED_BY_DANGER`

相关代码：

- `/Users/wurh/Desktop/SW/apps/android/src/main/java/com/example/sonicwavev4/feature/sine/domain/VibrationSessionContracts.kt:95-131`

## 2. 当前 ESP32 正式导出语义分类

| 类别 | 事件 | owner | 含义 |
| --- | --- | --- | --- |
| 运行态 | `STATE` | `SystemStateMachine` | 设备顶层状态：`IDLE/ARMED/RUNNING/FAULT_STOP` |
| fault 可见性 | `FAULT` | `SystemStateMachine` | 当前可见 fault code；对 `USER_LEFT_PLATFORM` / `FALL_SUSPECTED` 额外桥接 `reason` 文本 |
| 安全语义 | `SAFETY` | `SystemStateMachine` | `reason + effect + state + wave` |
| 停波上下文 | `STOP` | `SystemStateMachine` | `stop_reason + stop_source + effect + target state` |
| 稳定体重 | `STABLE` | `LaserModule` | 一次稳定体重锁定结果 |
| 参数/ready 扩展 | `PARAM` | `LaserModule` | 零点/系数与兼容 ready 扩展 |
| 实时流 | `STREAM` | `LaserModule` | 距离/体重调试流 |
| baseline-main 证据 | `BASELINE` | `LaserModule + RhythmStateJudge` | baseline_ready、stable_weight_active、MA7、ratio、danger evidence |

## 3. stop / fault / state / baseline 的正式角色

### `STATE`

`STATE` 只回答“设备当前顶层运行态是什么”，不回答“为什么停了”。

- `IDLE`
- `ARMED`
- `RUNNING`
- `FAULT_STOP`

不要把 `STATE` 直接当作产品暂停/异常终止语义 owner。

### `FAULT`

`FAULT` 当前承担兼容可见性角色。

当前仍保留 numeric code 作为兼容前缀，例如：

- `EVT:FAULT 100`
- `EVT:FAULT 101`

为让 current formal SW APP 在不改主消费契约的前提下继续通过 `Event.Fault.reason`
识别关键停波语义，当前阶段额外做了最小桥接：

- `USER_LEFT_PLATFORM` 场景：`EVT:FAULT 100 reason=USER_LEFT_PLATFORM`
- `FALL_SUSPECTED` 场景：`EVT:FAULT 101 reason=FALL_SUSPECTED`

关键边界：

- numeric code 仍保留，Demo / legacy parser 现有 numeric 依赖不要求先迁移
- 只桥接 formal 当前必需识别的这两个 safety reason
- 这不是最终 canonical 协议，`FAULT` 仍然不是长期 stop/pause owner
- 后续真正清理应在 formal 正式切到 `STOP/SAFETY/BASELINE` 后进行

### `SAFETY`

`SAFETY` 是 firmware 当前最完整的安全语义通道，直接表达：

- `reason`
- `code`
- `effect`
- `state`
- `wave`

其中：

- `USER_LEFT_PLATFORM` -> `effect=RECOVERABLE_PAUSE`
- `FALL_SUSPECTED` -> `effect=ABNORMAL_STOP`

### `STOP`

`STOP` 是 firmware 当前最完整的停波上下文通道，表达：

- `stop_reason`
- `stop_source`
- `code`
- `effect`
- `state`

它更适合回答“这次停波最终按什么语义落地”。

### `BASELINE`

`BASELINE` 用于 baseline-main evidence，不是正式产品 stop/pause owner。

它适合：

- 联调
- 危险停波证据回放
- baseline readiness 与 MA7 偏离观测

不适合直接当作产品 stop reason owner。

## 4. 典型场景语义

### 4.1 `USER_LEFT_PLATFORM`

当前固件真实表现：

- owner：`LaserModule` 在位判定
- 默认 effect：`RECOVERABLE_PAUSE`
- 若发生在 `RUNNING`：
  - `EVT:STOP stop_reason=USER_LEFT_PLATFORM stop_source=FORMAL_SAFETY_OTHER effect=RECOVERABLE_PAUSE state=IDLE`
  - `EVT:STATE IDLE`
  - `EVT:FAULT 100`
  - `EVT:SAFETY reason=USER_LEFT_PLATFORM code=100 effect=RECOVERABLE_PAUSE state=IDLE wave=STOPPED`

重要说明：

- `USER_LEFT_PLATFORM` 确实已对外导出
- 命名 reason 仍以 `SAFETY` / `STOP` 为主 owner
- 但当前阶段也会桥接到 `EVT:FAULT 100 reason=USER_LEFT_PLATFORM`

### 4.2 `FALL_SUSPECTED`

当前固件真实表现：

- final action owner 仍是 `SystemStateMachine`
- 默认 effect：`ABNORMAL_STOP`
- danger baseline reason 会记录到 `EVT:STOP stop_reason=... stop_source=BASELINE_MAIN_LOGIC`
- 同时还会发：
  - `EVT:STATE FAULT_STOP`
  - `EVT:FAULT 101`
  - `EVT:SAFETY reason=FALL_SUSPECTED effect=ABNORMAL_STOP ...`

重要说明：

- `FALL_SUSPECTED` 作为产品 reason 在 `SAFETY` 里稳定可见
- `STOP.stop_reason` 可能是更细粒度的内部 danger 触发文本
- 同时当前阶段也会桥接到 `EVT:FAULT 101 reason=FALL_SUSPECTED`

## 5. formal SW APP 当前正式依赖哪些语义

正式 SW APP 当前实际消费的是：

- `STATE`
- `FAULT`
- `STABLE`
- `PARAM`
- `STREAM`

而不是：

- `SAFETY`
- `STOP`
- `BASELINE`

相关代码：

- `/Users/wurh/Desktop/SW/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/Model.kt:48-82`
- `/Users/wurh/Desktop/SW/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/ProtocolCodec.kt:24-40`

### pause recoverable 当前真正依赖

formal SW APP 当前不是直接依赖 firmware `SAFETY.effect=RECOVERABLE_PAUSE`。

它实际依赖的是：

- `Event.Fault.reason` 能否被映射成 `USER_LEFT_PLATFORM`

之后才会通过：

- `Repository`
- `ProductController`
- `SessionSafetyInterventionBridge`
- `SessionCoordinator`

落到 `PAUSED_RECOVERABLE`。

### danger stop 当前真正依赖

formal SW APP 当前不是直接依赖 firmware `STOP_SOURCE` 或 `STATE=FAULT_STOP`。

它实际依赖的是：

- `Event.Fault.reason` 能否被映射成 `FALL_SUSPECTED`

之后才会落到 `STOPPED_BY_DANGER`。

### readiness / continue 当前真正依赖

- `STATE` 的 `ARMED/RUNNING`
- `STABLE`
- `PARAM` extras
- connection state

`continue` 还必须满足：

- `PAUSED_RECOVERABLE`
- device connected
- `baseline_ready=true`

## 6. 当前契约缺口

当前最大契约缺口不是 firmware 没导出语义，而是：

- firmware 把正式 stop/safety 语义放在 `SAFETY` / `STOP`
- formal SW APP 仍把 `FAULT` 当 stop meaning 主入口
- 因而在 formal 正式迁移完成前，需要为关键 reason 保留 `FAULT` 桥接文本

因此：

- 设备可以已经停波
- `USER_LEFT_PLATFORM` 可以已经对外导出
- 若缺少 `FAULT` 桥接文本，formal SW APP 仍可能看不见 `PAUSED_RECOVERABLE` / `STOPPED_BY_DANGER` 所需的命名 reason

## 7. 后续开发者建议阅读顺序

建议先看语义，再看代码：

1. 本文
2. `src/core/SystemStateMachine.cpp`
3. `src/modules/laser/LaserModule.cpp`
4. `src/core/ProtocolCodec.h`
5. `/Users/wurh/Desktop/SW/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/ProtocolCodec.kt`
6. `/Users/wurh/Desktop/SW/apps/android/src/main/java/com/example/sonicwavev4/core/device/sonicwave/SonicWaveRepository.kt`
7. `/Users/wurh/Desktop/SW/apps/android/src/main/java/com/example/sonicwavev4/feature/sonicwave/data/SonicWaveProductController.kt`
8. `/Users/wurh/Desktop/SW/apps/android/src/main/java/com/example/sonicwavev4/feature/sonicwave/session/SessionCoordinator.kt`

## 8. 哪些语义不能被产品层直接误用

- 不要把 firmware `STATE` 直接等同于 APP `SessionControlState`
- 不要把 `FAULT_STOP` 直接等同于 `STOPPED_BY_DANGER`
- 不要把 `BASELINE.main_state` 直接等同于产品会话状态
- 不要把 firmware `STOP_SOURCE` 和 APP 当前 repository `source=FAULT_EVENT/CONNECTION_STATE` 混为同一字段
- 当前桥接格式是 `EVT:FAULT <code> reason=<NAME>`，不要把它误解成最终 canonical stop 协议

## 9. 历史文档引用建议

以下文档后续引用要谨慎：

- `docs/protocol.md`
  - 该文档仍以旧的 `STATE/FAULT/STABLE/PARAM/STREAM` 视角为主，未完整覆盖当前 `SAFETY/STOP/BASELINE` 导出。
- `docs/system/task4_safety_contract.md`
  - 它描述的是 firmware 对齐目标与 demo engineering 消费原则，不等于 formal SW APP 当前已消费到这些语义。
- `docs/system/firmware_safety_behavior.md`
  - 它描述的是期望行为摘要，不足以代替当前 formal SW APP 消费现状说明。
- `docs/system/demo_app_signal_meaning_zh.md`
  - 它是 demo engineering console 文档，不是正式 SW APP 契约文档。

建议作为当前 canonical 文档使用：

- `docs/system/esp32_app_communication_semantics.md`

原因：

- 同时覆盖 firmware 导出语义与 formal SW APP 实际消费边界
- 明确 stop / fault / state / baseline 的 owner 分工
- 明确哪些输出已经导出，哪些尚未被正式产品消费
