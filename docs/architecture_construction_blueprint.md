# ESP32-S3 固件架构施工蓝图（只读设计版）

状态：批次 1 已施工并通过统一固件编译；批次 2 及授权闭环尚未施工。本文区分当前源码事实、目标设计和实际验证结果。

## 1. 人话版运行路线

1. APP 通过 BLE 连接设备并发送命令。
2. `BleTransport` 接收 BLE 写入，放入控制队列；控制任务调用 `ProtocolCodec` 解析，再交给 `CommandBus`。
3. 当前 `CommandBus` 的唯一处理器是 `main.cpp` 的 `HubHandler`。它把命令分给状态机、波形模块或激光模块，并生成 ACK/NACK。
4. `SystemStateMachine` 是律动启动、停止和安全停波的最终 Owner；只有它可以决定调用 `WaveModule::start/stopSoft`。
5. `LaserModule::LaserTask` 周期读取传感器，形成在位、稳定、离位、测量故障等事实；相关状态送入状态机，并通过 `EventBus` 上报 APP。
6. `WaveModule::I2S_Audio` 独占 I2S 输出，同时处理正弦波和连接/断开提示音。
7. 状态、测量、故障和快照经 `EventBus -> BleTransport -> ProtocolCodec -> BLE Notify` 返回 APP。

核心原则：先冻结上述旧链路，再拆分职责；授权模块以后只能作为“设备使用资格输入”，不能改写波形算法、测量采样或安全故障语义。

## 2. 当前源码事实与架构裁决

| 项目 | 当前事实 | 裁决 |
|---|---|---|
| 模块基础 | 已有 BLE、协议、命令总线、事件总线、状态机、波形、激光及大量激光子模块 | 通过：保留并复用 |
| 主入口 | `main.cpp` 仍集中组装和承载 `HubHandler` 的业务分发 | 部分通过：需先收窄入口职责 |
| 命令分发 | `CommandBus` 只有一个 `CommandHandler`，大量分支在 `HubHandler` | 部分通过：建立窄服务接口，保留兼容适配器 |
| 协议层 | `ProtocolCodec` 同时承载解析、编码和兼容逻辑 | 部分通过：拆分解码、编码、兼容适配，但保持线格式不变 |
| 激光层 | `LaserModule` 是大门面，已包含测量、稳定、校准、配置、流等子模块；并持有 FSM/Wave 指针 | 部分通过：先保留门面，逐步抽出纯策略；禁止形成新的双 Owner |
| 安全 Owner | `SystemStateMachine::setState()` 统一控制波形启停 | 通过：必须保持唯一 Owner |
| 任务边界 | `I2S_Audio`、`LaserTask`、BLE 控制/OTA/TX 任务已存在 | 通过：先记录队列和锁边界，再改依赖 |

结论：`architecture-partial`。现有基础足以施工，但不能直接新增授权业务；必须先完成第一阶段命令路由收口并通过回归。

## 3. 模块接口蓝图

| 模块 | 唯一职责 | 输入 | 输出 | 允许依赖 | 禁止依赖 |
|---|---|---|---|---|---|
| `BleTransport` | BLE 生命周期、RX/TX 队列、会话隔离 | BLE callback、原始字符串、事件 | `Command`、ACK/NACK、Notify | ProtocolCodec、CommandBus、EventBus | 直接操作 Wave/Laser/FSM |
| `ProtocolCodec` | 线协议解码/编码 | 字符串、Command、Event | Command/Event 字符串 | Types、协议常量 | 业务状态判断、硬件调用 |
| `CommandBus` | 命令投递 | `Command` | 处理结果 | 窄 `CommandHandler` 接口 | 解析字符串、访问硬件 |
| `CommandRouter`（目标） | 按命令类别选择业务服务 | 已解析 `Command` | ACK/NACK | 服务接口、权限/资格接口 | 直接写 I2S、直接读传感器 |
| `SystemStateMachine` | 启停、安全状态和恢复 | 在位、传感器、安全、用户启停事实 | 状态变更、Wave 控制、事件 | Wave 控制接口、EventBus | BLE、协议解析、支付/授权存储 |
| `WaveModule` | I2S、DDS、软停、提示音 | 参数、状态机控制、Cue 请求 | PCM/I2S、波形事件 | I2S、EventBus | BLE、订单、授权规则 |
| `LaserMeasurementReader` | 传感器读写与有效性 | 传感器配置 | 原始/有效测量 | 硬件驱动、锁 | Wave、BLE、订单 |
| `LaserModule`（过渡门面） | 编排测量子模块并向 FSM 提供事实 | 读数、配置命令 | 在位/稳定/故障事件 | 测量子模块、FSM、EventBus | 协议字符串、授权判定 |
| `DeviceAuthorization`（后续） | 保存并评估设备资格 | APP 下发的授权凭据、设备身份、时间/版本事实 | 资格快照、授权 ACK/NACK | 独立 NVS、设备身份、窄策略接口 | 改写安全故障、直接控制 I2S |
| `FirmwareDeviceIdentityProvider` | 提供不可变设备身份 | 芯片/固件事实 | 设备身份 | ESP-IDF | 业务订单状态 |
| `FirmwareDiagnostics` | 有界日志与诊断 | 事件、错误 | 中文日志/诊断摘要 | 诊断存储 | 阻塞实时任务 |

