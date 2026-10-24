# Demo APP 保护开关与 ESP32 `start_ready` 施工蓝图

- 状态：历史保护专项蓝图；保留演进记录，不再作为独立施工入口。当前统一合同以 [`esp32_demo_start_ready_independent_judgment施工蓝图.md`](./esp32_demo_start_ready_independent_judgment施工蓝图.md) 为准。
- 当前维护入口：[ESP32、Demo APP 与工厂交付维护说明](esp32_demo_release_maintenance.md)。2026-10-25 复盘只更新文档。
- 适用范围：`tools/android_demo/app-demo`、重构后的 ESP32 固件
- 最终目标：激光未设置或故障时，由 ESP32 自主降级，不要求用户先关闭保护；激光正常时，两个保护均关闭则不要求站稳，任意保护开启则必须站稳。APP 只消费 `start_ready`，故障弹窗只通知。SW APP 本轮冻结。
- 不包含：服务器业务、账号授权、固件 OTA、传感器型号和空载距离产品规则的重新设计

## 当前阅读规则（覆盖下方历史正文）

历史正文第 1～15 节记录了当时的判断、复核和后续补充，其中有已被用户纠正的规则。保留它们用于复盘，不能按旧段落重新施工。当前顺序如下：

1. 连接后读取设备已确认型号。选择型号只改变待设置值，ESP32 不变；已选择同型号时不能重复设置。
2. 点击“设置”，等待设备保存并 GET 确认。确认 Plus 后才显示 3 张卡片；确认 Base 后才隐藏。激光型号另行选择、设置、回读，之后才能设置空载距离。
3. 激光正常时两个保护开关分别可设置；激光异常/未设置时灰色不可点，不发送保护修改或降级授权。
4. 连接故障 Plus 或 Base 设置成故障 Plus 后显示一次“激光故障”通知；只有“知道了”。Plus 设置成 Base 不沿用旧故障，通知关闭不改变设备 ready。
5. ESP32 独立计算 `start_ready`，Demo APP 只渲染。保护 ACK、卡片选择、通知关闭都不能直接生成 ready；硬阻断、断连、配置未完成仍按固件合同处理。

| 下方历史说法 | 最终修正 |
|---|---|
| 第 1.1 节：选择 Plus 就显示 3 张卡片 | 必须设置并回读确认为 Plus 后显示 |
| 第 1.2、6.2 节：激光故障后先关闭两个保护才能降级 | 激光故障/未设置时两个开关不可点，由 ESP32 自主降级 |
| 第 1.4 节：保护命令失败就由 APP 判开始不可用 | 操作失败不能改写设备 ready；APP 消费正式 ready，并保留操作生命周期限制 |
| 第 12～13 节：“当前尚未施工/当前问题” | 这是施工前快照，不是现状；后续代码由 `3617b81` 等提交完成 |
| 第 14～15 节：新增目标与验收矩阵 | 是应验证的目标，不自动代表旧固件、所有传感器或当前 1.1.3 已通过 |

