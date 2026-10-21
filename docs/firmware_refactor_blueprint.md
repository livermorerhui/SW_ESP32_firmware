# ESP32-S3 固件完整重构施工蓝图

版本：2026-10-21  
状态：施工基线  
适用分支：`codex/construct/esp32-architecture`

## 1. 目标

围绕“律动仪后续需要接入设备授权，但不能破坏现有律动、声音、激光和安全功能”的需求，完成 ESP32-S3 固件结构重构。

本蓝图的最终目标不是简单拆文件，而是实现：

- 每个模块只有一个明确职责和 Owner。
- 模块之间只通过窄接口、命令或事件通信。
- BLE、协议、授权、律动会话、安全、激光、波形和硬件相互隔离。
- 原有 APP 协议和设备行为保持兼容。
- 重构完成后可以构建、烧录，并由用户统一进行真实设备验收。

## 2. 用户需求对应的设备闭环

```text
客户使用 APP
  ↓ BLE 连接
ESP32 识别设备和会话
  ↓
APP/后端完成订单授权后，APP 下发签名授权凭据
  ↓
ESP32 保存并核验设备授权状态
  ↓
用户发送 WAVE_START
  ↓
授权资格 + 在位 + 稳定 + 传感器健康 + 安全状态
  ↓
SystemStateMachine 决定是否允许启动
  ↓
WaveModule 输出律动
  ↑
LaserModule 持续报告在位、稳定、离位和测量故障事实
```

关键边界：

- 后端是订单、支付和合同状态的真相源。
- APP 是联网核验和 BLE 传输的当前主链。
- ESP32 是本地设备身份、凭据核验、实时安全和输出执行的真相源。
- ESP32 首版不自行通过 Wi-Fi 核验授权。
- 授权失败不能伪装成 `FAULT_STOP`、传感器故障或离位故障。

## 3. 当前源码事实

| 区域 | 当前实现 | 重构判断 |
|---|---|---|
| BLE | `BleTransport` 已有 RX、控制、OTA、TX 任务和 session/conn fence | 保留，协议策略继续下沉 |
| 命令 | `CommandBus` 只有一个处理器，`HubHandler` 在 `main.cpp` 承载大量分支 | 必须拆成命令服务和兼容适配器 |
| 协议 | `ProtocolCodec.h` 同时负责解析、编码和兼容 | 拆出解码、编码、旧协议适配 |
| 状态 | `SystemStateMachine` 管理启停、保护、恢复和快照 | 保留为唯一安全启停 Owner |
| 波形 | `WaveModule` 独占 I2S/DDS/Cue 任务 | 保留，禁止其他模块直接启动/停止 |
| 激光 | `LaserModule` 已有测量、稳定、校准、配置和摘要子模块，但门面仍很大 | 保留过渡门面，逐步下沉策略 |
| 事件 | `EventBus` 当前单 sink、同步发布 | 明确线程、背压和事件丢失策略 |
| 授权 | 旧授权原型在归档分支，不在当前重构分支 | 完整重构后重新接入，不原样搬运 |

当前架构裁决：`architecture-partial`。前三批已完成边界基础设施，但不代表完整重构完成。

## 4. 目标分层

```text
┌──────────────────────────────────────┐
│ 接入层                                │
│ BleTransport / OTA / BLE session      │
└──────────────────┬───────────────────┘
                   │ raw frame / event
┌──────────────────▼───────────────────┐
│ 协议层                                │
│ CommandDecoder / EventEncoder /       │
│ LegacyProtocolAdapter                 │
└──────────────────┬───────────────────┘
                   │ typed command
┌──────────────────▼───────────────────┐
│ 应用编排层                            │
│ CommandRouter / CapabilityService /   │
│ ConfigService / CalibrationService    │
└─────────────┬──────────────┬──────────┘
              │              │
┌─────────────▼──────┐ ┌─────▼────────────┐
│ 资格与会话层        │ │ 安全状态层       │
│ Authorization       │ │ SystemStateMachine│
│ Session/StartPolicy │ │ Safety/Fault      │
└─────────────┬──────┘ └─────┬────────────┘
              │              │
┌─────────────▼──────────────▼────────────┐
│ 设备能力层                              │
│ WaveModule / LaserModule / Calibration  │
└──────────────────┬───────────────────────┘
                   │
┌──────────────────▼───────────────────────┐
│ 硬件与系统基础层                          │
│ I2S / Modbus / NVS / FreeRTOS / logging   │
└───────────────────────────────────────────┘
```