## 4. 任务与时序边界

| 上下文 | 当前任务 | 主要职责 | 施工约束 |
|---|---|---|---|
| BLE 回调 | 协议栈上下文 | 捕获连接、断开、写入 | 不做业务判断、不阻塞、不直接碰硬件 |
| BLE 控制任务 | `BleCtrl` | 顺序处理 RX、连接生命周期 | 通过队列调用 Router；保留 session/conn fence |
| BLE TX 任务 | `BleTx` | 控制事件和流事件通知 | 不阻塞业务 Owner；保留优先级和丢弃策略 |
| OTA 任务 | `BleOta` | OTA 控制和数据 | 与业务命令隔离，保留回滚确认 |
| 波形任务 | `I2S_Audio` | DDS、软停、Cue、I2S DMA | 不能被授权、BLE、日志同步阻塞 |
| 激光任务 | `LaserTask` | 采样、判稳、在位/安全事实 | 只发布事实或调用 FSM 输入，不直接停波 |
| 主任务 | `setup/loop` | 组装、初始化、轻量守护 | 不承载业务分支；`loop` 保持轻量 |

## 5. 用户操作到设备结果的闭环

| 用户操作 | APP | ESP32 真相源/Owner | 结果 |
|---|---|---|---|
| 连接设备 | 扫描、连接、订阅 Notify | `BleTransport` 会话 | 连接事件和快照可见，提示音按 session 去重 |
| 发送启动 | 发 `WAVE_START` | Router → FSM → Wave | 仅满足在位、稳定、无故障和后续授权条件时进入 RUNNING |
| 训练中离位 | 展示停止/保护原因 | `LaserTask` 发现事实，FSM 决定停波 | 受控软停，不伪造授权或未知故障 |
| BLE 断连 | 连接状态变化 | `BleTransport` 投递断连，FSM 停波 | 软停并播放断连 Cue，沿用现有 session 规则 |
| 发送授权（后续） | 登录、订单号、设备识别码，接收授权结果 | 后端是订单/支付真相源；设备只保存签发凭据并输出本地资格 | 授权失败只拒绝需要资格的业务命令，不改变安全链路 |

## 6. 保留/迁移/重建矩阵

| 现有对象 | 处理 | 原因 |
|---|---|---|
| `WaveModule` I2S/DDS/软停 | 保留，补窄接口 | 已是实时 Owner，改动风险最高 |
| `SystemStateMachine` | 保留，逐步减少对具体模块的反向依赖 | 继续作为唯一安全启停 Owner |
| `LaserModule` | 保留门面，内部策略逐步下沉 | 已有子模块，先避免一次性大爆炸重构 |
| `HubHandler` | 第一阶段保留为兼容适配器，后续缩小 | 先保证旧 APP 命令不变 |
| `ProtocolCodec` | 拆出纯解码/编码接口，旧函数做代理 | 线协议和兼容行为不能改变 |
| `CommandBus/EventBus` | 保留，补接口和测试 | 是跨上下文的稳定边界 |
| `main.cpp` | 只保留组装和依赖注入 | 消除业务分支集中 |
| 授权模块 | 后续新建独立模块 | 不得混入 Laser/Wave/Safety |
| 删除旧路径 | 暂不删除 | 必须等新路径回归完成并有可回滚点 |

