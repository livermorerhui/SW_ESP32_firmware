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
| baseline-main 证据 | `BASELINE` | `LaserModule + RhythmStateJudge` | baseline_ready、MA7、ratio、danger evidence |
| 测量健康 | `SNAPSHOT.measurement_health` | `LaserModule` | 测量链路运行健康态：`BOOTING/PROBING/READY/TRANSIENT_UNAVAILABLE/FAULT` |

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
- `USER_LEFT_PLATFORM` with `SAFETY:LEAVE_PROTECTION enabled=0` -> `effect=WARNING_ONLY`
- `FALL_SUSPECTED` -> `effect=ABNORMAL_STOP`
- `BLE_DISCONNECTED` -> effect 取决于 disconnect policy；当前 APP-driven 默认构建为 `RECOVERABLE_PAUSE`

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
- final action owner：`SystemStateMachine`
- 默认 effect：`RECOVERABLE_PAUSE`
- 若发生在 `RUNNING`：
  - `EVT:STOP stop_reason=USER_LEFT_PLATFORM stop_source=FORMAL_SAFETY_OTHER effect=RECOVERABLE_PAUSE state=IDLE`
  - `EVT:STATE IDLE`
  - `EVT:FAULT 100`
  - `EVT:SAFETY reason=USER_LEFT_PLATFORM code=100 effect=RECOVERABLE_PAUSE state=IDLE wave=STOPPED`
- 若 `SAFETY:LEAVE_PROTECTION enabled=0`：
  - 保留离台检测与串口日志
  - 发出 `EVT:SAFETY reason=USER_LEFT_PLATFORM code=100 effect=WARNING_ONLY state=RUNNING wave=RUNNING`
  - 不因为该离台事件进入 `RECOVERABLE_PAUSE`
  - 不改变 `FALL_SUSPECTED` 的自动停波配置

重要说明：

- `USER_LEFT_PLATFORM` 确实已对外导出
- 命名 reason 仍以 `SAFETY` / `STOP` 为主 owner
- 但当前阶段也会桥接到 `EVT:FAULT 100 reason=USER_LEFT_PLATFORM`

### 4.1.1 Leave protection switch protocol

正式命令：

- `SAFETY:LEAVE_PROTECTION enabled=0/1`

兼容别名：

- `DEBUG:LEAVE_STOP enabled=0/1`

ACK：

- `ACK:LEAVE_PROTECTION enabled=<0/1> supported=1 effect=<ENABLED_PAUSE|WARNING_ONLY>`

能力与快照：

- `ACK:CAP ... leave_stop_supported=1`
- `SNAPSHOT: ... leave_stop_enabled=<0/1>`

边界：

- 固件是 `leave_stop_enabled` 真相源。
- APP 只能消费 `ACK:LEAVE_PROTECTION`、`ACK:CAP leave_stop_supported`、`SNAPSHOT leave_stop_enabled`。
- 旧固件没有这些字段时，APP 必须视为不支持，不得伪造开关状态。
- `enabled=0` 只关闭 `USER_LEFT_PLATFORM` 自动暂停 / 停波动作；检测、日志、`EVT:SAFETY` 可见性仍保留。

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

正式 SW APP 当前实际消费分成四层：

1. bootstrap / connect-time truth

- `CAP? -> ACK:CAP`
- `fw`
- `proto`
- `platform_model`
- `laser_installed`

2. runtime readiness truth

- `SNAPSHOT.start_ready`
- `SNAPSHOT.measurement_health`
- `SNAPSHOT.degraded_start_available`
- `SNAPSHOT.leave_stop_enabled`
- reconnect 后重新拉取 `SNAPSHOT`

### 5.0.1 measurement health truth

`measurement_health` 是固件测量链路运行时真相源，不属于 `ACK:CAP`。

当前正式值：

- `BOOTING`
- `PROBING`
- `READY`
- `TRANSIENT_UNAVAILABLE`
- `FAULT`