## 5. 模块接口合同

| 模块 | 允许输入 | 允许输出 | 可依赖 | 禁止事项 |
|---|---|---|---|---|
| `BleTransport` | BLE 回调、原始帧、Event | typed command、发送帧 | 协议边界、CommandBus、EventBus | 不调用 Wave/Laser/FSM |
| `CommandDecoder` | 原始命令字符串 | `Command` 或解析错误 | 类型和协议常量 | 不判断业务资格 |
| `EventEncoder` | Event、Snapshot | 旧协议字符串 | 类型和协议常量 | 不读取硬件或状态内部字段 |
| `LegacyProtocolAdapter` | 旧命令/旧 ACK | 标准命令/兼容响应 | Decoder/Encoder | 不新增业务规则 |
| `CommandRouter` | typed command | ServiceResult/ACK | 各服务接口 | 不直接碰硬件 |
| `AuthorizationService` | 凭据、设备身份、时间事实 | `AuthorizationSnapshot` | Store、Verifier、Policy | 不直接停波、不改安全故障 |
| `SystemStateMachine` | 用户启停、授权资格、在位、传感器和安全事实 | 状态、输出许可、事件 | WaveControl、EventBus | 不解析 BLE、不读订单 |
| `WaveModule` | 波形参数、FSM 输出许可、Cue | I2S、波形事件、只读摘要 | I2S、FreeRTOS | 不读授权、不读订单 |
| `LaserMeasurementReader` | 传感器配置 | 原始/有效测量 | Modbus/I2C、锁 | 不调用 Wave/FSM |
| `LaserModule` | 测量输入、配置命令 | 测量和安全事实 | Reader、纯策略、事实 Sink | 不直接停波 |
| `NvsStore` | 版本化记录 | 读写结果 | NVS | 不持有业务 Owner |
| `FirmwareDiagnostics` | 低频诊断事实 | 有界日志 | 非阻塞队列、时间源 | 不阻塞实时任务 |

## 6. 唯一 Owner 规则

| 事实/动作 | 唯一 Owner |
|---|---|
| BLE 连接生命周期 | `BleTransport` |
| 原始字符串解析 | `CommandDecoder` |
| 旧协议兼容 | `LegacyProtocolAdapter` |
| 命令分类 | `CommandRouter` |
| 授权凭据有效性 | `AuthorizationService` |
| 设备身份 | `FirmwareDeviceIdentityProvider` |
| 是否允许开始 | `SystemStateMachine` + `StartPolicy` |
| 波形启动/停止 | `SystemStateMachine` |
| I2S/DDS 输出 | `WaveModule` |
| 激光原始采样 | `LaserMeasurementReader` |
| 在位/稳定事实 | `LaserModule` 内部纯策略 |
| 设备持久化 | 对应 Store，不由页面/Router直接写 NVS |
| 对外事件编码 | `EventEncoder` |

## 7. 关键状态与时序

### 7.1 设备运行状态

```text
IDLE
  └─ 用户在位且满足准备条件 → ARMED
ARMED
  └─ 用户启动 + 授权有效 + 无故障 → RUNNING
RUNNING
  ├─ 用户停止 → IDLE
  ├─ 离位/断连/传感器保护 → FAULT_STOP 或既有恢复态
  └─ 授权失效 → 按产品规则拒绝下一次启动；运行中按安全停机合同执行
FAULT_STOP
  └─ 满足既有恢复窗口 → IDLE
```

授权状态不得替代设备运行状态；授权状态必须单独投影：

