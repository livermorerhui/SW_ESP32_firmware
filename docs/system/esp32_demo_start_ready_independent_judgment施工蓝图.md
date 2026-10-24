# ESP32 独立判断 start_ready 与 Demo APP 渲染施工蓝图

- 版本：2026-10-24；2026-10-25 复盘同步
- 状态：ESP32 与 Demo APP 代码已落主线；历史主机验证与用户操作有通过记录，专项业务观测 PASS，但采集完整性 INCOMPLETE；不能据此宣布当前 1.1.3 包或全部场景已验收。源码差异见第 15 节。
- 当前维护入口：[ESP32、Demo APP 与工厂交付维护说明](esp32_demo_release_maintenance.md)；本轮只修正文档，未重新构建、Run 或设备测试。
- 施工范围：重构后的 ESP32 固件、Demo APP 型号页面及其开始状态渲染
- 冻结范围：SW APP、服务器、账号授权、音乐、校准页、采样页、日志页和运行页的其他功能
- 核心目标：ESP32 独立判断 `start_ready`；Demo APP 只读取并渲染；任何 APP 都不能通过授权或按钮状态命令改写 ESP32 的就绪判断
- 重构约束：只在现有重构模块内收口，不新增第二套状态机、第二个 `start_ready` Owner 或 APP 侧业务判断

## 1. 先说用户最终看到的结果

```text
用户连接 ESP32
→ ESP32 自己判断当前能不能开始
→ ESP32 发送 start_ready=true/false
→ Demo APP 显示开始按钮可用/不可用
```

用户不需要在 APP 中确认“是否允许降级”。激光故障弹窗只负责通知，只有一个“知道了”按钮，不发送授权命令，也不改变开始按钮状态。

型号页面的用户路线：

```text
当前设备是 Base
→ 用户只选择 Plus：ESP32 仍是 Base，Plus 的 3 张卡片继续隐藏
→ 用户点击设置：ESP32 保存 Plus 并回读确认
→ 确认成功：Demo APP 才显示激光型号、空载距离、保护开关

当前设备是 Plus
→ 用户只选择 Base：ESP32 仍是 Plus，Plus 的 3 张卡片继续显示
→ 用户点击设置：ESP32 保存 Base 并回读确认
→ 确认成功：Demo APP 才隐藏 Plus 的 3 张卡片
```

## 2. 用户最终规则

### 2.1 SW APP

按用户确认的对接合同，SW APP 本轮冻结，不修改；以下不是本轮对其全部现实现的重新审计：

- 连接 ESP32 后读取 `start_ready`。
- `start_ready=true`：开始按钮可用。
- `start_ready=false`：开始按钮不可用。
- 不发送降级授权。
- 不修改 `start_ready`。

### 2.2 Demo APP

Demo APP 必须与 SW APP 使用同一条就绪规则：

- 读取 ESP32 的型号、激光状态和 `start_ready`。
- 型号卡片以 ESP32 已回读确认的型号为准。
- 激光未设置或故障时显示通知弹窗。
- 激光不可用时，保护开关显示灰色并不可点击。
- 开始按钮只使用 ESP32 的 `start_ready`。
- 不发送 `DEGRADED_START_SET`。
- 不根据本地 `laser_available`、`degraded_start_available` 或缓存值自行开放开始。

### 2.3 ESP32 固件

ESP32 是 `start_ready` 的唯一 Owner。ESP32 自己读取并判断：

- 当前已生效的型号和传感器配置。
- 激光是否设置。
- 激光是否可用。
- 保护开关状态。
- 是否需要站稳。
- 是否存在阻止启动的硬故障。
- 是否正在配置切换或配置未完成。
- 当前 BLE 连接是否有效。

ESP32 只对外发送状态，不接收任何 APP 命令来修改 `start_ready`。

## 3. 当前代码审计结论

### 3.1 真实 Owner

| 事实 | 当前 Owner | 审计结论 |
|---|---|---|
| 设备配置保存 | `LaserModule` / `DeviceConfigStore` | 保留 |
| 传感器读取 | `LaserMeasurementReader` / `LaserModule` | 保留 |
| 安全启停 | `SystemStateMachine` | 保留为唯一 Owner |
| 就绪策略 | `RuntimeProtectionPolicy` + `SystemStateMachine` | 已完成本包输入收口和旧授权隔离；目标板仍待验证 |
| BLE 状态编码 | `EventEncoder` | 保留，补齐稳定状态输出时序 |
| Demo APP 型号投影 | `DemoViewModel` + `MainScreen` | 只消费设备回读和 `SNAPSHOT` |
| Demo APP 开始按钮 | `UiModels.waveStartAvailability()` | 只消费 ESP32 `start_ready` |
| SW APP 开始按钮 | SW APP 现有实现 | 本轮冻结 |

### 3.2 施工前确认的旧逻辑残留

施工前固件曾存在以下链路（历史问题，已由 `3617b81` 修正）：

```text
DEGRADED_START_SET
→ CapabilityCommandService
→ SystemStateMachine::setDegradedStartAuthorized()
→ degraded_start_authorized
→ RuntimeProtectionPolicy::degradedStartEnabled
→ effectiveStartReady
```

这条链会让 APP 指令影响 ESP32 的有效就绪状态，与本次产品规则冲突。

施工前固件还把 Plus 是否设置传感器与“配置是否完成”绑在一起：