## 7. 施工批次与验收

### 批次 0：冻结基线（当前阶段）

- 记录旧 APP 的连接、CAP/SNAPSHOT、WAVE、EVT、断连、离位、故障行为。
- 固定 BLE 线格式、ACK/NACK、session/conn fence、启动和停波语义。
- 不改源代码，不新增授权行为。

### 批次 1：命令路由收口（第一施工包）

- 新增窄的命令服务接口和 Router；`HubHandler` 只做兼容转接。
- `WAVE_START/STOP/SET` 仍必须落到原 FSM/Wave 路径。
- `DEVICE_*` 配置命令仍落到原 Laser 配置路径。
- 禁止同时改波形算法、激光阈值、协议线格式和断连语义。

验收：旧 APP 连接、启动、停止、断连 Cue、离位保护、CAP/SNAPSHOT/EVT 全部与基线一致；任一失败即回滚本批次。

### 批次 2：协议层拆分

- 拆出纯 `CommandDecoder`、`EventEncoder`、`LegacyProtocolAdapter`。
- 保留 `ProtocolCodec` 作为兼容外观，确保旧协议字节级/字段级兼容。

验收：命令矩阵、事件矩阵、异常输入和旧 APP 回归通过。

### 批次 3：激光门面收口

- 以现有测量、稳定、在位、校准子模块为基础，明确输入/输出。
- `LaserModule` 只编排，不让子模块反向控制 Wave。

验收：采样周期、稳定窗口、在位/离位、传感器故障和恢复行为不变。

### 批次 4：授权技术闭环

- 新增独立身份/凭据存储/资格评估接口。
- APP 通过 BLE 传递签发结果；设备仅核验签名凭据和本地策略。
- 授权拒绝只影响需要授权的命令；安全停波仍由 FSM 独立负责。

验收：全款、分期、过期、缴清、重复授权、断电重启、无网络、篡改凭据和旧 APP 兼容矩阵。

## 8. 本阶段明确不做

- 不修改 `WaveModule` 的算法、I2S 参数和声音时序。
- 不修改 `LaserTask` 的采样周期、判稳窗口和保护阈值。
- 不把授权失败映射成 `FAULT_STOP` 或安全故障。
- 不让 ESP32 自行 Wi‑Fi 核验进入首版施工；仅预留接口。
- 不修改 APP、后端或支付逻辑；这些属于后续跨端闭环。

## 9. 进入代码施工的前置条件

1. 用户确认本蓝图中的模块边界和批次顺序。
2. 先建立独立 firmware worktree/分支，不动当前共享工作区。
3. 为批次 0 固化命令、事件、任务和回归矩阵。
4. 批次 1 只做路由结构变更，完成 focused tests、编译和 `git diff --check` 后再进入批次 2。

## 10. 多轮资料复核结论

### 10.1 FreeRTOS 与任务边界

ESP-IDF 官方文档明确：队列按值复制消息；队列发送/接收应避免在不适合的上下文阻塞；任务可以固定到指定核心，但任务栈、优先级和队列容量必须作为运行时资源审计对象。因此本项目的“BLE 回调只入队、业务任务处理、实时输出任务独占 I2S”方向是可行的，但施工时必须补齐：

- 每个队列的长度、消息大小、满队行为和丢弃策略。
- 每个任务的栈高水位、CPU 占用、最高阻塞时长。
- 所有 `portMAX_DELAY` 的调用点是否可能把实时链路拖住。
- BLE TX 控制消息和流消息的优先级是否始终满足控制优先。

当前源码已经有 `BleCtrl`、`BleOta`、`BleTx`、`I2S_Audio`、`LaserTask` 和诊断任务；但这些资源指标还没有形成一份统一基线，所以暂定为“可施工但未放行”。

### 10.2 授权安全与存储