```text
AuthorizationSnapshot {
  deviceId
  mode: FULL / INSTALLMENT
  status: VALID / EXPIRED / REVOKED / INVALID / UNKNOWN
  effectiveAt
  expiresAt
  contractVersion
  credentialVersion
}
```

### 7.2 首次授权路线

```text
APP 登录并输入订单号
  → 后端核验订单、付款、设备唯一识别码
  → 后端签发授权凭据
  → APP 通过 BLE 发送授权命令
  → ESP32 校验设备 ID、签名、版本、时间策略
  → 独立 Store 原子保存
  → 读回校验成功
  → 返回授权 ACK
  → APP 再查询 CAP/SNAPSHOT
```

“收到授权码”不等于“授权已生效”；只有 ESP32 写入并读回校验成功，才返回成功 ACK。

## 8. 分阶段施工顺序

### 阶段 0：基线冻结

- 固化命令矩阵、事件矩阵、任务矩阵和旧 APP 行为。
- 建立重构前固件构建产物和版本哈希。
- 不改变业务行为。

### 阶段 1：入口和生命周期收口

- 已完成 BLE 生命周期协调器。
- `HubHandler` 不再承载 BLE 连接/断连动作。
- 保持旧协议和状态机行为。

### 阶段 2：协议边界

- 已建立 `CommandDecoder`、`EventEncoder` 适配边界。
- 下一步把真实解析/编码实现拆入独立文件。
- `ProtocolCodec` 暂作兼容门面。

### 阶段 3：设备能力依赖隔离

- 已完成 `WaveObservation` 只读接口。
- 下一步将 `LaserModule` 对 FSM 的多项写入收口为事实 Sink。
- 禁止激光模块直接控制波形。

### 阶段 4：命令服务拆分

- 按能力拆出 `CapabilityService`、`WaveCommandService`、`ConfigService`、`CalibrationService`、`ProtectionService`。
- `HubHandler` 变为 `LegacyCommandAdapter`。
- 所有服务返回统一 `ServiceResult`，由适配器生成旧 ACK/NACK。

### 阶段 5：状态机接口化

- 保留 `SystemStateMachine` 为唯一安全 Owner。
- 抽象 `WaveControl`、`MeasurementFacts`、`AuthorizationGate`、`SafetyEventSink`。
- 移除对具体模块的非必要反向依赖。

### 阶段 6：LaserModule 内部拆分

- `MeasurementRuntime`：采样和健康状态。
- `PresenceStabilityPolicy`：在位、稳定、开始准备。
- `CalibrationService`：校准模型和空载距离。
- `LaserConfigStore`：配置持久化。
- `RunSummaryCollector`：运行摘要。
- `LaserModule` 只保留编排和兼容外观。

### 阶段 7：协议实现拆分

- `CommandDecoder`、`EventEncoder`、`LegacyProtocolAdapter` 成为真实实现。
- 保留旧字符串、字段顺序、错误文案和事件语义。

### 阶段 8：任务与资源审计

- 固化任务栈高水位、队列水位、堆内存、事件延迟和阻塞时长。
- 处理所有 `portMAX_DELAY` 的实时影响。
- 明确事件可丢失等级和控制事件不可丢失等级。

### 阶段 9：重构完成评审

- 旧 APP 回归通过。
- BLE、波形、激光、安全和 OTA 回归通过。
- 无双 Owner、无未解释直接依赖。
- 形成最终架构文档和测试报告。

### 阶段 10：授权模块开发

仅在阶段 0–9 全部通过后进行：

- `AuthorizationVerifier`
- `AuthorizationPolicy`
- `AuthorizationStore`
- `AuthorizationService`
- `AuthorizationSnapshotProjection`
- 授权命令和 ACK/NACK
- 启动资格接入 `SystemStateMachine`

## 9. 测试蓝图

### 9.1 主机/纯策略测试

- 命令解析、路由和旧协议兼容。
- 状态机合法/非法转换。
- 授权签名、版本、设备 ID、过期、撤销、重复和篡改。
- NVS 记录版本、校验、读回失败和迁移失败。
- session/conn 乱序、迟到命令和重复断连。

### 9.2 目标板测试