```text
Plus + SensorProfile.NONE
→ LaserModule::configOperational() = false
→ startConfigurationComplete() = false
→ SystemStateMachine 拒绝开始
```

这会把合法的“Plus 无激光降级状态”错误地当成配置错误。

本轮 ESP32 施工已处理以上两处：`DEGRADED_START_SET` 只返回当前兼容 ACK，Plus +
`SensorProfile.NONE` 被视为合法降级配置；最终是否可开始仍由固件策略和状态机计算。

### 3.3 必须持续防回归的竞态风险

配置切换过程中存在这些状态同时变化：

```text
configTransitionActive
旧 Reader
新 Reader
旧 laser_available
新 laser_available
旧 start_ready
新 start_ready
APP 的旧 Snapshot
APP 的配置 GET 回读
```

如果没有明确的切换 epoch 和最终状态屏障，可能出现：

- Base→Plus 后仍显示 Base 状态。
- Plus→Base 后仍弹激光故障。
- 配置已回读但 `start_ready` 仍是旧值。
- 断开重连后状态才看起来正确。

## 4. 目标状态模型

### 4.1 配置状态与运行状态分开

ESP32 必须区分：

```text
DeviceConfigState
├─ platform_model
├─ sensor_profile
├─ generation
├─ config_transition_active
└─ config_valid

LaserRuntimeState
├─ laser_configured
├─ laser_available
├─ measurement_fault_confirmed
└─ measurement_health

ReadinessState
├─ fall_stop_enabled
├─ leave_protection_enabled
├─ baseline_ready
├─ start_ready
└─ start_block_reason
```

`Plus + SensorProfile.NONE` 是合法的设备配置状态，但它没有正常激光测量能力；它必须进入 ESP32 的降级就绪判断，不得被当成 NVS/配置错误。

### 4.2 `start_ready` 目标规则

以下规则由 ESP32 内部计算：

```text
硬故障、BLE断连、配置切换中、配置无效
→ start_ready=false
```

```text
Base + 配置已确认 + 无硬故障
→ start_ready=true
```

```text
Plus + SensorProfile.NONE + 无硬故障
→ 自动降级
→ start_ready=true
```

```text
Plus + 激光已设置但激光故障 + 无硬故障
→ 自动降级
→ start_ready=true
```

```text
Plus + 激光正常 + 两个保护都关闭
→ 不要求站稳
→ start_ready=true
```

```text
Plus + 激光正常 + 任意保护开启
→ 需要站稳
→ 未站稳：false
→ 站稳：true
```

这里的“自动降级”是 ESP32 的内部策略结果，不是 APP 授权结果。

### 4.3 `baseline_ready` 与 `start_ready`

- `baseline_ready` 是站稳/基线事实。
- `start_ready` 是最终是否允许开始的设备事实。
- 激光正常且保护开启时，`baseline_ready` 参与 `start_ready` 判断。
- Plus 无激光或激光故障的自动降级路径，不等待 `baseline_ready`。
- APP 不把 `baseline_ready` 自己转换成 `start_ready`。

## 5. ESP32 施工方案

### 5.1 `RuntimeProtectionPolicy`

修改目标：

- 删除 `degraded_start_authorized` 对当前 `start_ready` 的影响。
- 增加明确的设备配置输入：当前型号、传感器是否已设置、激光是否正常、配置是否切换中。
- 把 `Plus + NONE` 和 `Plus + 激光故障`统一纳入自动降级分支。
- 增加“是否需要保护基线”输入：激光正常且两个保护开关都关闭时跳过基线；任意一个开启时仍走基线。
- 保留 `degraded_start_available/enabled` 作为兼容性或诊断字段时，不能让它们覆盖最终 `start_ready`。

禁止：

- 在 `WaveCommandService` 复制一套 ready 规则。
- 在 BLE 命令服务里直接写 `start_ready`。
- 把 `laser_available=false` 直接等同于硬故障。

### 5.2 `SystemStateMachine`

修改目标：

- 保持 `SystemStateMachine` 为唯一 `start_ready` Owner。
- 在配置切换期间强制 `start_ready=false`。
- 配置切换完成后调用一次统一状态同步入口。
- `requestStart()` 只检查统一计算结果、配置/断连暂停和硬故障，不读取 APP 授权状态。
- 断连清除运行资格，但不通过 APP 授权恢复；重连后重新计算。

### 5.3 `LaserModule`

修改目标：

- 将“Plus 配置是否有效”和“激光测量是否可用”分开。
- `Plus + SensorProfile.NONE` 可以完成设备画像保存和回读。
- Reader 未配置时，提供明确的 `laser_available=false` 和自动降级输入。
- 激光故障时清除旧的测量就绪事实，但不把设备配置标成损坏。
- 配置切换必须按顺序执行：

```text
锁定切换
→ 停止旧 Reader
→ 使旧样本失效
→ 保存新配置并提交
→ 读取并校验新配置
→ 配置新 Reader 或进入 NONE 状态
→ 重新计算运行状态
→ 清除切换锁
→ 统一计算 start_ready
→ 发布最终 Snapshot
```

### 5.4 旧命令处理

`DEGRADED_START_SET` 不能再改变 `start_ready`。

推荐处理：

- 保留旧命令的解析以兼容旧 APP。
- 返回当前设备状态或明确的兼容 ACK。
- 不写入 `degraded_start_authorized`，不改变 `start_ready`。
- 后续再单独清理无用授权字段和方法。