Espressif 官方资料建议生产设备组合使用 Secure Boot v2、Flash Encryption 和 NVS Encryption。当前仓库的 `sdkconfig.*` 中没有形成已确认的生产安全配置，且现有激光/配置代码大量使用 Arduino `Preferences`。因此：

- 不能把“授权码写入 NVS”直接视为防破解方案。
- 授权凭据应采用签名凭据，不保存服务器私钥；设备只内置公钥或验证材料。
- 需要单独审计 NVS 分区、密钥保护、量产 eFuse、OTA 签名和恢复流程。
- 安全配置一旦烧写 eFuse，可能影响返修、刷机和 OTA，必须先做量产/返修方案，不能在本轮重构中顺手开启。

### 10.3 测试方式

ESP-IDF 官方支持 Unity 目标测试；本仓库已有 `tools/tests`、协议测试、激光测试和 BLE 测试脚本，说明测试基础存在。但当前蓝图缺少“架构变更不会影响旧行为”的逐项证据要求，已在下一节补全。

## 11. 施工前审计清单

| 审计项 | 当前状态 | 放行条件 |
|---|---|---|
| 命令全量覆盖 | 已有命令枚举和 `HubHandler` 分支，未形成可执行矩阵 | 每个命令列出 Owner、ACK/NACK、旧 APP 结果 |
| 事件全量覆盖 | 已有 `EventType` 和编码器 | 每个事件列出来源、队列、优先级、丢失策略 |
| 任务资源 | 任务和队列已找到 | 固化栈水位、队列水位、延迟和堆内存基线 |
| BLE 会话隔离 | 已有 session/conn fence | 覆盖旧会话延迟命令、重复连接、断连重连 |
| 状态机 Owner | `SystemStateMachine` 是停波 Owner | 禁止 Router、Laser、Authorization 直接停波 |
| NVS/配置 | 现有配置存储可用 | 授权存储必须独立命名空间、版本和失败回滚 |
| 时间可信性 | 现有大量 `millis()` | 授权期限不得直接依赖可回拨的墙上时间；离线策略需产品确认 |
| 安全启动 | 生产配置未确认 | 完成签名、加密、eFuse、返修、OTA 回滚评审 |
| 真实设备证据 | 需要硬件/APP 配合 | 通过指定设备完成 BLE、声音、停波和重启验收 |

## 12. 测试与验收矩阵

### 12.1 纯策略/主机测试

- 命令解码：合法、大小写、缺参数、超长、乱码、未知命令。
- 命令路由：每个 `CmdType` 只进入一个 Owner；禁止直接访问硬件。
- 状态机：`IDLE/ARMED/RUNNING/FAULT_STOP` 的合法和非法转换。
- 授权策略：全款、分期有效、到期、缴清、撤销、重复下发、版本不兼容、签名错误。
- 会话策略：旧 session 命令、重复断连、断连后迟到 ACK、重连后旧 Cue。
- 配置失败：NVS 打开失败、写失败、读回不一致、升级中断后的恢复。

### 12.2 目标板测试

- 冷启动：无 BLE、连接、快速断连重连、启动测量未就绪。
- 波形：启动、参数变更、软停、连接 Cue、断连 Cue，确认 I2S 无竞争。
- 激光：在位、稳定、离位、传感器异常、恢复窗口。
- 资源：任务栈高水位、队列峰值、堆内存、BLE TX 背压、长时间运行。
- 授权：授权写入后重启、掉电恢复、无网络、重复授权、篡改凭据。
- OTA：签名失败、版本回滚、断连、断电恢复；不得破坏授权存储和旧协议。

### 12.3 APP/设备联调验收

1. APP 连接设备，收到连接事件和快照。
2. APP 发送旧版 `CAP?`、`SNAPSHOT`、`WAVE:*`，结果与重构前一致。
3. APP 发送授权凭据，设备返回明确的授权 ACK/NACK；授权失败不伪造成安全故障。
4. 训练中离位或断连，设备按原安全流程停波，APP 收到原有中文可读原因。
5. 重连后旧会话命令不生效，新会话可以正常操作。

## 13. 本轮复核后的施工放行结论