- 冷启动、BLE 连接/断开/重连。
- CAP、SNAPSHOT、WAVE 全命令矩阵。
- 连接音、断开音、软停和 I2S 独占。
- 在位、稳定、离位、传感器故障和恢复。
- 任务栈、队列、堆和长时间运行。
- 授权写入、重启、掉电、无网络、无效凭据。

### 9.3 烧录和 OTA 测试

- 构建 `esp32s3_unified`。
- 烧录并记录固件哈希、设备身份和分区状态。
- 启动自检、BLE ready、传感器 ready。
- 首次 OTA 启动成功后确认有效。
- 首次启动期间断电/崩溃，确认回滚到旧版本。
- OTA 不破坏授权 Store、配置 Store 和旧协议。

## 10. 完整重构完成标准

只有以下条件全部满足，才称为“完整重构完成”：

1. `main.cpp` 只负责初始化和依赖组装。
2. `HubHandler` 已退化为旧协议兼容适配器。
3. 协议解码、编码和兼容实现分离。
4. `SystemStateMachine` 是唯一启停和安全 Owner。
5. `LaserModule` 不直接控制 Wave，不持有不必要的具体实现依赖。
6. 所有跨任务边界都有队列、阻塞和丢失策略。
7. 旧 APP 全部核心功能与重构前一致。
8. 编译、主机测试、目标板测试、烧录测试和 OTA 回滚证据齐全。
9. 授权模块尚未加入旧功能路径，直到重构评审通过。

## 11. 施工与交付规则

- 所有代码在独立 worktree/分支施工。
- 每个阶段独立提交，失败可单阶段回滚。
- 不把未完成重构合并到 `main`。
- 用户不需要逐批确认；只有产品规则、现场设备结果或不可推断的硬件问题才需要用户参与。
- 真机测试由用户统一执行；我负责准备构建产物、测试矩阵、串口观察点和结果解释。

## 12. 依赖图与初始化顺序

### 12.1 目标依赖方向

```text
main / CompositionRoot
  ├─ BleTransport → ProtocolBoundary → CommandBus
  ├─ CommandRouter → Capability/Config/Calibration/Protection Services
  ├─ AuthorizationService → Verifier + AuthorizationStore + DeviceIdentity
  ├─ SystemStateMachine → WaveControl + MeasurementFacts + EventSink
  ├─ LaserModule → MeasurementReader + PurePolicies + MeasurementFactsSink
  └─ WaveModule → I2S Driver + AudioTask
```

禁止形成以下反向依赖：

- `WaveModule → AuthorizationService`
- `LaserModule → WaveModule` 具体类型
- `BleTransport → SystemStateMachine/WaveModule/LaserModule` 具体业务对象
- `ProtocolCodec → 业务状态或硬件`
- `AuthorizationService → WaveModule/LaserModule`
- 任意模块绕过 `SystemStateMachine` 直接启停波形

### 12.2 初始化顺序

```text
基础日志/时间/硬件
  → DeviceIdentity
  → Stores（只打开，不执行业务迁移）
  → WaveModule/I2S task
  → SystemStateMachine
  → LaserMeasurementReader/LaserModule task
  → AuthorizationService（加载并校验快照）
  → CommandRouter/CommandBus
  → BleTransport queues/tasks
  → Event sink
  → BLE advertising/ready
```

启动失败必须有明确阶段原因；不得把授权存储失败伪造成激光故障，也不得在 Wave task 未就绪前接受启动。

## 13. 任务、队列和事件资源基线

现有任务和队列必须在阶段 A 记录实际配置；以下是施工时必须填写的合同，不允许只写“异步处理”。