消费规则：

- `BOOTING/PROBING` 表示启动期或首个有效测量前的探测状态，不得被 APP 升级为“激光故障”。
- `READY` 表示连续有效测量已确认，APP 应清除由测量健康导致的不可用/故障提示。
- `TRANSIENT_UNAVAILABLE` 表示 ready 后短暂不可用，默认不弹激光故障；后续如需 UI 提示，应作为初始化/临时不可用，不作为维修故障。
- `FAULT` 才是正式测量链路故障，可驱动激光异常、degraded-start 维修确认、顶栏设置不可用。
- 固件不得在 `TRANSIENT_UNAVAILABLE` 阶段通过 `setSensorHealthy(false)` 抢先发布正式 `MEASUREMENT_UNAVAILABLE`；只有进入 `FAULT` 才能发布正式故障 truth。`READY/FAULT` 切换必须发布 `SNAPSHOT`，供 APP 对账。
- 旧固件没有该字段时，APP 才回退到 `laser_available / protection_degraded` 兼容逻辑。
- APP 不得用本地定时器伪造 `measurement_health`。

启动连接规则：

- ESP32-plus 上电后，BLE advertising 应等测量健康启动判定完成后再开放；判定完成指 `READY` 或 `FAULT`，无激光配置也视为完成。
- 这样 APP 不需要按“连接早晚”猜状态；连接后只按 ESP32 的正式 `SNAPSHOT / ACK / EVT` 处理。
- 固件启动等待必须有串口证据：`[BLE_STARTUP_GATE] result=<resolved|timeout> health=<...> wait_ms=<...>`。

降级开始规则：

- `MEASUREMENT_UNAVAILABLE`、`measurement_health=FAULT`、`protection_degraded=true` 可以驱动保护失效 UI 和设置入口不可用，但不能被 APP 用来伪造 `degraded_start_available=true`。
- 是否允许保护模块故障后继续开始，只能由固件 `SNAPSHOT.degraded_start_available / degraded_start_enabled` 或 `ACK:DEGRADED_START` 决定。
- 如果固件 ACK 返回 `available=false enabled=false`，APP 必须停止本次 start 链路，不得继续发送 `WAVE:START`。

3. control confirmation truth

- `EVT:WAVE_OUTPUT`
- authoritative `SNAPSHOT.wave_output_active`

4. stop / safety / compatibility truth

- 已正式接入：`SAFETY`、`STOP`、`BASELINE`
- 仍保留兼容链：`STATE`、`FAULT`、`STABLE`、`PARAM`、`STREAM`

### 5.1 关键帧交付与 reconnect truth

当前 BLE TX 使用 notify 文本行输出。它不是应用层确认交付机制，因此以下规则必须固定：

- `EVT:STREAM` 是 measurement plane，可丢最新样本外的历史样本。
- `EVT:STREAM` 不能被 control/status frame 无限期饿死；BLE TX 调度必须保留“latest stream”并设置有界公平预算，确保 Demo APP 校准和遥测曲线能持续收到实时距离 / 体重样本。
- `EVT:STATE`、`EVT:WAVE_OUTPUT`、`EVT:FAULT`、`EVT:SAFETY`、`EVT:STOP` 属于关键 truth frame，内部重构必须可观察其入队失败或断链跳过。
- BLE 已断开时，固件不能假装 `EVT:*` 已经交付给 APP；此时 notify 路径只能记录串口诊断。
- reconnect 后恢复真实状态的正式通道仍是 `SNAPSHOT`，不能新增临时 BLE 线格式表达同一件事。
- APP / Demo APP 不应把断链窗口里缺失的 `EVT:*` 当成固件状态未变化；应以 reconnect 后 `SNAPSHOT` 为 authoritative runtime truth。
- 如果断链窗口跳过关键 truth frame，`BleTransport` 会设置内部 reconnect snapshot dirty 标记；下一次 `SNAPSHOT` notify 成功发出后清除此标记并记录串口补偿日志。