**当前边界：**历史用户测试和专项业务观测有 PASS，采集完整性 INCOMPLETE；本轮未 Run、构建或设备测试。当前源码仍有 SET 等待暂隐藏卡片、CYD 保护策略门禁和 legacy 全链路兼容待证三项，精确路径见[统一蓝图第 15 节](esp32_demo_start_ready_independent_judgment施工蓝图.md#15-2026-10-25-复盘状态与源码差异登记)。不能用历史通过掩盖这些差异，也不在本次文档复盘中追加修改业务代码。

## 历史正文（第 1～15 节）

以下保留原稿和补充的先后关系；“当前”“施工前”“最新”均指对应历史阶段。

## 1. 用户视角操作路线

### 1.1 正常 Plus 配置

1. 用户连接 ESP32。
2. APP 读取设备型号、传感器、保护开关状态和 `SNAPSHOT`。
3. 用户选择 Plus；此时只改变 APP 的待配置选择，不写 ESP32。
4. APP 显示激光型号、空载距离和保护开关卡片。
5. 用户选择激光型号；此时仍只改变待配置选择。
6. 用户点击设置，APP 通过 BLE 写入型号和传感器，并等待 ESP32 回读确认。
7. 当前传感器数据流恢复后，用户设置空载距离；APP 写入后再次 GET 回读。
8. 激光正常时，用户可分别设置两个保护开关。
9. ESP32 根据自身状态产生 `start_ready`；APP 按 `start_ready` 显示开始按钮。

### 1.2 Plus 降级开始

1. 用户选择 Plus 并完成必要的设备配置，或设备处于 Plus 无激光/激光不可用状态。
2. 用户分别关闭跌倒保护和离台保护。
3. APP 发送两个独立的保护关闭命令，并等待 ACK/状态回读。
4. ESP32 确认两个保护均为关闭后，立即计算并广播 `start_ready=true`。
5. Demo APP 和 SW APP 收到 `start_ready=true` 后，直接开放开始按钮。
6. 激光不可用的降级场景不等待站稳基线、不等待 `baseline_ready`、不要求激光有效距离。

### 1.3 激光正常时开始

1. ESP32 确认激光正常。
2. 只要跌倒保护或离台保护任一开启，`start_ready` 必须等待站稳基线。
3. 站稳确认后，ESP32 才广播 `start_ready=true`。
4. Demo APP 和 SW APP 的开始按钮变绿并可点击。
5. 保护全部关闭时，不走此等待站稳分支，转入 1.2 的直接就绪规则。

### 1.4 失败、恢复、重复操作

- 任一保护关闭命令失败：APP 保持开始按钮不可用；Demo APP 可显示调试原因，正式 SW APP 不显示原因，只保持按钮不可用。
- ESP32 重新发现硬故障、断连或保护重新开启：ESP32 广播 `start_ready=false` 及原因，APP 立即收回开始按钮。
- BLE 重连：APP 清空旧的就绪投影，重新读取 `CAP`/`SNAPSHOT`；未收到当前连接 epoch 的 `start_ready=true` 前不得开放开始。
- 重复点击保护开关：APP 按当前事务锁去重；迟到 ACK 不得覆盖新连接或新状态。

## 2. 目标链路

```text
用户关闭两个保护开关
→ Demo APP/SW APP 发送独立 BLE 命令
→ ESP32 ProtectionService/FSM 保存并确认保护状态
→ SystemStateMachine 计算 start_ready
→ BLE SNAPSHOT/EVT 广播 start_ready 与原因
→ APP 当前连接 epoch 的 Projection 更新
→ 开始按钮就绪
→ 用户点击开始
→ APP 发送 WAVE:SET + WAVE:START
→ ESP32 最终安全门禁再次检查并执行/拒绝
```

## 3. 相关链路

- 型号/传感器配置：`DEVICE:SET_CONFIG`、GET 回读、实时距离订阅；本轮不改变协议。
- 空载距离：`EMPTY:SET_CURRENT`/GET；本轮不改变协议。
- 保护命令：跌倒保护、离台保护的 ACK、能力回读和持久化。
- BLE 连接 epoch、CAP/SNAPSHOT 就绪、断连重置。
- ESP32 硬故障停机、离台、跌倒候选和恢复状态。
- Demo APP 的开始命令去重、开始中/停止中状态。

## 4. 真相源

| 事实 | 唯一真相源 |
|---|---|
| 保护开关是否已生效 | ESP32 保护模块的状态和回读 |
| 是否允许开始 | ESP32 `start_ready` |
| 是否需要站稳基线 | ESP32 当前产品合同；本场景 `start_ready=true` 时不作为开始前置条件 |
| APP 页面显示 | Demo APP / SW APP 的当前连接 epoch Projection；SW APP 只显示按钮状态，不显示阻止原因 |
| BLE 传输是否有效 | 当前连接 epoch 的 CAP/SNAPSHOT/事件连续性 |

APP 不得根据“没有激光”“保护开关看起来关闭”或本地缓存自行推断 `start_ready`。

## 5. Owner 与模块边界

### 5.1 ESP32（重构结构）

| 模块 | 责任 | 修改方向 |
|---|---|---|
| `ProtectionCommandService` | 接收两个保护开关命令、返回 ACK | 保留独立命令和事务语义；确认写入后触发状态刷新 |
| `SystemStateMachine` | 唯一安全启停 Owner | 收口 `start_ready` 计算；两个保护均关闭时，不要求 baseline_ready 即可进入就绪分支 |
| `RuntimeProtectionPolicy` | 保存保护降级/运行策略 | 明确定义“两个保护关闭”的策略输入，不复制 APP 规则 |
| `EventEncoder` / SNAPSHOT 编码 | 输出设备状态 | 保证 `start_ready`、`baseline_ready`、保护状态和原因同时可读 |
| `WaveCommandService` | 处理 `WAVE:START` | 只调用 FSM 最终门禁，不在命令服务中重复推断 |
| `DeviceConfigStore` | 持久化配置 | 仅在当前协议已支持时复用；不把 APP 临时选择写入这里 |

不新增第二个 `start_ready` Owner，不在 `LaserModule` 或 `DeviceConfigCommandService` 中复制开始门禁。

### 5.2 Demo APP

| 文件/模块 | 责任 | 修改方向 |
|---|---|---|
| `DemoViewModel` | 保护命令、连接 epoch、状态 Projection | 保护设置后重新读取/订阅当前 `SNAPSHOT`；拒绝旧 epoch 迟到结果 |
| `UiModels.kt` | 开始可用状态 | `waveStartAvailability()` 以 ESP32 `start_ready` 为主要开始门禁；不以 baseline_ready 或本地激光推断替代 |
| `FallStopProtectionSection.kt` | 两个保护开关显示 | 展示 ACK、回读、同步中和 ESP32 原因；不自行改变开始资格 |
| `WaveControlBottomBar.kt` | 开始按钮呈现 | 只消费统一 Projection，不复制一套降级判断 |
| `ProfileDeviceTransactions.kt` | 事务 epoch/状态绑定 | 复用现有事务锁，不新建重复事务 Owner |
| 测试 | 覆盖状态组合 | 增加 start_ready 与保护状态矩阵测试 |

正式 SW APP 需要复用同一协议语义；本蓝图本轮施工主体是 Demo APP，但固件字段和事件合同必须同时满足正式 APP。

## 6. 状态和时序合同

### 6.1 状态字段

必须区分：

- `fall_stop_enabled`
- `leave_protection_enabled`
- `start_ready`
- `baseline_ready`
- `runtime_ready`
- `start_block_reason`

`start_ready=true` 不得被编码成 `baseline_ready=true`，也不得通过修改基线字段伪造站稳。

### 6.2 就绪规则

在本次产品场景下，ESP32 至少按下列规则计算：

```text
激光不可用
且保护状态已确认：
  fall_stop_enabled = false
  leave_protection_enabled = false
且没有硬故障停机、连接/协议状态允许开始
→ start_ready = true

激光正常
且任一保护开启
且站稳基线已确认
且没有硬故障停机、连接/协议状态允许开始
→ start_ready = true
```

激光正常但站稳基线未确认时，`start_ready=false`。激光不可用但两个保护均关闭时，不等待站稳基线。设备进入硬故障、断连或配置事务未完成时，`start_ready=false`。

激光有效距离、空载距离、站稳基线不是“两个保护都关闭”场景的开始前置条件。激光保护正常路径仍按 ESP32 既有安全合同执行。

### 6.3 APP 投影规则

- 收到当前 epoch 的 `start_ready=true`：开始按钮变为可用。
- 仅收到保护 ACK、但未收到设备状态确认：不开放开始按钮。
- 收到 `start_ready=false`：立即禁用开始按钮；Demo APP 可显示中文调试原因，SW APP 不显示原因。
- APP 断连、重连、切换设备、设备配置写入期间：清空旧就绪状态。

## 7. 施工批次

### 批次 A：固件状态审计与协议收口

1. 核对 `SystemStateMachine` 当前 `start_ready` 计算是否仍依赖 `baseline_ready`。
2. 核对两个保护关闭后的状态事件、SNAPSHOT 和 ACK 是否包含足够字段。
3. 若缺字段，在现有 `EventEncoder`/snapshot contract 中补充，不新增平行协议。
4. 增加固件 focused tests：两个保护关闭立即就绪、保护重新开启收回就绪、硬故障覆盖就绪、重连清除旧状态。

### 批次 B：Demo APP 状态消费

1. 在 `UiState`/事件映射中明确保存当前 epoch 的 `start_ready` 和原因。
2. 收到保护 ACK 后触发当前设备状态确认，而不是直接把本地开关值当成就绪。
3. 收口 `UiModels.waveStartAvailability()`，删除本地 baseline/激光推断对该场景的替代判断。
4. `DemoViewModel.startWave()` 保留最终防线，只允许在当前 ESP32 `start_ready=true` 时发送开始命令。
5. `WaveControlBottomBar` 只显示统一 Projection。

### 批次 C：保护开关交互

1. 两个开关继续独立写入。
2. 同步中禁用重复点击。
3. 激光是否正常只影响相应的保护设置合同；不能由 APP 伪造 `start_ready`。
4. 显示“保护已关闭，设备已就绪”或 ESP32 返回的真实原因。

### 批次 D：回归验证与文档同步

1. 更新 `docs/demo_app_protocol.md`、`docs/start-readiness-contract.md` 中与本规则冲突的表述。
2. 保留 `baseline_ready` 作为独立事实，不把它改成 `start_ready` 的别名。
3. 完成代码、协议、测试、固件日志和 Demo APP Run 证据后，才进入分类 commit。

## 8. 验收矩阵

| 场景 | ESP32 事实 | APP 结果 |
|---|---|---|
| Plus，激光正常，两个保护开启 | 未站稳前 `start_ready=false` | 开始按钮不可用；站稳后变绿 |
| Plus，激光正常，只关闭一个保护 | 未站稳前 `start_ready=false` | 开始按钮不可用；站稳后变绿 |
| Plus，激光正常，两个保护均关闭 | `start_ready=true`，不要求基线 | 开始按钮立即可用 |
| Plus，激光不可用，两个保护均关闭 | `start_ready=true`，不要求基线 | 开始按钮立即可用 |
| 两个保护均关闭后重新开启一个 | `start_ready=false` | 开始按钮立即收回 |
| 保护均关闭但硬故障停机 | `start_ready=false` | 开始按钮不可用，显示故障原因 |
| 保护均关闭后 BLE 重连 | 未收到新 epoch 状态前不可用 | APP 不沿用旧就绪 |
| Base | 以 ESP32 正式 `start_ready` 为准 | 不由 APP 自行推断 |

## 9. 证据与验证

- 固件：状态机单元测试、协议帧测试、目标板串口日志。
- Demo APP：`waveStartAvailability` focused tests、保护 ACK/回读测试、epoch 迟到事件测试。
- 集成：ESP32 串口与 Demo APP BLE 日志按时间对齐，确认“关闭第二个保护 → ESP32 start_ready=true → APP 按钮就绪”。
- 用户验收：Android Studio Run 安装当前 `main` 的 Demo APP，按验收矩阵逐项操作；不以旧 APK、单独 ACK 或 APP 本地日志代替设备证据。

## 10. 冻结范围和风险

- 冻结型号、传感器、空载距离协议，不借本次需求重写。
- 冻结服务器，本流程不经过服务器。
- 不删除 `baseline_ready`，避免破坏校准、测量和历史证据。
- 主要风险是现有固件已部分实现 `start_ready`，但不同事件/SNAPSHOT 路径可能仍有旧的 baseline 或 degraded-start 语义；必须先通过源码和目标板日志确认，再决定最小改动。

## 11. 施工放行条件

只有同时满足以下条件才进入代码修改：

- 固件已确认 `start_ready` 的唯一 Owner 是 `SystemStateMachine`。
- 已确认两个保护关闭后的正式状态帧格式。
- 已确认 Demo APP 当前事件映射没有丢失 `start_ready` 或原因。
- 已确定 `baseline_ready` 不被修改为伪就绪。
- 已建立上表 focused tests 和目标板验收步骤。

## 12. 多轮复核结果（施工前必须处理）

### 12.1 已确认的实现断点

1. `src/core/RuntimeProtectionPolicy.cpp` 当前把“未配置激光”直接转换为 `effectiveStartReady=true`，没有把“跌倒保护关闭 + 离台保护关闭”作为输入。因此当前固件不能证明符合本蓝图的产品规则。
2. `src/modules/laser/StartGateContractEvaluator.cpp` 当前仍可要求 `baselineReadyLatched`。如果本场景继续走该评估器，两个保护关闭后仍可能被 `baseline_not_ready` 拦截。
3. `src/core/EventEncoder.cpp` 的连接快照目前输出 `leave_stop_enabled`，但没有同时输出 `fall_stop_enabled`、保护状态是否已确认和开始阻止原因。重连后 APP 无法仅凭当前快照完整复原两个保护状态。
4. `PlatformSnapshot` 有 `protectionDegraded`、`degradedStartAvailable` 等旧语义，但没有表达“两个保护关闭导致的直接就绪原因”。不能只改 APP 字段映射来掩盖固件真相缺口。
5. `docs/demo_app_protocol.md` 仍把 Plus 激光配置和 `degraded_start_available` 作为启动路径的重要条件；`docs/start-readiness-contract.md` 仍描述测量健康/持久条件。施工前必须逐条与本产品决定对齐，避免代码和文档各有一套合同。

### 12.2 复核后的最低风险边界

- 固件先改 `RuntimeProtectionPolicy` 的输入/决策模型，再由 `SystemStateMachine` 统一调用；不得在 APP 或 `WaveCommandService` 增加第二套规则。
- `StartGateContractEvaluator` 不直接删除基线能力；应增加明确的策略分支/输入，只有设备确认两个保护均关闭且没有硬故障时，才允许本场景绕过站稳基线。
- `PlatformSnapshot`、`SNAPSHOT` 和必要的状态事件增加最小正式字段：两个保护的当前值、保护状态确认、`start_ready` 原因。必须检查单帧预算；若超过连接通知预算，使用现有事件顺序和一次连接后的 `SNAPSHOT?`，不能静默截断。
- Demo APP 只消费当前连接 epoch 的 `start_ready`、原因和保护回读；不使用 `degraded_start_available` 代替最终就绪。
- 只有 ESP32 返回 `start_ready=true` 后，Demo APP 才开放开始按钮；APP 发送开始命令后仍由 ESP32 最终拒绝/允许。

### 12.3 快速漏洞清单

| 风险 | 当前证据 | 施工要求 |
|---|---|---|
| 旧连接的 `start_ready` 泄漏到新连接 | APP 有连接 epoch，但快照字段不完整 | 重连清空 ready；必须绑定 epoch 和设备身份 |
| ACK 到达但设备实际未就绪 | 当前保护 ACK 与 start gate 分离 | ACK 不能直接开放按钮，必须等 SNAPSHOT/正式状态 |
| 两个保护状态无法在重连后恢复 | 快照缺 `fall_stop_enabled` | 补字段或定义一次性状态查询 |
| 保护关闭但硬故障仍可开始 | 旧 bypass 可能直接置 ready | 硬故障、断连、配置事务优先级覆盖 ready |
| 文档合同冲突 | `demo_app_protocol`、`start-readiness-contract` 与本需求存在旧表述 | 施工批次 D 必须同步修订并保留历史差异 |
| 单帧超预算导致字段丢失 | `EventEncoder` 明确有 MTU 预算 | 增加字段后运行 golden frame/长度测试 |
| 保护开关重复写导致乱序 | APP 有同步状态，但两开关独立 | 每个开关事务绑定 epoch，迟到回包丢弃 |

### 12.4 资料复核依据

- 仓库协议和架构：`docs/start-readiness-contract.md`、`docs/demo_app_protocol.md`、`docs/ble-init-contract.md`、`docs/firmware_refactor_blueprint.md`、`docs/safety_design.md`。
- 当前实现：`src/core/RuntimeProtectionPolicy.*`、`src/core/SystemStateMachine.*`、`src/modules/laser/StartGateContractEvaluator.*`、`src/core/EventEncoder.*`、`src/core/PlatformSnapshot.h`、Demo APP 的 `UiModels.kt`、`DemoViewModel.kt`、`FallStopProtectionSection.kt`。
- 官方资料复核：ESP-IDF NVS 要求写入后显式 `nvs_commit()` 才能保证落盘；本次保护状态若持久化，必须保留提交成功与回读证据。[ESP-IDF NVS 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- 官方资料复核：BLE 事件通过任务/事件循环异步到达，不能用固定延时替代状态确认；实现应使用当前连接 epoch、事件顺序和回读确认。[ESP-IDF Event Loop 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_event.html)

### 12.5 补全后的施工前测试矩阵

#### 固件单元/协议测试

- 两个保护均开：`start_ready=false`，原因明确。
- 只关闭第一个保护：仍 `false`。
- 关闭第二个保护：立即 `true`，不读取或等待 baseline 才能置 true。
- 关闭状态持久化后重启：回读两个保护均关闭，再重新计算 ready。
- 任一保护重新开启：立即 `start_ready=false`。
- 保护均关闭但硬故障/FAULT_STOP：`false`，硬故障优先。
- BLE 断开：安全停机并清除当前运行就绪；重连需重新发完整状态。
- `SNAPSHOT`/事件 golden frame：字段完整、中文原因对应 code、长度不超过预算。
- `WAVE:START`：覆盖 ready=true 允许、ready=false 拒绝、迟到状态不绕过 FSM。

#### Demo APP focused tests

- 保护 ACK 但没有新 `SNAPSHOT`：按钮仍不可用。
- 当前 epoch 收到 `start_ready=true`：按钮可用。
- 新连接收到旧 epoch 的 ready：丢弃。
- `start_ready=false` 或硬故障事件：按钮立即禁用。
- 两个保护关闭状态回读完成后，APP 不等待 baseline 字段。
- 开始命令发送前再次检查当前 ready；发送中断连或状态失效时取消旧事务。
- Base、Plus 正常激光、Plus 无激光/故障、保护组合四组 UI/状态矩阵。

#### 集成/人工验收

- 串口日志、Demo APP 原始 BLE 日志、按钮可见状态按时间戳对齐。
- 验证“关闭第二个保护 → ESP32 `start_ready=true` → APP 按钮就绪”的完整链路。
- 验证“重新打开一个保护/硬故障/断连 → ESP32 `start_ready=false` → APP 收回按钮”。
- Android Studio Run 使用当前 `main` 源码安装，不以旧 APK、单独 ACK 或 APP 本地状态替代固件证据。

## 13. 复核后的放行结论

当前蓝图的产品方向正确，但原蓝图不能直接施工，原因是当前固件仍存在三处未闭环：

1. 降级启动策略没有以两个保护开关为正式输入。
2. 启动评估器仍可能要求站稳基线。
3. 连接快照没有完整携带两个保护状态和就绪原因。

同时，正式 SW APP 的显示合同已明确：SW 不显示阻止原因，只显示开始按钮是否可用；原因保留在 ESP32 状态证据和 Demo APP 调试面。

因此施工顺序必须是：

```text
固件 Owner/策略审计
→ 固件状态/协议补齐
→ 固件 focused tests
→ Demo APP 状态消费和开始门禁
→ Demo APP focused tests
→ ESP32 + Demo APP 联调证据
→ 文档同步和分类 commit
```

## 14. 最新产品规则补充：激光异常时锁定保护开关

本节覆盖此前蓝图中未明确的交互规则，优先级高于旧的“保护开关可操作”描述。

### 14.1 激光异常或未设置

当 ESP32 报告激光未设置或激光测量异常时：

- ESP32 自动进入降级启动路径。
- 两个保护开关在 Demo APP 中显示为灰色并不可点击。
- APP 不向 ESP32 发送保护开关修改命令。
- ESP32 将此状态作为降级开始条件，直接产生 `start_ready=true`。
- Demo APP 的开始按钮变绿并可点击。
- 不等待站稳基线，不等待有效激光距离，不要求空载距离。

“激光异常”是测量链异常，不是固件硬故障；不能被 `FAULT_STOP`、BLE 断连或配置未完成等硬阻断条件覆盖混淆。

### 14.2 激光正常

当 ESP32 报告激光正常时：

- 两个保护开关恢复可操作。
- 两个开关都关闭：ESP32 可直接给出 `start_ready=true`，不等待站稳。
- 任意一个开关开启：必须等待站稳基线后才给出 `start_ready=true`。
- 两个开关都开启：同样必须等待站稳基线。

### 14.3 Demo APP 的显示合同

| ESP32 状态 | 保护开关 | Demo APP |
|---|---|---|
| 激光异常/未设置 | 自动锁定 | 两个开关灰色不可点击；开始按钮可用 |
| 激光正常 | 两个都关闭 | 两个开关可操作；开始按钮不需要站稳即可用 |
| 激光正常 | 任意一个开启 | 两个开关可操作；未站稳前开始按钮不可用 |
| 激光正常 | 两个都开启 | 两个开关可操作；未站稳前开始按钮不可用 |
| 固件硬故障/断连 | 任意 | 开始按钮不可用；保护控件按连接状态禁用 |

正式 SW APP 不在本轮修改范围内；本轮只施工 Demo APP 和 ESP32 固件。Demo APP 可显示调试原因，但不得把原因显示合同扩展到 SW APP。

### 14.4 固件 Owner 修改要求

- `RuntimeProtectionPolicy` 必须同时接收激光状态、两个保护状态、站稳状态和硬阻断状态。
- 激光异常/未设置必须走明确的 `degraded_start` 分支，不得被误归类为固件硬故障。
- 激光正常且任一保护开启时，`start_ready` 必须继续由站稳合同产生。
- 激光正常且两个保护均关闭时，允许直接就绪。
- 保护开关命令在激光异常/未设置状态下必须被固件拒绝或返回不可用 ACK；Demo APP 仍需本地灰掉控件，形成双重防线。
- `start_ready` 仍由 `SystemStateMachine` 唯一拥有；`WaveCommandService` 只调用最终门禁。

### 14.5 Demo APP Owner 修改要求

- `FallStopProtectionSection` 的 `enabled` 条件必须包含当前已确认的 `deviceLaserAvailable == true`。
- `DemoViewModel.setFallStopProtectionEnabled` 与 `setLeaveProtectionEnabled` 必须再次检查激光状态，不能只依赖 UI 灰化。
- 收到激光异常/未设置状态时，立即清除或冻结两个开关的本地待修改状态，防止旧点击迟到写入。
- 开始按钮只消费 ESP32 当前连接 epoch 的 `start_ready`；不能用 `deviceDegradedStartAvailable` 或本地“激光故障”推断替代最终状态。

### 14.6 新增测试矩阵

- 激光未设置：两个保护开关灰色，点击无命令，`start_ready=true`，开始按钮可用。
- 激光异常：两个保护开关灰色，点击无命令，`start_ready=true`，开始按钮可用。
- 激光异常期间伪造保护写命令：固件拒绝，保护状态不改变。
- 激光恢复正常：两个保护开关恢复可操作；按照当前开关状态决定是否等待站稳。
- 激光正常、两个保护均关闭：未站稳也能收到 `start_ready=true`。
- 激光正常、只开启一个保护：未站稳 `start_ready=false`，站稳后变为 true。
- 激光正常、两个保护均开启：未站稳 `start_ready=false`，站稳后变为 true。
- 激光异常与固件硬故障同时出现：硬故障优先，`start_ready=false`。
- 激光异常期间断连/重连：重连前清空旧 ready；重新收到当前连接状态后再投影。

### 14.7 复核依据补充

- 当前代码已存在 `laserAvailable`、`degradedStartAvailable`、`degradedStartEnabled` 和 `start_ready` 字段，但现有策略还没有把“激光异常时锁定保护开关”作为完整的 APP/固件合同。
- ESP-IDF 官方 BLE 资料说明通知数据受当前 MTU 约束，状态字段增加必须运行长度/golden frame 检查，不能假设任意通知长度都能完整传输。[BLE 连接与 MTU 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ble/get-started/ble-connection.html)
- 保护状态持久化仍必须显式提交并回读确认；不能用 APP 本地灰色状态冒充 ESP32 已保存状态。[ESP-IDF NVS 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

## 15. 新增问题闭环：旧固件兼容与 Plus 设置后的即时降级

### 15.1 旧固件协议兼容边界

当前 Demo APP 使用的新配置事务是：

```text
DEVICE:SET_CONFIG platform_model=<BASE|PLUS>,sensor_profile=<...>
→ ACK:DEVICE_PROFILE_CONFIG op=SET
→ DEVICE:GET_CONFIG
→ ACK:DEVICE_PROFILE_CONFIG op=GET
```

旧固件只支持：

```text
DEVICE:SET_CONFIG platform_model=<BASE|PLUS>,laser_installed=<0|1>
→ ACK:DEVICE_CONFIG
```

旧固件没有 `sensor_profile`、配置 generation 和 `DEVICE:GET_CONFIG` 回读时，Demo APP 不得把“没有新格式回包”直接显示成新固件写入失败。

#### Demo APP 协议分支

- `CAP` 确认重构协议：使用 profile SET、GET 和 generation 一致性确认。
- `CAP` 确认旧协议：使用旧 Base/Plus 设置命令，只等待旧 `ACK:DEVICE_CONFIG`；不发送或不等待新 profile GET 回读。
- 协议未知或收到明确 NACK：显示真实协议不匹配，不静默当成旧固件。
- 旧协议只能确认 Base/Plus 和 `laser_installed`，不能假装确认激光型号或空载距离。
- Demo APP 当前配置页必须明确区分“旧固件只支持型号设置”和“重构固件支持型号+传感器设置”。

### 15.2 Plus 配置后的即时状态刷新

当前问题：Plus 配置回读成功后，ESP32 未立即把激光状态、测量异常、降级状态和 `start_ready` 作为当前连接的新状态发出；断连重连后重新初始化才恢复正确状态。

#### 固件施工要求

`DeviceConfigCommandService::handleSetProfileConfig` 成功后，必须通过现有 Owner 链触发：

1. 停止旧 Reader/旧测量任务和旧流帧。
2. 应用新的 `platform_model/sensor_profile`。
3. 重新初始化当前 Reader。
4. 清除上一 profile 的 `laser_available`、测量健康、距离就绪和 start readiness 快照。
5. 立即调用 `SystemStateMachine` 的统一状态同步入口。
6. 按最新策略计算 `laser_available`、`measurement_health`、`degraded_start_available`、`degraded_start_enabled`、`start_ready`。
7. 在当前 BLE 连接中发送新的 `SNAPSHOT`；若测量故障已确认，同时发送正式 `EVT:FAULT` 或 `EVT:SAFETY`/测量不可用事件。
8. 不得把上一 profile 的 `laser_available=true` 或 `start_ready` 沿用到新 profile。

不得通过断连、重连或重新连接提示来触发上述刷新；配置切换必须在同一 BLE 连接内完成。

#### Demo APP 施工要求

收到新 profile 的 SET/GET 回读确认后：

1. 立即清除旧 profile 的激光正常、距离就绪和降级投影。
2. 进入当前连接 epoch 的“等待新设备状态”阶段。
3. 收到新的 `SNAPSHOT laser_available=0` 时，立即投影激光异常。
4. 激光未设置或异常时，两个保护开关立即灰化、不可点击。
5. 收到 ESP32 的降级 `start_ready=true` 后，开始按钮立即可用。
6. 收到正式故障/安全事件时，显示 Demo APP 故障弹窗；不能要求用户断连重连。
7. 只有当前连接 epoch 的新状态可以结束等待；旧连接或旧 profile 的迟到状态必须丢弃。

### 15.3 故障弹窗与自动降级的分工

- `laser_available=0` 是测量链异常事实，必须进入 Demo APP 的故障投影。
- `start_ready=true` 是 ESP32 的开始真相，必须进入 Demo APP 的开始按钮投影。
- 故障弹窗和开始按钮是两个独立投影：显示故障不能阻止已经由 ESP32 确认的降级开始。
- Demo APP 不得要求 `EVT:FAULT` 到达后才开放降级；如果先收到合法 `SNAPSHOT start_ready=true`，按钮应先按设备真相更新，故障提示随后补充。
- 反过来，只有故障弹窗而没有 `start_ready=true`，不能自动开放开始按钮。

### 15.4 新增验收矩阵

| 操作 | ESP32 当前连接应输出 | Demo APP 应显示 |
|---|---|---|
| 连接旧固件并设置 Base/Plus | 旧 `ACK:DEVICE_CONFIG` | 设置成功，不出现新 profile 回读超时 |
| 连接旧固件选择激光型号 | 旧固件不支持 | 明确提示“当前固件不支持激光型号设置” |
| 连接重构固件设置 Plus | 新 SET/GET 回读 | 型号和传感器确认成功，保持连接 |
| 设置 Plus 后激光未设置 | 新 `SNAPSHOT laser_available=0`，降级 `start_ready=true` | 立即显示激光故障；保护开关灰色；开始按钮可用 |
| 设置 Plus 后激光异常 | 新故障/测量事件和 `start_ready=true` | 立即弹窗；自动降级；无需重连 |
| Plus 设置后激光恢复正常 | 新 `laser_available=1` | 保护开关恢复可操作，按保护状态决定是否等待站稳 |
| 配置切换中收到旧状态 | 旧 profile 状态 | 丢弃，不覆盖新 profile 投影 |
| 断连重连 | 新连接完整 CAP/SNAPSHOT | 清空旧状态后重新确认，不沿用旧 ready |

### 15.5 施工前新增阻断

施工前必须先确认：

- 重构固件是否已有 Reader 重初始化的正式 Owner；没有时只能在 `LaserModule`/配置服务的现有接口上补齐，不在 BLE 回调中直接重建 Reader。
- `SystemStateMachine` 是否有公开的统一状态同步入口；若没有，新增窄接口，不允许各服务分别计算 `start_ready`。
- 旧固件的 `CAP` 版本/能力字段是否足以可靠区分新旧协议；如果不足，使用明确 ACK 形态建立一次性兼容判定，不用超时猜测。
- Demo APP 的故障弹窗是否能由 `SNAPSHOT laser_available=0` 触发；不能只依赖 `Event.Fault`。
- 新旧协议的测试 fixture、回读超时测试和同连接切换测试必须在施工前补齐。

### 15.6 新增 focused tests

#### Demo APP

- 旧 ACK `ACK:DEVICE_CONFIG`：不进入新 profile GET 等待，不报回读超时。
- 新 ACK `ACK:DEVICE_PROFILE_CONFIG`：继续 SET/GET/generation 闭环。
- Plus SET/GET 成功后同一 epoch 收到 `laser_available=0`：立即弹窗、保护开关灰化、等待 `start_ready`。
- 同一 epoch 先收到 `start_ready=true` 后收到故障事件：按钮先可用，故障提示随后显示。
- 旧 epoch 的故障或 SNAPSHOT：不改变当前页面。
- 配置切换后无断连：状态必须自动刷新，不允许把重连作为恢复步骤。

#### ESP32

- 新 profile 设置后 Reader 重新初始化并清除旧测量状态。
- 激光未设置/异常时，当前连接立即收到最新 `SNAPSHOT`。
- 测量异常时同时验证故障事件和 `start_ready` 状态。
- 重构固件新协议与旧协议命令分别返回正确 ACK/NACK。
- 配置切换后旧 profile 的 stream、laser_available、start_ready 不得泄漏。