| 任务/队列 | 当前角色 | 必填记录 | 满队/阻塞策略 |
|---|---|---|---|
| BLE callback | 捕获 BLE 事件 | 不分配大对象、不调用业务耗时函数 | 只入控制队列；失败记录并返回 |
| `BleCtrl` / controlQueue | 命令顺序处理 | 栈、优先级、长度、消息 sizeof、最大处理时长 | 控制命令不可静默丢失；满队返回明确 NACK |
| `BleTx` / control TX | ACK、NACK、关键事件 | 栈、优先级、长度、峰值水位 | 控制消息优先；失败有诊断 |
| `BleTx` / stream TX | 测量流 | 栈、队列长度、覆盖规则 | 允许按策略覆盖旧流帧，不影响控制帧 |
| `BleOta` / OTA queue | OTA | 栈、块大小、队列长度 | 与普通命令隔离，断连终止并清理 |
| `I2S_Audio` | DDS/Cue/I2S | 栈、优先级、DMA、写入阻塞时长 | 不等待授权、日志或 BLE 锁 |
| `LaserTask` | 采样与事实生成 | 栈、周期、传感器读耗时 | 采样失败按既有健康状态处理 |
| DiagnosticLog | 日志写入 | 栈、容量、写入延迟 | 队满丢低优先级日志，不阻塞业务 |

所有 `portMAX_DELAY`、互斥锁和同步 `EventBus::publish` 调用必须列出调用上下文和最坏等待时间。事件同步发布不得直接在实时任务中执行 BLE notify 或文件写入。

## 14. 命令与事件矩阵要求

阶段 A 必须生成并维护 golden matrix，至少覆盖以下命令族：

| 命令族 | 唯一 Owner | 成功结果 | 失败结果 | 兼容要求 |
|---|---|---|---|---|
| `CAP?`/身份/安全查询 | CapabilityService | 旧 ACK 字段 | 原 NACK 原因 | 字段顺序和预算不变 |
| `DEVICE_*` | ConfigService | 读回后 ACK | 配置/存储 NACK | 旧 APP 可用 |
| `WAVE:SET` | WaveCommandService | 参数 ACK | 参数错误 NACK | 不改参数语义 |
| `WAVE:START` | StartAdmission + FSM | `RUNNING`/ACK | 启动原因 NACK | 不绕过安全门 |
| `WAVE:STOP` | FSM | 受控停波/ACK | 仅报告实际失败 | 停波原因不混用 |
| `AUDIO:CUE` | CueService/Wave | 去重 ACK | 本地/忙/无效 NACK | session 规则不变 |
| 校准/空载/流 | Calibration/Stream Service | 读回 ACK | 原因码 NACK | 不改变测量时序 |
| 保护开关 | ProtectionService/FSM | 状态 ACK | 参数 NACK | 安全语义不扩张 |

事件矩阵必须列出：来源任务、事件身份、是否可丢、编码器、TX 队列、旧 APP 展示和恢复行为。协议 golden frame 以文件回放对比，不以“编译通过”代替。

## 15. LaserModule 拆分边界

`LaserModule` 当前是过渡门面；后续按以下顺序拆分，数据所有权不得重复：

1. `MeasurementRuntime`：原始采样、有效性、健康状态。
2. `PresenceStabilityPolicy`：在位、稳定、baseline、start-ready。
3. `CalibrationService`：模型、空载距离、校准捕获。
4. `LaserConfigStore`：设备/传感器配置和版本迁移。
5. `StreamPublisher`：采样流订阅、节流和 BLE 发布请求。
6. `RunSummaryCollector`：运行摘要和停止摘要。
7. `SafetyFactsSink`：向 FSM 提交 typed 事实，不暴露 FSM 内部字段。

拆分期间 `LaserModule` 继续作为兼容门面；每次只迁移一组职责，先由 focused tests 证明输入/输出一致，再删除门面内部旧实现。

## 16. 授权前置设计（重构完成后才实施）

### 16.1 业务边界

- 本固件授权只承载律动仪设备合约资格：全款、分期、结清、到期、撤销等。
- 音乐订阅是独立业务，不进入设备授权状态机。
- 每台设备单独保存和同步授权，不设置全局授权开关。

### 16.2 授权接口

```text
ProtocolBoundary
  → CommandRouter
  → AuthorizationTransferManager
  → AuthorizationService
      ├─ AuthorizationVerifier
      ├─ AuthorizationPolicy
      ├─ AuthorizationStore
      └─ DeviceIdentityProvider
  → CommercialEntitlementSnapshot
  → StartAdmissionPolicy
  → SystemStateMachine
```

