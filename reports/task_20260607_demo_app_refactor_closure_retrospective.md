# Demo APP Refactor Closure Retrospective

状态：收官复盘
日期：2026-06-07
范围：`tools/android_demo/app-demo`

## 1. 本轮实际完成

本轮 Demo APP 重构从“能否自动重构”推进到阶段收官，完成了三类工作：

- 结构 owner：`MeasurementDisplayStore`、`RawConsoleStore`、`CalibrationSessionStore`、`MotionSamplingSessionStore`、`DeviceConfigWriteTracker`、`WaveControlStateReducer`、`WavePendingLifecycleStore`、`TestSessionBridge`。
- UI presentation / 信息架构：section actions DTO、固定顶部 `型号 / 校准 / 采样 / 运行 / 日志`、型号页主流程、校准页压缩、采样页压缩、运行页系统状态压缩、legacy UI cleanup。
- 证据链：focused JVM tests、Gradle compile/test/assemble、B6/B4 运行页轻量 smoke、完整 Demo APP quality baseline smoke、B11 轻量安装打开 smoke。

最终结论：

- 当前没有必须继续重构的 Demo APP blocker。
- Demo APP 可以继续作为联调和调试工具使用。
- 后续不应为了文件行数继续强拆。

## 2. 真经验

### 2.1 小 owner 包比全量拆 `DemoViewModel` 更稳

有效做法：

- 先抽纯状态 store / reducer，再保留 `DemoViewModel` 作为 orchestration owner。
- 每包只迁一个 owner，并补 focused tests。
- `client.send`、truth refresh job、BLE callback、notice、日志、exporter IO 不随便迁走。

原因：

- Demo APP 的复杂度不只在 UI 行数，而在设备事实、APP 乐观状态、capture 证据和用户操作之间的组合。
- 一次性拆 `DemoViewModel` 会同时动连接、控制、校准、采样、导出和日志，回归面过大。

后续规则：

- 只有当某段逻辑有明确 owner、可独立测试、迁移后不改变外部行为，才继续抽。
- 不因“文件大”直接开架构重写。

### 2.2 UI 精简要先改信息架构，再清死代码

有效做法：

- 先把默认入口改成 `型号 / 校准 / 采样 / 运行 / 日志`。
- 再按用户真实调试顺序压缩型号、校准、采样、运行页。
- 最后用调用点审计删除旧堆叠首页 section。

原因：

- 如果先删旧 UI，容易误删仍需要的调试能力。
- 如果只压文案而不重排入口，用户仍会觉得啰嗦。

后续规则：

- 当前正式入口是固定顶部 Tab、顶栏搜索 / 断开、底部运行控制。
- 旧 `DeviceConnectSection`、`ScaleSection`、`StableWeightSection`、`WaveSection` 不应重新接回主界面。

### 2.3 UI-only 和设备链改动要分开验证

有效做法：

- 设备控制链改动使用 capture：Start/Stop 必须看 APP、BLE、ESP32 串口和 `STOP_SUMMARY`。
- 信息架构和 UI-only cleanup 使用本地 Gradle 门禁 + 轻量安装打开 smoke。
- capture 脚本问题先修工具链，不扩大业务代码改动。

原因：

- 不是所有 UI 改动都需要完整设备 capture。
- 但触碰 BLE command、实时流订阅、校准录点、设备配置写入或 capture 语义时，只靠体感不够。

后续规则：

- UI-only 小包：`compileDebugKotlin`、`testDebugUnitTest`、`assembleDebug`、轻量安装打开 smoke。
- 控制 / 校准 / 实时流 / 配置写入：专项 capture / audit 后再收口。

### 2.4 串口采集默认用成熟工具更可靠

有效做法：

- Demo APP quality capture 的 ESP32 串口采集默认改用 PlatformIO monitor。
- macOS raw `stty` 采集乱码时，不把证据缺口误判为业务失败。

后续规则：

- 修改采集工具前先复用 PlatformIO / 项目既有脚本。
- 证据缺口优先归到采集链，不直接扩大 APP 或固件改动。

## 3. 稳定边界

后续默认冻结：