- **架构方向：可行。** 分层、单一 Owner、队列通信与现有代码结构兼容。
- **蓝图严谨性：已补齐。** 增加了任务资源、授权安全、时间、NVS、测试和真实设备证据边界。
- **施工可行性：部分放行。** 可以准备批次 0 和批次 1 的设计/测试，不可以直接施工授权功能。
- **当前禁止事项：** 不烧 eFuse、不启用生产安全配置、不改变波形/激光阈值、不新增授权线协议、不在共享工作区切分支。
- **下一施工入口：** 先建立独立 worktree，完成命令/事件/任务基线和批次 1 focused tests，再开始路由收口。

## 14. 复核资料

- [Espressif FreeRTOS 与队列 API](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/freertos.html)
- [Espressif ESP32-S3 Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html)
- [Espressif ESP32-S3 Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html)
- [Espressif NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/v5.2.4/esp32s3/api-reference/storage/nvs_encryption.html)
- [Espressif Unit Testing](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/api-guides/unit-tests.html)

## 15. 实际施工记录

### 批次 1 已完成：BLE 生命周期职责收口

- 新增 `src/core/BleLifecycleCoordinator.h/.cpp`。
- `BleLifecycleCoordinator` 独立负责连接、断连、会话 Cue、断连停波和流订阅清理。
- `HubHandler` 现在只保留命令处理职责，不再继承 `BleDisconnectSink`。
- `main.cpp` 只负责组装 `HubHandler` 和 `BleLifecycleCoordinator`，BLE 生命周期入口改接后者。
- 原 BLE 线格式、命令分支、状态机、波形模块、激光模块均未改写。

### 已完成验证

| 验证项 | 结果 | 证据 |
|---|---|---|
| 统一固件编译 | 通过 | `python3 -m platformio run -e esp32s3_unified` |
| 编译产物 | 通过 | `firmware.elf`、`firmware.bin` 生成成功 |
| 内存检查 | 通过 | RAM 15.1%，Flash 15.8% |
| 代码格式检查 | 通过 | `git diff --check` |
| 真机 BLE/声音/安全行为 | 未执行 | 用户要求统一测试，待后续硬件验收 |

### 当前批次边界

- 本批次是结构迁移，不代表完整授权功能已完成。
- 未修改授权协议、支付、订单、波形算法、激光阈值和安全配置。
- 未烧写 Secure Boot/Flash Encryption 相关 eFuse。
- 下一批次进入协议解码、事件编码和兼容适配拆分前，需先确认本批次真机回归结果。

### 批次 2 已完成：协议边界适配

- 新增 `src/core/ProtocolBoundary.h/.cpp`。
- `CommandDecoder` 统一承接 BLE 原始命令解码入口。
- `EventEncoder` 统一承接事件和快照编码入口。
- `ProtocolCodec` 暂作为兼容实现保留，未改变任何旧协议字符串。
- `BleTransport` 不再直接依赖具体解析/编码函数实现，只依赖协议边界接口。

### 批次 2 验证

| 验证项 | 结果 |
|---|---|
| `esp32s3_unified` 编译 | 通过 |
| 固件 ELF/BIN 生成 | 通过 |
| RAM/Flash 预算 | RAM 15.1%，Flash 15.8% |
| 协议线格式变更 | 未变更 |
| 真机协议回归 | 待统一测试 |

### 批次 3 已完成：激光只读波形观察边界

- 新增 `src/core/WaveObservation.h`，只提供输出状态和参数摘要读取。
- `WaveModule` 实现 `WaveObservation`，仍保留唯一的启动/停止控制能力。
- `LaserModule` 不再依赖 `WaveModule` 具体类型，只依赖只读观察接口。
- 激光模块仍通过 `SystemStateMachine` 报告配置故障和安全事实，不直接操作 I2S 或波形启停。

### 批次 3 验证

| 验证项 | 结果 |
|---|---|
| `esp32s3_unified` 编译 | 通过 |
| 固件 ELF/BIN 生成 | 通过 |
| RAM/Flash 预算 | RAM 15.1%，Flash 15.8% |
| 激光模块直接引用 `WaveModule` | 已移除 |
| 真机测量/停波行为 | 待统一测试 |