### 5.5 状态发布屏障

配置 SET/GET 完成后，ESP32 必须先完成内部状态稳定，再发布对外状态：

```text
SET成功
→ GET确认
→ Reader/测量状态稳定
→ 计算start_ready
→ 发布SNAPSHOT
```

旧配置的 Snapshot、旧 Reader 样本和旧 `start_ready` 不得覆盖新配置的最终状态。

## 6. Demo APP 施工方案

### 6.1 型号页唯一投影

型号页只使用两组值：

```text
selectedPlatformModel
```

表示用户临时选择；

```text
devicePlatformModel + deviceConfigVerified
```

表示 ESP32 已确认生效的型号。

三张 Plus 卡片只在以下条件成立时显示：

```text
devicePlatformModel == PLUS
且 deviceConfigVerified == true
```

### 6.2 型号设置事务

```text
用户选择目标型号
→ 只改 selected 状态
→ 用户点击设置
→ APP发送配置命令
→ ESP32保存并回读
→ APP确认GET
→ APP更新 devicePlatformModel
→ APP根据确认型号显示/隐藏三张卡片
```

设置中：

- 清除旧的 `start_ready` 投影。
- 不使用旧型号快照覆盖新事务。
- 不在 APP 内判断是否降级。
- 不发送 `DEGRADED_START_SET`。

### 6.3 激光故障通知

当当前已确认型号是 Plus，且 ESP32 返回 `laser_available=false`：

- 只显示“激光故障”通知弹窗。
- 弹窗只有“知道了”。
- 连接时若首个 Snapshot 早于型号 GET，APP 在型号 GET 确认后主动请求一次新 Snapshot；不会要求断开重连。
- 同一连接、同一故障状态下，用户点“知道了”后重复 Snapshot 不重复弹窗；恢复后再次进入故障才重新通知。
- 不发送授权命令。
- 不改变 `start_ready`。
- 两个保护开关渲染为灰色。
- 开始按钮继续只读取 ESP32 `start_ready`。

### 6.4 开始按钮

Demo APP 的开始按钮只消费：

```text
deviceStartReady == true
```

连接未建立、型号回读/配置切换尚未完成、参数输入非法或按钮自身处于发送/停止事务中，属于 APP 控件生命周期限制；这些限制不计算、不写回 ESP32 的 `start_ready`。

禁止使用以下字段自行开放开始：

```text
deviceDegradedStartAvailable
deviceDegradedStartEnabled
laser_available == false
本地缓存
弹窗确认结果
```

## 7. 施工范围

### 本轮修改

- ESP32：就绪策略、旧降级授权残留、Plus NONE 配置状态、配置切换时序、最终状态发布。
- Demo APP：型号页配置回读、卡片显示、故障通知、开始按钮状态投影。

### 本轮不修改

- SW APP。
- 其他 Demo APP 页面：校准、采样、运行、日志。
- 服务器、账号授权和合同逻辑。
- 音乐和会话逻辑。

## 8. 目标链路与相关链路

### 目标链路

```text
用户连接/设置型号
→ APP发送配置
→ ESP32保存并回读
→ ESP32稳定Reader和配置状态
→ ESP32独立计算start_ready
→ BLE Snapshot
→ Demo APP渲染型号卡片、故障通知和开始按钮
```

### 相关链路

- BLE 当前 connection epoch 和旧 Snapshot 丢弃。
- NVS 保存、提交和读回校验。
- Reader 停止、配置、重新启动。
- 保护开关状态读取和显示。
- `WAVE:START` 最终固件门禁。

### 真相源

- ESP32 型号和传感器：ESP32 配置 Store + GET 回读。
- ESP32 是否可以开始：ESP32 `SystemStateMachine` 的 `start_ready`。
- Demo APP 显示：当前连接 epoch 的 UI Projection。
- SW APP：既有 `start_ready` 渲染合同，本轮冻结。

### 不该动的层

- 不在 Demo APP 伪造 `start_ready`。
- 不在 SW APP补逻辑。
- 不在服务器增加降级授权。
- 不在 `WaveCommandService`复制状态规则。

## 9. 测试与审计矩阵

### 9.1 ESP32 单元测试

- Base 配置确认且无硬故障：`start_ready=true`。
- Plus + NONE：配置有效，自动降级，`start_ready=true`。
- Plus + 激光故障：自动降级，`start_ready=true`。
- Plus + 激光正常、两个保护关闭：不需要 baseline，`start_ready=true`。
- Plus + 激光正常、任意保护开启、未站稳：`false`。
- Plus + 激光正常、任意保护开启、已站稳：`true`。
- `DEGRADED_START_SET` 不改变 `start_ready`。
- 断连、硬故障、配置切换中：`false`。
- 硬故障优先于降级。
- 配置切换后旧 Reader、旧样本和旧 Snapshot 不得泄漏。

### 9.2 ESP32 持久化与切换测试

- Plus + NONE 重启后仍能读取合法配置。
- Plus + NONE 断电重启后仍进入降级判断。
- SET 后立即 GET，型号、传感器和 generation 一致。
- NVS 提交失败时不把目标配置冒充成功。
- Reader 配置失败时返回配置失败并保持 `start_ready=false`。
- 配置切换期间不发布可开始状态。

### 9.3 协议和时序测试