推荐凭据字段：

```text
schema_version, device_id, grant_id, grant_version,
contract_mode, entitlement, valid_from, valid_until,
settled, revoked, issued_at, issuer, key_id,
algorithm, nonce, signature
```

设备只内置公钥/验证材料，服务器私钥不进入 APP 或 ESP32。授权 Store 必须独立命名空间、版本化、双槽/原子提交、读回校验和失败回滚。

### 16.3 旧授权原型不得直接合并的原因

- 原型的完整性校验不能替代签名真实性。
- 原型缺少有效期、撤销、签发者、算法和 key_id 等合同字段。
- 分片传输缺少完整的大小、超时、乱序、重连和 BLE session 约束。
- 原型与 `HubHandler`、FSM 商业启动开关耦合，不能作为最终 Owner。

授权传输必须单独由 `AuthorizationTransferManager` 管理：

- `BEGIN` 绑定 `transportSessionId/connectionId`、`transfer_id`、总长度、哈希和凭据版本。
- `CHUNK` 有固定最大长度、顺序/幂等规则和总大小上限，禁止无限 `String` 拼接。
- `COMMIT` 先验证长度、哈希、签名、设备 ID 和版本，再执行原子写入和读回校验。
- 支持 `ABORT`、超时清理、断连清理和旧 session 拒绝；半包不得落盘为有效授权。
- ACK 只有在持久化成功并读回一致后才能表示“授权生效”。

授权拒绝建议通过独立的 `StartAdmissionDecision` 返回“商业资格不足”，不新增安全 `FaultCode`，不进入 `FAULT_STOP`，不触发安全停波事件。

### 16.4 尚待产品决定的授权规则

- 无网络时设备可使用多久。
- 到期时正在训练的处理方式。
- 设备本地时间不可信时的策略。
- 分期结清、撤销和争议状态的生效时点。

这些规则未确认前，只能设计接口和测试桩，不能写入固件默认行为。

### 16.5 旧设备兼容与能力开关

- 重构阶段不得把默认授权拒绝直接接入现有 `requestStart()`，否则旧 APP/旧设备会被意外锁死。
- 授权能力必须通过版本化 capability/feature gate 显式启用；未启用授权能力的旧固件保持原有启动行为。
- `CAP/SNAPSHOT` 的旧字段语义和首包长度不能静默改变。
- 授权状态优先通过版本化能力查询或独立状态命令返回，不直接塞入固定长度的旧 `SNAPSHOT`，除非完成 MTU、字段兼容和旧 APP 忽略规则评审。
- 设备身份始终由 `FirmwareDeviceIdentityProvider` 提供，APP 传入的设备 ID 只作为匹配条件，不能成为真相源。

## 17. 证据与放行门禁

每一阶段必须分别提供：

| 证据层 | 证明内容 | 不能替代 |
|---|---|---|
| 源码审计 | Owner、依赖、状态和调用路径 | 真机行为 |
| 主机测试 | 纯策略、协议、存储边界 | BLE/I2S/传感器时序 |
| 编译产物 | 可链接、分区、资源预算 | 设备可用 |
| 目标板测试 | 任务、BLE、I2S、传感器、回滚 | APP 体验 |
| APP 联调 | 用户操作到设备结果 | 生产部署 |
| 真机验收 | 指定设备上的可见/可听结果 | 代码结构正确性 |

必须保留：固件哈希、构建环境、配置文件、串口原始日志、golden frame 回放、测试结果和失败回滚记录。

## 18. 文档同步事项

`docs/architecture_runtime.md` 当前有两处需要在后续文档施工中同步核实：

- 用户离位源码实际调用 `SystemStateMachine::onUserOff()`，不能写成 BLE 断连入口。
- BLE 断连入口现在由 `BleLifecycleCoordinator`，不是旧的 `HubHandler`。

历史说明、归档授权原型和候选蓝图不能覆盖当前源码事实；每次阶段收口时同步架构说明、施工蓝图和测试证据。