2026-06-02 的 Demo APP 遥测 / 校准修复确认：

- 问题现象：ESP32 测量平面有有效样本，但 Demo APP 实时曲线、实时体重、实时距离和校准录点缺少实时数据。
- 根因证据：修复前 capture 中 `MEASUREMENT_DIAG ok` 持续增长，Android 只收到极少 `EVT:STREAM`，同时串口出现大量 `queue=stream suppressed_for_control`。
- 修复边界：不改 `EVT:STREAM` wire format，不改校准算法，不改 MAX485/Modbus 参数；只调整 BLE TX 内部调度。
- 当前调度参数：control/status 仍优先；stream 保留最新样本；最多延迟 250ms 或最多连续发送 4 个 control frame 后让出一次 stream；延迟重试间隔 5ms。
- 后续如果要让 SW APP 默认不接收实时距离、Demo APP 按需接收，推荐新增显式订阅合同，例如 `STREAM:SET enabled=1 rate_hz=10`、`ACK:STREAM ...`、`ACK:CAP stream_control_supported=1`。不要靠 APP 名称或连接来源猜测是否发送实时流。

2026-04-22 的 `BleTransport critical delivery observability` 真机验证确认：

- 正常 `WAVE:START -> RUNNING -> WAVE:STOP -> ARMED` 链路通过。
- 未出现 `enqueue_failed`、`lifecycle_enqueue_failed` 或 control queue overflow。
- 断链窗口出现过 `EVT:STATE IDLE`、`EVT:FAULT 102`、`EVT:SAFETY reason=BLE_DISCONNECTED ...` 的 `send_skipped reason=not_connected` 串口诊断；这是预期可观察事实，不是 BLE wire contract 变更。

相关代码：

- `/Users/wurh/Desktop/SW/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/Model.kt:48-82`
- `/Users/wurh/Desktop/SW/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/ProtocolCodec.kt:24-40`

### pause recoverable 当前真正依赖

formal SW APP 当前不再是“完全不看 `SAFETY/STOP/BASELINE`”。

当前 pause / stop 语义由以下链路共同承担：

- `SAFETY`
- `STOP`
- `BASELINE`
- `ProductController`
- `SessionSafetyInterventionBridge`
- `SessionCoordinator`

同时为了兼容旧链，以下语义仍然保留：

- `FAULT.reason`
- `STATE`

因此当前真实状态不是“formal 仍只靠 `FAULT.reason`”，而是：

- 新 owner 已接入
- 旧兼容桥仍未完全删除

### danger stop 当前真正依赖

formal SW APP 当前 danger stop 也不是单独依赖 `FAULT`。

它当前会同时消费：

- `STOP`
- `SAFETY`
- `FAULT` 兼容 reason

再由产品层汇总成 `STOPPED_BY_DANGER` 等会话语义。

### readiness / continue 当前真正依赖

- `BASE`：连接即允许 start
- laser-equipped non-BASE：信 firmware `start_ready`
- measurement unavailable：信 `degraded_start_available / degraded_start_enabled`
- continue / resume：仍要求 device connected + `start_ready`

`baseline_ready` 不再是 ESP32 start / continue 的正式 owner。

## 6. 当前契约缺口

当前最大契约缺口已经不是“formal SW APP 尚未接入 `STOP/SAFETY/BASELINE`”。

当前更真实的缺口是：

- 旧兼容链仍未完全退休
- 仍需要避免误删 `STATE/FAULT/STABLE/PARAM/STREAM`
- 文档与测试如果继续描述成“formal 只消费旧链”，会反向诱导错误实现

因此当前建议是：

- 承认 formal 已接入新的 owner 语义
- 同时明确旧链还在兼容期
- 在兼容期结束前，不要依据旧文档删除旧输出

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