- `SNAPSHOT` 同时包含型号、激光状态和 `start_ready`。
- 状态字段不超过当前 BLE MTU/通知预算；超出时有明确分帧或查询顺序。
- 当前 connection epoch 的旧 Snapshot 被丢弃。
- `DEGRADED_START_SET` 旧命令不会改变状态。
- `WAVE:START` 只执行最终 ESP32 门禁。

### 9.4 Demo APP focused tests

- 冷启动未连接：型号设置不可用，Plus 三张卡片隐藏。
- 连接 Base 并 GET 确认：三张卡片隐藏。
- 连接 Plus 并 GET 确认：三张卡片显示。
- Base 只选择 Plus：卡片仍隐藏，ESP32状态不变。
- Plus 只选择 Base：卡片仍显示，ESP32状态不变。
- Base→Plus SET/GET成功：卡片显示。
- Base→Plus 后激光未设置：型号 GET 确认后立即出现一次“激光故障”通知，不需要断连重连。
- Plus→Base SET/GET成功：卡片隐藏。
- SET/GET未完成：旧型号不被临时 Snapshot覆盖。
- 激光异常：只显示一个通知弹窗，点击“知道了”不发送命令。
- `start_ready=false`：开始按钮不可用，即使本地认为是降级。
- `start_ready=true`：开始按钮可用，不需要 APP 授权。
- 旧 epoch 的 Snapshot 不改变当前型号页。

### 9.5 人工联调顺序

1. 空白/默认 Base 设备连接。
2. 选择 Plus，不点击设置：确认三张卡片仍隐藏。
3. 点击设置 Plus，确认 GET 后三张卡片显示。
4. Plus 不设置激光：确认 ESP32 状态、故障通知、保护开关和开始按钮按 `start_ready` 显示。
5. Plus 设置真实激光：确认激光正常后保护开关恢复可操作。
6. Plus 选择 Base，不点击设置：确认三张卡片仍显示。
7. 点击设置 Base，确认 GET 后三张卡片隐藏。
8. 重连 Base：不弹激光故障通知。
9. 不使用任何降级授权按钮完成以上流程。

## 10. 多轮资料复核

### 第一轮：仓库源码和协议

已复核：

- `SystemStateMachine`、`RuntimeProtectionPolicy`、`LaserModule`、`DeviceConfigCommandService`。
- `PlatformSnapshot`、`EventEncoder` 和 `SNAPSHOT` 字段。
- Demo APP `MainScreen`、`DeviceProfileSection`、`DemoViewModel`、`UiModels`。
- `docs/start-readiness-contract.md`、`docs/demo_app_protocol.md`、`docs/ble-init-contract.md`、`docs/firmware_refactor_blueprint.md`。

结论（施工前）：文档中曾有旧的 `PLUS + laser_installed=0` 和降级授权表述；本轮已同步修订，不再作为当前产品合同。

### 第二轮：运行架构与竞态

已复核：

- 配置 SET/GET 与 Reader 切换。
- connection epoch、Snapshot refresh gate 和旧状态清理。
- `startConfigurationComplete`、`configOperational` 和 `start_ready` 的耦合点。
- Demo APP 型号卡片基于设备确认型号的渲染边界。

结论（施工前）：需要先在 ESP32 拆开配置完成与激光测量可用，再收口 APP 渲染；本轮已完成 ESP32 侧拆分，并完成 Demo APP 型号页、故障通知和 `start_ready` 投影施工。

### 第三轮：官方 ESP-IDF 资料