- ESP32 BLE wire payload。
- `STREAM:SET` 合同。
- 固件校准算法。
- MAX485 参数。
- `SonicWaveClient` / BLE transport。
- `client.send` command gateway。
- 完整 event reducer。
- 多 ViewModel / navigation 重写。

这些不是未完成工作；它们是需要真实 blocker 或明确专项请求才进入的高风险区。

## 4. 文档落点

已落长期约束：

- `AGENTS.md`
  - Demo APP 重构默认小 owner / 小 UI 包。
  - 当前已无必须继续重构 blocker。
  - UI-only 小包与设备链改动的验证边界。
  - 旧堆叠首页 section 不再接回主 UI。

已落长期真相源：

- `docs/system/esp32_firmware_optimization_priority_table.md`
  - FW-OPT-013 到 FW-OPT-024 状态。
  - Demo APP 后续重构启动原则。
  - 当前无必须重构结论。

已落阶段计划：

- `reports/task_20260606_demo_app_refactor_master_plan.md`
  - B2-B11 分包完成状态。
  - 收官结论、剩余观察项、冻结项、触发规则。

本报告只记录本轮复盘，不作为后续 backlog 真相源。

## 5. 未来触发表

| 现场情况 | 下一步 | 不要做 |
| --- | --- | --- |
| 用户问 Demo APP 是否还要继续重构 | 先读 `docs/system/esp32_firmware_optimization_priority_table.md`，默认回答无必须重构 | 不把冻结项说成未完成 |
| 用户反馈 UI 啰嗦、入口不直观、按钮位置不对 | 做信息架构 / presentation 小包，本地验证后轻量 smoke | 不碰 BLE / command / ViewModel business owner |
| 用户反馈 Start/Stop 异常 | 跑运行页专项 capture，确认 APP TX、ESP32 RX、`i2s_stop`、`STOP_SUMMARY` | 不凭体感直接改固件或状态机 |
| 用户反馈校准录点或实时距离异常 | 跑 telemetry / calibration capture，先看 `STREAM_SUBSCRIPTION_RESULT`、`MEASUREMENT_CONSUME_SUMMARY`、`CAL_CAPTURE_RESULT` | 不先改校准算法或 MAX485 参数 |
| 用户反馈型号写入不确定 | 先看设备回传真值、ACK、snapshot 和 write tracker 状态 | 不恢复旧连接状态卡片 |
| 删除 UI 或整理参数后需要验证 | compile/test/assemble + 轻量安装打开 smoke | 不重复完整 baseline capture |
| capture 证据缺口但用户体感正常 | 先修 capture wrapper / audit 或补证据说明 | 不扩大业务改动面 |

## 6. 以后可能需要做的事情

这些事项只作为未来触发项，不代表本轮重构未完成。

| 事项 | 触发条件 | 推荐入口 |
| --- | --- | --- |
| 型号页继续优化 | 用户仍看不清型号是否写入成功 | presentation 小包 + write tracker / snapshot 复核 |
| Pro / Ultra 型号开放 | 产品和固件合同正式支持 | 跨端合同审计 |
| 校准撤回 / 取消写入 | 固件提供可逆校准事务 | 校准合同专项 |
| 校准页 / 采样页局部拆分 | 大文件真实影响测试、复用或维护 | UI-only composable 小包 |
| Device config 写入专项 smoke | 现场允许写入或反馈不确定 | 专项 capture |
| 测试会话导出增强 | 用户需要更多导出字段或命名规则 | exporter 合同小包 |
| 完整 event reducer / command gateway / BLE connection owner | capture 指向真实 owner blocker | 高风险审计包 |
| Demo APP 发布 hardening | 进入外部试用或发布前验收 | release hardening 包 |

## 7. 协作方式结论

长周期重构继续采用“总计划 + 分包推进 + 阶段报告 + 总表收口”。

这轮有效的节奏：

- 先审计，再按推荐顺序自动推进。
- 每包只在需要真机测试或提交时停下来。
- 用户负责体感和真实设备动作，AI 负责本地验证、capture 复核和文档真相源。

后续同类任务应沿用这个模式，不回到碎片化逐点确认。