- BLE 的 ATT MTU 限制单帧通知长度；新增状态字段必须做长度和分帧/查询顺序测试。[ESP-IDF BLE 连接与 MTU 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ble/get-started/ble-connection.html)
- ESP-IDF 事件循环用于异步事件的延迟处理和解耦；配置切换必须通过事件/状态顺序确认，不能靠固定延时判断完成。[ESP-IDF Event Loop 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_event.html)
- NVS 写入后必须显式提交，持久化状态要以提交成功和读回为依据。[ESP-IDF NVS 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

这些资料只提供通用工程约束，不能替代 SW 产品规则；本蓝图的型号、降级和 `start_ready` 规则以用户最终确认和重构固件 Owner 为准。

## 11. 施工前放行条件（本轮复核结果）

满足以下条件才进入代码施工：

- 已确认 `SystemStateMachine` 是唯一 `start_ready` Owner。
- 已确认 APP 不再发送或依赖降级授权。
- 已确定 `Plus + SensorProfile.NONE` 的合法降级配置表示。
- 已确定配置切换的开始、完成和最终 Snapshot 发布屏障。
- 已补齐旧授权命令兼容处理，不允许其改变 `start_ready`。
- 已建立上面的 ESP32、Demo APP、协议和人工联调测试矩阵。
- 已同步修订冲突的 readiness/protocol 文档。
- ESP32 本包已通过 `git diff --check`、固件构建和 evaluator tests；Demo APP unit tests、目标板和 Android Studio Run 证据单独记录。

## 12. 历史审计门禁结论（施工阶段快照）

本节保留施工时的证据状态；后续用户测试和采集已经发生，当前分层状态以第 15 节为准，不再把“目标板证据完全缺失”当作现状。

- `general gate`：`general-partial`。Owner 和事件/持久化通用边界已明确；目标板状态时序仍需现场验证。
- `SW project gate`：`sw-partial`。固件是设备状态真相源、Demo APP 只做 Projection、SW APP 冻结范围已明确；Demo APP 已删除授权发送路径，仍需现场回归确认。
- `evidence status`：`evidence-missing` for target-board. 源码和主机测试不能替代目标 ESP32 + Demo APP 的状态时序证据；本轮已有 Demo focused tests 和 Debug 编译证据。

本蓝图不授权修改 SW APP，也不把弹窗、按钮或 APP 本地状态升级为 ESP32 的就绪真相。

## 13. 按重构架构修正版

### 13.1 允许修改的现有模块

本需求只能在以下现有重构模块内施工：

| 现有模块 | 本次职责 | 允许修改 |
|---|---|---|
| `src/core/SystemStateMachine.*` | 唯一计算和执行最终 `start_ready`、最终 `WAVE:START` 门禁 | 增加统一输入快照和状态同步时序；不复制策略 |
| `src/core/RuntimeProtectionPolicy.*` | 纯策略判断 | 增加型号/传感器配置状态输入，删除旧授权对当前 ready 的影响 |
| `src/modules/laser/LaserModule.*` | 提供型号、传感器、Reader、测量健康和配置切换事实 | 拆开“配置有效”和“测量可用”；保留 Reader/存储 Owner |
| `src/services/DeviceConfigCommandService.*` | 处理 SET/GET 配置命令 | 只调用 `LaserModule` 配置接口；完成后请求统一状态同步，不直接写 `start_ready` |
| `src/core/EventEncoder.*` / `PlatformSnapshot.*` | 对外输出最终设备事实 | 输出稳定的 `start_ready` 和现有状态字段；不计算业务规则 |
| `src/services/CapabilityCommandService.*` | 能力查询和旧命令兼容 | `DEGRADED_START_SET` 只能兼容返回，不得改变 ready |
| `tools/android_demo/.../DemoViewModel.kt` | 连接、回读、状态 Projection | 只消费 ESP32 状态；删除授权 UI/命令路径 |
| `MainScreen.kt` / `DeviceProfileSection.kt` | 型号页渲染 | 只按已确认型号显示卡片，不拥有设备状态 |

### 13.2 禁止新增或移动的职责

- 不新增 `StartReadyManager`、`DegradedStartManager` 或其他平行状态机。
- 不让 `BleTransport`、`LegacyCommandAdapter`、`WaveCommandService` 计算 `start_ready`。
- 不让 `DeviceConfigCommandService`、`ProtectionCommandService` 直接修改 `start_ready`。
- 不让 `LaserModule` 直接调用 `WaveModule.start()` 或 `WaveModule.stop()`。
- 不让 Demo APP 从 `laser_available`、`degraded_start_available` 或本地缓存推断按钮可用。
- 不把 `baseline_ready` 改名或伪装成 `start_ready`。

### 13.3 重构后的正式状态链

```text
LaserModule
  → 提供 DeviceConfigFacts / LaserRuntimeFacts / ProtectionFacts
  → RuntimeProtectionPolicy 计算纯结果
  → SystemStateMachine 写入唯一 start_ready
  → PlatformSnapshotOwner 提供快照
  → EventEncoder 编码
  → BLE notify
  → Demo APP / SW APP 渲染
```

`WaveCommandService` 的职责只保留为：

```text
接收 WAVE:START
→ 调用 SystemStateMachine::requestStart()
→ 返回允许或拒绝结果
```

### 13.4 重构后的配置切换事务

配置切换必须由 `LaserModule` 的现有 transition mutex 和 `DeviceConfigStore` 完成，不能在 BLE 回调里临时重建 Reader：

```text
DeviceConfigCommandService 接收 SET
→ LaserModule 标记 configTransitionActive
→ 停止旧 Reader，废弃旧样本和旧流帧
→ DeviceConfigStore 保存并提交
→ 直接 GET 校验目标配置
→ LaserModule 配置目标 Reader 或 NONE 状态
→ 恢复配置事实
→ RuntimeProtectionPolicy 重新计算
→ SystemStateMachine 写入 start_ready
→ EventEncoder 发布最终 Snapshot
→ 返回 SET/GET 结果
```

在最终 Snapshot 发布前，`start_ready` 必须保持 `false`；旧 Snapshot 不能覆盖新配置。

### 13.5 重构后的 Plus + NONE 表达

不得通过把 `laserInstalled` 改成 `false` 来表达 Plus 未设置激光，因为 `laserInstalled` 是兼容字段，Plus 的型号语义仍然需要保留。

应在现有 `MeasurementFacts`/配置事实接口中增加窄的“传感器配置状态”表达，至少能区分：

```text
BASE + NONE
PLUS + NONE
PLUS + 已选传感器但测量异常
PLUS + 已选传感器且测量正常
```

同时保留三个独立门禁事实：

```text
configurationValid
measurementStartupResolved
protectedStartRequired = fall_stop_enabled || leave_stop_enabled
```

只有 `configurationValid=true` 才允许自动降级或“两个保护关闭时跳过基线”；
配置无效、切换中或存储错误仍必须保持 `start_ready=false`。

该事实由 `LaserModule` 提供，`RuntimeProtectionPolicy` 消费；APP 不复制这组判断。

### 13.6 重构后的授权命令处理

`DEGRADED_START_SET` 是历史兼容命令，不再是当前产品能力：

- `CommandDecoder` 可以继续识别旧命令。
- `CapabilityCommandService` 可以返回当前状态或兼容 ACK。
- 不得调用 `SystemStateMachine::setDegradedStartAuthorized()` 改变当前 `start_ready`。
- 新 Demo APP 不显示授权弹窗、不发送该命令。
- SW APP 不修改。

如果后续确认该命令无旧 APP 依赖，再单独删除命令、字段和测试；本施工包先保证它不再改变当前 ready。

### 13.7 重构施工批次（状态于 2026-10-25 同步）

#### R0：冻结与反例测试（ESP32 已完成）

- 固定 `start_ready` 唯一 Owner 为 `SystemStateMachine`。
- 增加 Plus + NONE、Plus 激光故障、Plus 正常保护组合的纯策略反例。
- 固定旧 `DEGRADED_START_SET` 不得改变 ready 的测试。

#### R1：固件事实接口收口（ESP32 已完成）

- 在现有 `MeasurementFacts` 增加最小配置状态事实。
- `LaserModule` 实现 Plus + NONE 与 Plus 故障的不同事实。
- 不移动 Reader、NVS、稳定和安全 Owner。

#### R2：固件策略和状态机收口（ESP32 已完成）

- `RuntimeProtectionPolicy` 使用新事实计算纯结果。
- `SystemStateMachine` 统一写入 `start_ready`。
- 清除旧授权对 ready 的影响。
- `requestStart` 只消费最终状态和硬故障。

#### R3：配置服务和状态发布（本包保留现有配置屏障）

- `DeviceConfigCommandService` 只编排 SET/GET 和统一状态同步。
- `EventEncoder` 发布最终快照。
- 本包未改 BLE 字段；现有 `configTransitionActive`、Reader epoch 和样本失效链继续作为屏障，目标板需验证旧快照不会覆盖新状态。

#### R4：Demo APP 型号页（代码已施工；源码差异见第 15 节）

- 保留 `selected` 与 `device confirmed` 两套值，但不混用。
- 卡片只按已确认型号显示。
- 删除降级授权弹窗和命令。
- 开始按钮只消费 `deviceStartReady`。

#### R5：验证和文档同步（历史主机验证及业务观测通过；采集完整性未通过）

- 固件 evaluator、PlatformIO 构建、协议 golden frames。
- Demo APP focused tests、Android Debug unit tests。
- 历史用户测试和业务观测已有通过记录；按场景/版本保留证据，采集完整性为 INCOMPLETE，不能推定当前 1.1.3 全矩阵通过。
- 同步 `start-readiness-contract.md`、`demo_app_protocol.md` 和本蓝图。

### 13.8 修正版放行条件

以下保留完整交付条件；ESP32 与 Demo APP 代码均已施工，实际证据及剩余差异见第 15 节，不能把所有条件统一标成已通过：

- `SystemStateMachine` 仍是唯一 `start_ready` Owner。
- `RuntimeProtectionPolicy` 是纯策略，不持有第二份运行状态。
- `LaserModule` 提供配置事实和测量事实，但不拥有最终启动动作。
- `DEGRADED_START_SET` 不再改变当前 `start_ready`。
- `Plus + NONE` 不再被当成配置错误。
- 配置切换期间旧状态不会覆盖新状态。
- Demo APP 不自行计算 ready，不发送授权命令。
- SW APP 无代码变化且协议行为保持兼容。
- 历史记录包含纯策略、PlatformIO 和 Demo APP 主机验证；目标板业务观测与采集完整性须分开确认，历史结果不自动覆盖当前发布包。

### 13.9 历史修正版审计结论（ESP32 包当时状态）

以下保留 ESP32 包完成、Demo 包尚未完成时的阶段记录。Demo 后续已施工，当前状态见第 15 节，不得重复据此报告 Demo“未施工”。

- `general gate`：`general-partial`。ESP32 状态事实接口、自动降级策略和旧授权隔离已通过主机验证；目标板仍待验证。
- `SW project gate`：`sw-partial`。SW APP 冻结；Demo APP 型号页尚未纳入本包，不能宣称跨端完成。
- `architecture status`：`architecture-partial`。本需求不能通过单个 APP 条件或单个策略 if 完成，必须按 R0–R5 在现有重构模块内收口。

## 14. 三轮复核后的补充门禁

### 14.1 “硬故障”定义

本蓝图中的“硬故障”不是激光测量异常。它指 ESP32 已经进入必须禁止输出的状态，例如：

- `SystemStateMachine` 的 `blocking_fault_code != NONE`。
- `TopState::FAULT_STOP` 或 `FAULT_LOCKED`。
- 配置/NVS 校验失败且设备不能确认当前有效配置。
- 设备进入既有安全停机恢复窗口。

以下状态属于“暂时不可开始”，但不能被写成激光硬故障：

- BLE 断连。
- 配置切换中。
- 新 Reader 尚未完成启动状态判定。

合法配置下，Plus 未设置激光或已确认激光测量异常进入 ESP32 自动降级策略，不因这两个条件本身阻止开始；其余正式运行门禁仍生效。固件故障锁、配置无效不能被降级绕过。

### 14.2 `degraded_start_*` 字段边界

本轮正式开始门禁只有：

```text
SNAPSHOT.start_ready
```

`degraded_start_available` 和 `degraded_start_enabled` 的处理规则：

- 可以继续保留在协议中，作为兼容和诊断字段。
- Demo APP 不得用它们替代 `start_ready`。
- SW APP 不得用它们替代 `start_ready`。
- `DEGRADED_START_SET` 不得通过它们间接改变 `start_ready`。
- 如果旧 APP 仍发送该命令，固件只能返回当前状态，不能改变当前就绪结果。

### 14.3 状态发布的一致性要求

同一份最终 `SNAPSHOT` 中，以下字段必须来自同一个配置 generation、同一个 Reader epoch 和同一个状态计算时点：

```text
platform_model
sensor_profile（如协议已支持）
laser_available
measurement_health
start_ready
baseline_ready
```

禁止出现以下组合被作为最终状态发布：

```text
新 platform_model + 旧 laser_available
新 sensor_profile + 旧 start_ready
旧 generation + 新 Reader 状态
```

如果 BLE 单帧预算不足，必须使用现有查询/分帧机制，并保证 APP 只在同一 generation 的完整状态收齐后更新型号页和开始按钮。[ESP-IDF BLE MTU 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ble/get-started/ble-connection.html)

### 14.4 配置切换的失效与恢复规则

- SET 发送后，旧配置可以继续作为“当前设备已确认状态”显示，但不得继续驱动新的开始资格。
- 一旦进入 `configTransitionActive`，`start_ready` 必须变为 `false`。
- SET 失败：恢复旧配置前，不得恢复旧 `start_ready`；必须重新 GET 验证旧配置和 Reader 状态。
- Reader 配置不支持或资源配置失败，导致正式配置无效时：不能冒充配置成功，`start_ready=false`，返回配置错误。该情况不同于合法配置后健康检测确认测量不可用；后者进入自主降级策略。
- GET 回读成功、Reader 状态稳定后，才允许发布新的 `start_ready`。
- 断连或连接 epoch 改变后，所有旧配置事务、旧 Snapshot 和旧 `start_ready` 都失效。

### 14.5 持久化边界

型号和传感器配置的成功条件必须是：

```text
NVS set
→ nvs_commit/Store commit成功
→ 直接读回
→ generation/型号/传感器一致
→ Reader状态与配置一致
```

只收到 SET ACK、只看到 APP 本地值或只看到旧 Snapshot，都不能作为配置成功。ESP-IDF 文档明确要求 NVS 修改后显式提交，关闭句柄不能替代提交。[ESP-IDF NVS 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

### 14.6 复核发现的文档同步债务（已处理）

以下正式文档曾保留旧语义，本轮已同步修订：

- `docs/start-readiness-contract.md` 已改为 Plus 无型号/激光不可用由固件自动判断。
- `docs/demo_app_protocol.md` 已明确 APP 只消费 `start_ready`。
- `docs/ble-init-contract.md` 已明确降级字段仅兼容/诊断，旧命令不改变 ready。
- `docs/firmware_refactor_blueprint.md` 已记录兼容命令不再调用授权状态机。

这些文档必须在功能 commit 前同步，避免施工后源码和合同再次分叉。

### 14.7 复核后的最终漏洞清单

| 漏洞 | 所在层 | 施工门禁 |
|---|---|---|
| Plus NONE 被当成配置错误 | `LaserModule` | 配置有效与测量可用分离 |
| 旧授权改变 ready | `CapabilityCommandService`/`SystemStateMachine` | 兼容解析但不得改变 ready |
| 激光故障未被确认就发布降级 ready | `LaserModule`/策略 | 明确 NONE 与故障事实输入 |
| 新旧 Snapshot 交错 | `BleTransport`/状态发布/APP epoch | generation + Reader epoch 屏障 |
| APP根据 degraded 字段自行开放开始 | Demo APP/SW APP | 只允许 `start_ready` 作为开始投影 |
| 配置 SET ACK冒充持久化成功 | Store/配置服务 | commit + GET + Reader 校验 |
| 文档仍要求旧授权 | docs/system | 代码施工前完成文档同步 |

### 14.8 历史施工放行结论（ESP32 包当时状态）

本节记录施工放行时点，不是当前交付状态；Demo 后续实现与分层验收见第 15 节。

经过本轮三次复核，ESP32 本包已处理以下施工门禁：

```text
Plus + NONE合法化
旧授权从ready中移除
保护开关关闭时跳过基线、任意开启时保留基线
文档合同同步
```

因此状态更新为：

- `blueprint-review`：通过
- `implementation-ready`：通过
- `code-change-authorized`：已进入并完成 ESP32 包
- `esp32-host-validation`：通过
- `demo-app-model-page`：未施工
- `target-board-evidence`：仍缺失


## 15. 2026-10-25 复盘状态与源码差异登记

### 15.1 已确认目标保持不变

- 选择不写设备；同型号不重复设置；点击设置并回读确认后，Base 隐藏、Plus 显示后续 3 张卡片。选择、提交中、已确认、运行状态已刷新是不同阶段。
- Plus 激光型号确认后才能设置空载距离；保护开关分别设置，激光异常或未设置时灰色不可点。
- ESP32 独立发布 `start_ready`；APP 只消费当前设备事实。故障弹窗只有“知道了”，不授权，不改 ready；SW APP 和服务器冻结。
- 连接故障 Plus 或 Base 设置成故障 Plus 后应通知；Plus 设置成 Base 不应通知；同一故障的重复帧不重复弹窗。
- 本节发现差异只登记事实与待补证据，不倒改用户目标，不授权追加业务代码修改。

### 15.2 真实代码与历史修正

源码审计基线：`main` 的 `b2ce08e`。关键变更：`58edbf2`、`0627da8`、`3617b81`、`325bbf8`。

| 历史问题 | 修正与当前证据 | 可作出的结论 |
|---|---|---|
| 选中 Plus 就显示卡片，选项冒充设备事实 | `0627da8` 曾在 `MainScreen.kt` 改用 `selectedPlatformModel`；`3617b81` 改为 `UiModels.kt` 的 `shouldShowPlusConfigurationCards()` | 本地选择不再直接改变卡片显隐；等待阶段仍有下表差异 |
| 型号切换依赖断连重连 | `58edbf2` 移除主动重连路径；`DemoViewModel.kt` 在配置 GET 后刷新 CAP/SNAPSHOT/STREAM；`ProfileDeviceTransactions.kt` 保存 epoch、generation、请求序号和刷新屏障 | 同连接刷新有正式实现；具体设备时序仍按对应日志和版本验收 |
| 通知、授权、开始状态耦合 | `3617b81` 删除 Demo 授权弹窗和 SDK 授权发送入口；`DemoViewModel.kt:2541` 的 `dismissLaserFaultDialog()` 只关闭通知；`UiModels.kt:452` 消费 `deviceStartReady` | APP 不再以激光故障确认授权开始；协议兼容定义仍可保留，不能说所有授权关键字均已删除 |
| 旧 true 或降级字段覆盖新的 false | `WaveControlStateReducer.kt` 的 `resolveSnapshotStartReady()` 保留正式 Snapshot 的 false；`DemoStartReadyRegressionTest.kt` 覆盖本地状态不能伪造 ready | 测试是源码回归证据，不代表全部手机/传感器实测 |

### 15.3 未被“通过”覆盖的源码差异

| 编号 | 用户目标/边界 | 当前源码事实（`b2ce08e`） | 后续验证要求 |
|---|---|---|---|
| D1 | Plus 切 Base 回读成功后才隐藏 3 张卡片 | `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt:2155` 在 SET 等待时把 `deviceConfigVerified=false`；`tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/UiModels.kt:481` 要求 verified 才显示。`tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/DemoStartReadyRegressionTest.kt:323` 也断言未 verified 时隐藏 | 当前会在等待阶段暂时隐藏；不能宣称写入等待期间卡片始终保留。若修正，须先拆清“已确认型号显示”与“写入/操作资格”，补成功、失败、超时和旧回包用例 |
| D2 | Plus 健康激光、任意保护开启时站稳后可开始 | `src/modules/laser/LaserModule.cpp:429` 的 `protectedStartPolicyAvailable()` 目前只接受 J100；`src/core/SystemStateMachine.cpp:171` 的 `effectiveStartReady()` 在激光可用但保护策略未验收且任意保护开启时返回 false | CYD 可采距离/空载距离，不代表其保护算法已验收。保持目标，记录 CYD 当前限制；禁止把 J100 证据扩展到 CYD 或未经验证解除门禁 |
| D3 | 旧固件可设置 Base/Plus，不假装支持新传感器/空载距离 | `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt:2042` 的 `sendProfileConfig()` 先要求 identity、generation、verified；`2081` 才分流 `proto < 2` | legacy 分支存在不证明旧固件能走到分支；需实际 CAP/ACK fixture 与完整设置路线证明，当前不宣布全兼容 |

### 15.4 验证证据分层

| 层次 | 已留存/已确认事实 | 不能扩大为 |
|---|---|---|
| 源码和主机验证 | 历史施工有 evaluator、协议及 Demo focused tests/构建记录；本次核对当前源码、测试入口和 Git 差异 | 本次重新跑过测试、真机全部通过 |
| 用户操作 | 用户确认新 Demo/固件简单测试通过、能输出正弦波；随后确认升级旧云端 1.1.2 成功并自动回连 | 所有型号、传感器、保护组合与未来 1.1.4 已验收 |
| 专项业务日志 | 已归档场景的业务观测为 PASS | 未操作场景或每个设备都通过 |
| 采集完整性 | 专项采集完整性为 INCOMPLETE | 双端原始日志完整、无丢失 |
| 当前正式 1.1.3 | 包身份、构建和导出证据单独记录，见维护入口 | 历史 1.0.0/测试版本的用户操作自动变成当前 1.1.3 真机验收 |

本轮文档复盘不重新启动采集、构建、刷机或安装 APP。证据目录、报告和发布身份以[当前维护入口](esp32_demo_release_maintenance.md)为索引；没有原始证据时明确保留缺口，不补写“全部通过”。

### 15.5 当前文档路线与复核依据

- 当前操作与维护：[维护说明](esp32_demo_release_maintenance.md)、[Demo README](../../tools/android_demo/README.md)、[开始就绪合同](../start-readiness-contract.md)。
- 旧[保护专项蓝图](demo_app_protection_start_ready施工蓝图.md)保留历史决定和反例，已明确由本蓝图与最终用户合同覆盖，不能恢复“激光故障先关两个开关”或“选中即显示卡片”的旧路线。
- [Android UI layer 官方指南](https://developer.android.com/topic/architecture/ui-layer)说明 UI 状态由正式状态持有者提供；[UI events 官方指南](https://developer.android.com/topic/architecture/ui-layer/events)区分 UI 通知动作和业务动作。资料支持职责划分，不替代本项目产品规则，也不能证明当前代码或真机已通过。
