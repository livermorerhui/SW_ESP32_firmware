# Demo APP Information Architecture Simplification

状态：第十阶段已完成，本地验证通过，待用户看效果后补一次 UI smoke
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-07

## 本轮判型

本轮属于整包开发，目标是降低 Demo APP 默认首页的信息密度，让调试者不用长距离滚动也能完成连接、看状态、看曲线和 Start/Stop。

目标链路：

- Demo APP 默认运行页。
- 校准、采样、设备配置和日志入口可达性。

相关链路：

- `CalibrationToolsSection`、`MotionSamplingSection`、`DeviceProfileSection`、`RawConsoleSection`。
- B9 `demo_app_quality_smoke` 真机证据链。

不该动的层：

- ESP32 固件。
- BLE wire payload 与 command timing。
- `DemoViewModel` business owner。
- stores / reducers 行为合同。
- 正式 SW APP。

## 改动点

- `MainScreen` 增加固定在顶部 app bar 下方的 `设备 / 校准 / 采样 / 运行 / 日志` 分区入口。
- 默认进入 `设备` 页，符合调试工具先设定设备型号、再校准、再采样、再运行、最后看日志的操作顺序。
- `设备` 页承载设备画像写入、摔倒保护开关和连接详情，不再重复系统状态。
- `校准` 页承载校准工具，不再重复交付边界说明。
- `采样` 页重新挂载 `MotionSamplingSection`。
- `运行` 页只承载系统主状态、交付边界或遥测曲线、测试会话，不再重复连接状态。
- `日志` 页承载 raw console，保持默认折叠。
- `DeviceConnectSection` 新增 compact 模式，默认隐藏 notify、protocol、capability、last ACK/Error 等工程字段。
- `SystemStatusSection` 新增 compact 模式，默认隐藏 runtime / wave / raw fault / source / safety code 等工程字段。
- `SCALE:ZERO` 和 `CAL:ZERO` 增加发送前确认弹窗。取消不会向设备发送；确认后才调用原 `DemoViewModel` 命令入口。

## 第三阶段改动

- 顶栏文案精简：`SonicWave 调试工具` 改为 `SW调试`，`搜索并连接设备` 改为 `搜索`，`断开连接` 改为 `断开`。
- `设备` tab 改为 `型号`，默认页仍固定在顶部 tab 下方。
- `型号` 页主卡片标题改为 `设置型号`，只保留型号选择和确认：
  - `Base`、`Plus` 可选。
  - `Pro`、`Ultra` 置灰不可选。
  - 选择 `Base` 显示 `无距离传感器`。
  - 选择 `Plus` 显示 `有距离传感器`。
  - 按钮文案改为 `确认`。
- 型号页原本的配置真值、运行时真值、一致性规则和写入状态，统一收进 `查看详情 / 收起详情`。
- 型号页删除连接状态卡片；连接入口保留在固定顶栏。
- 保护卡片默认只保留两个开关：`摔倒保护` 和 `律动离开`。
- 保护卡片的状态、说明和能力支持情况统一收进详情抽屉。
- `律动离开` 不是纯 UI 假开关：Demo APP 已按固件既有合同接入：
  - `Command.LeaveProtectionSet`
  - `SAFETY:LEAVE_PROTECTION enabled=<0|1>`
  - `ACK:LEAVE_PROTECTION enabled=<0|1> supported=<0|1> effect=<...>`
  - `SNAPSHOT leave_stop_enabled=<0|1>`
- 更新 `tools/demo_app_quality_smoke_capture.sh` 和 `tools/demo_app_quality_smoke_audit.py`，后续 smoke 会按 `型号` 页、双保护开关和 `LEAVE_PROTECTION` 证据复核。

## 第四阶段改动

- 校准页主流程从 7 个常驻说明步骤压缩为 4 个移动端竖向操作块：
  - `开始`
  - `记录`
  - `采集数据 / 点与拟合曲线`
  - `确认模型`
- `开始校准` 只进入本地校准状态并允许录点，不向 ESP32 写入归零。
- `设备归零` 保留为次级按钮，仍走既有发送前确认弹窗；取消不发送，确认后才执行原 `SCALE:ZERO` 入口。
- 记录区只保留参考重量、实时距离和记录按钮，录点条件与语义说明改为信息弹窗，不再常驻占屏。
- 数据区表头从 `预测` 改为 `当前预测`，并用信息弹窗说明它是录点当时已有 APP / 设备模型的预测值，不是新拟合模型预测。
- 曲线区单独展示点与拟合曲线；模型指标、线性 / 二次选择和写入按钮统一下移到 `确认模型`。
- 高级工程区继续保留 `GET MODEL`、`CAL:ZERO` 和手动 ref/c0/c1/c2；旧 Z/K 路径后续在第七阶段从校准页移除。
- 新增圆圈信息弹窗承载少量必要说明，避免页面长期堆解释文案。

## 第五阶段改动

- 删除校准页内重复标题 `校准工具`。用户已经在 `校准` tab 内，页面内部不再重复说明当前位置。
- 开始区压缩为一行两个主按钮：`开始校准` / `设备归零`。
- 删除常驻的 `未开始 / 校准中` 说明、录制状态文本、录制文件路径和 `何时使用设备归零？` 文本按钮。
- `结束校准` 只在校准中显示，不再占用未开始状态的页面空间。
- 记录区把 `实时距离` 和 `记录` 放到同一行，删除常驻录点摘要。
- 信息按钮改为小号圆圈图标，不再使用大号文本按钮。
- 确认模型区删除重复的模型指标卡；主流程只保留 `线性`、`二次`、待写入摘要和 `写入模型`。
- 线性 / 二次选择卡片改为短状态：`点数不足 / 可选 / 已选 / 待写入`。
- `开始校准` 信息弹窗同时说明设备归零适用场景，避免页面新增常驻解释。

## 第六阶段改动

- `结束校准` 从文本入口改为 `OutlinedButton`，保持按钮形态但不抢占主操作视觉层级。
- 新增 `清空校准点` 按钮，清空 APP 本地已记录点集、拟合结果和待写入模型；不发送 ESP32 命令，不撤回已写入设备的模型。
- `参考重量` 和 `实时距离` 改为水平并排；实时距离使用只读数值块表达，避免和输入框混淆。
- `记录` 和 `清空校准点` 改为同一行操作，减少纵向滚动。
- `确认模型` 的 `线性 / 二次` 选项新增拟合状态：`点数不足 / 拟合通过 / 拟合未通过`。
- 拟合状态直接读取当前 `CalibrationComparisonResult` 的 `isAvailable + monotonic`，不新增 store 字段，不改变拟合或写入策略。

## 第七阶段改动

- 高级工程区删除常驻说明文本，只保留实际调试控件。
- 删除旧 Z/K 校准路径入口：`零点 Z`、`系数 K`、`校准 / CALIBRATE` 不再显示在校准页。
- `CalibrationToolsActions` 不再暴露旧 Z/K 输入和 `CALIBRATE` 回调，避免 UI 参数保留已删除入口。
- 保留仍有调试价值的高级入口：
  - `获取模型`
  - `CAL:ZERO`
  - 手动 `ref / c0 / c1 / c2`
  - `详细测量日志`
  - 当前模型回读摘要

## 第八阶段改动

- `采样` 页删除常驻的工程 MVP 说明、移动平均研究说明、采样模式状态长文案、测量有效性 / runtime context 常驻说明。
- `采样` 页实时摘要压缩为一行关键值：距离、体重、稳定体重。
- `采样` 页会话主视图只保留行数、频率和强度；会话 ID、开始/结束时间、采样标志、导出标签和导出路径统一收进 `会话详情`。
- `采样` 页移动平均窗口和 preset 控制默认收进 `曲线设置`，曲线本身仍直接可见。
- `采样` 页删除最近 8 行 raw 预览和 schema 提示，避免把导出数据结构暴露为主页面说明。
- `运行` 页删除旧 `当前交付边界` 卡片；运行页不再因为 Base / Plus degraded profile 隐藏曲线和测试会话。
- `运行` 页曲线说明文本和 MA 来源说明不再常驻，曲线叠加开关默认收进 `曲线设置`。
- `运行` 页测试会话删除长说明；导出路径默认收进 `导出详情`。
- `运行` 页 compact 系统状态在无故障时不再显示 `故障参考：无`。

## 第九阶段改动

- `运行` 页 compact 系统状态卡片缩小 padding、间距、圆角和字体层级，保留原字段不增删。
- compact 系统状态里的 `状态 / 安全原因 / 影响 / 工程参考` 继续显示原内容，但从大号状态面板调整为紧凑状态摘要。
- compact 系统状态展开入口从大号按钮调整为轻量 `查看详情 / 收起详情` 文本按钮。
- `采样` 页主流程不再把 `开启采样模式 / 关闭采样模式` 作为第一行主按钮；主流程只保留 `开始采样 / 停止采样 / 清空会话 / 导出会话`。
- `DEBUG:MOTION_SAMPLING` 设备侧开关仍保留，但收进默认折叠的 `采样设置`。
- 关系结论：`开始采样` 是 Demo APP 本地采样会话开关，已经能记录 `EVT:STREAM` 行数据；`开启采样模式` 是向 ESP32 写入 `DEBUG:MOTION_SAMPLING enabled=1` 的设备侧调试模式，不是开始采样的必要条件。

## 第十阶段改动

- `型号` 页主卡片新增 `当前设备：...`，直接显示设备回传的型号真值。
- 当前设备显示源为 `UiState.devicePlatformModel` + `UiState.deviceLaserInstalled`，不是用户刚选择的本地草稿。
- 当前设备已知时显示为 `Base（无距离传感器）` 或 `Plus（有距离传感器）`；未知时显示 `N/A`。
- `deviceConfigStatus` 从详情抽屉移到主卡片条件显示，点击确认后可直接看到 `确认中 / 写入成功 / 超时 / 失败` 等状态。
- 详情抽屉仍保留原始 `MODEL / LASER` 和运行时健康真值，方便工程复核。

## 抽象点

- 分区状态只属于 UI presentation，不进入 `DemoViewModel`。
- 归零确认弹窗属于 UI element state，不进入 `DemoViewModel`；`DemoViewModel` 仍是实际命令发送 owner。
- 校准页信息弹窗属于 `CalibrationToolsSection` 本地 UI element state；录点、拟合、待写入模型和写设备反馈继续由 `CalibrationSessionStore` / `DemoViewModel` 负责。
- 第五阶段只压缩 presentation 密度，不新增 store 字段，不改变 `CalibrationModelOptionUi` 合同。
- 第六阶段新增清空点集 action，但实现复用既有 `CalibrationSessionStore.reset()`；UI action 只负责触发，ViewModel 仍是状态 owner。
- 第七阶段只删除校准页 presentation 入口，不删除 ViewModel / protocol 旧命令实现；如果未来需要彻底移除旧 SCALE:CAL 合同，应另开协议兼容审计。
- 第八阶段只调整 Compose presentation 密度；`MotionSamplingSessionStore`、`MeasurementDisplayStore`、test session owner、导出 CSV/JSON 格式、BLE command 和 ESP32 固件合同均不变。
- 第九阶段只调整 compact UI 参数和主按钮层级；采样会话 owner、`DEBUG:MOTION_SAMPLING` 命令、ESP32 固件行为和导出 metadata 中的 `samplingModeEnabled` 字段均不变。
- 第十阶段只新增型号页 presentation 反馈；设备型号真相源仍是现有 `ACK:DEVICE_CONFIG` / `SNAPSHOT` 消费后写入的 `UiState.devicePlatformModel` 和 `deviceLaserInstalled`，不新增本地伪确认。
- section action 继续通过既有 `CalibrationToolsActions` / `MotionSamplingActions` 分发。
- compact 模式只调整展示密度，不改变 `UiState` 字段含义。
- 律动离开能力按 protocol / sdk / ViewModel / UI 分层接入；`ACK:CAP` 的 `leave_stop_supported` 只表示能力支持，当前开关真值优先来自 `SNAPSHOT leave_stop_enabled` 或写入 ACK。
- `DeviceProfileSection` 不再暴露独立 `laser_installed` 选择，Base / Plus 通过既有 ViewModel 规则参数化映射到距离传感器有无。

## 参数点

- 新增 tab 文案：`tab_run`、`tab_calibration`、`tab_sampling`、`tab_device`、`tab_logs`。
- 新增确认文案：`confirm_scale_zero_*`、`confirm_cal_zero_*`、`action_write_to_device`。
- 新增校准页精简文案：`label_calibration_flow_*`、`action_start_calibration`、`action_device_zero`、`action_info_icon`、`calibration_info_*`。
- 新增模型短状态文案：`label_model_option_*_short`、`label_prepared_model_compact`。
- 新增清空和拟合状态文案：`action_clear_calibration_points`、`label_fit_status_*`、`label_live_distance_short`。
- `DeviceConnectSection(compact = true)` 用于设备页连接详情。
- `SystemStatusSection(compact = true)` 用于运行页。
- `tab_device` 文案现在显示为 `型号`。
- 新增 `device_profile_sensor_*`、`section_protection_switches`、`label_leave_protection_*`、`value_leave_protection_*`、`action_enable_leave_protection`、`action_disable_leave_protection`。
- 新增协议参数：`SAFETY:LEAVE_PROTECTION enabled=1|0`。
- 第八阶段新增 UI 折叠文案：`action_show_chart_settings`、`action_hide_chart_settings`、`action_show_session_details`、`action_hide_session_details`、`action_show_export_details`、`action_hide_export_details`。
- 第八阶段新增采样页紧凑摘要文案：`label_motion_sampling_live_compact`、`label_motion_sampling_no_session_compact`、`label_motion_sampling_session_compact`。
- 第九阶段新增采样设置折叠文案：`action_show_sampling_settings`、`action_hide_sampling_settings`。
- 第十阶段新增型号真值展示文案：`device_profile_current_device`、`device_profile_current_device_value`。

## 验证结果

本地门禁：

```bash
git diff --check
cd tools/android_demo
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
./gradlew :app-demo:assembleDebug --no-daemon --stacktrace
./gradlew :app-demo:compileDebugKotlin --no-daemon --stacktrace
```

结果：

- `git diff --check`：通过。
- `:sonicwave-protocol:test :app-demo:testDebugUnitTest`：通过。
- `:app-demo:assembleDebug`：通过。
- 用户反馈后的第二阶段：`:app-demo:compileDebugKotlin` 通过，`:sonicwave-protocol:test :app-demo:testDebugUnitTest` 通过。
- 第三阶段：
  - `git diff --check`：通过。
  - `:sonicwave-protocol:test`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第四阶段：
  - `git diff --check`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第五阶段：
  - `git diff --check`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第六阶段：
  - `git diff --check`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第七阶段：
  - `git diff --check`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第八阶段：
  - `git diff --check`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第九阶段：
  - `git diff --check`：通过。
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 第十阶段：
  - `:app-demo:compileDebugKotlin`：通过。
  - `:app-demo:testDebugUnitTest`：通过。
  - `:app-demo:assembleDebug`：通过。
- 观察项：Gradle 仍提示既有 JDK 路径 `/opt/homebrew/Cellar/openjdk@17/17.0.18/...` 不存在；`RawConsoleSection` 仍有既有 `LocalClipboardManager` deprecated warning。本包未处理这两个非阻断项。

真机门禁：

- 使用 `tools/demo_app_quality_smoke_capture.sh`。
- Capture：`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260607_101316__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-quality-smoke__manual__ESP32_RUNTIME__demo_app_quality_smoke`。
- 专项 audit：`PASS_CANDIDATE`。
- 覆盖结果：连接后实时距离 / 重量 / 曲线可见；Start -> Stop 成功；校准工具可操作；motion sampling 启停正常；日志入口可达。
- Android 证据：`SonicWaveTransport=2005`，`SNAPSHOT=1091`，`EVT:STREAM=368`，`visual_evidence_files=9`。
- ESP32 证据：`WAVE:START / START ALLOW`、`WAVE:STOP / STOP REQUEST / i2s_stop`、`STOP_SUMMARY result=NORMAL stop_reason=MANUAL_STOP`、`MOTION_SAMPLE_MODE enabled=true`。
- 非阻断观察项：device config 写入未覆盖，按现场安全条件可选；Android focus logcat 在 stop 时 stale，但已有 transport、SNAPSHOT/STREAM、ESP32 串口和 visual evidence 旁证，不影响本轮功能 smoke 结论。

第三阶段后建议补一次轻量 UI smoke：

- 确认顶部 Tab 上下滚动时固定不消失。
- 确认默认页为 `型号`。
- 确认 `型号 / 校准 / 采样 / 运行 / 日志` 各自内容不重复。
- 确认 `型号` 页只把设置型号和保护双开关作为主操作；连接状态卡片不再出现。
- 确认 Base / Plus 的距离传感器提示正确，Pro / Ultra 置灰不可选。
- 确认 `摔倒保护` 与 `律动离开` 两个开关可达，详情默认收起。
- 点击 `归零` 或 `CAL:ZERO` 时先出现确认弹窗；点取消不发送，点写入才发送。

第四阶段后建议补充：

- 确认校准页默认不再出现 7 步长说明。
- 确认 `开始校准` 后可录点，但不会弹出设备归零确认，也不会发送归零命令。
- 确认 `设备归零` 仍会先弹出确认，取消不发送。
- 确认参考重量、实时距离、记录按钮、点表、曲线、线性 / 二次和写入按钮在手机上按竖向顺序可达。
- 确认圆圈信息弹窗能解释设备归零、录点、当前预测和写入边界，且不会增加主页面滚动负担。

第五阶段后建议补充：

- 确认进入 `校准` tab 后不再看到重复的 `校准工具` 标题。
- 确认首屏能更快看到 `开始校准 / 设备归零 / 参考重量 / 实时距离 / 记录`。
- 确认模型区不再出现两套重复卡片，只剩线性、二次、待写入摘要和写入按钮。
- 确认信息图标足够可发现，但不会像说明文本一样占屏。

第六阶段后建议补充：

- 确认 `结束校准` 是按钮形态。
- 确认 `清空校准点` 只在已有点时可点，点击后点表、曲线、待写入摘要会清空。
- 确认 `参考重量` 和 `实时距离` 在同一行展示。
- 确认线性 / 二次选项能显示 `点数不足 / 拟合通过 / 拟合未通过`。

第七阶段后建议补充：

- 确认高级工程区展开后不再出现旧 Z/K 校准路径。
- 确认高级工程区只剩模型回读、工程归零、手动模型参数、详细日志和当前模型摘要。

第八阶段后建议补充：

- 确认 `采样` 页首屏只看到采样模式、开始/停止、清空/导出、实时摘要和会话摘要，不再出现大段说明。
- 确认 `会话详情`、`曲线设置`、`导出详情` 默认收起，点击后仍能查看工程信息。
- 确认 `运行` 页不再出现 `当前交付边界`，曲线和测试会话在 Base / Plus 路径下都可见。
- 确认无故障时运行页不再显示无意义的故障参考；出现真实故障时仍能显示故障参考。

第九阶段后建议补充：

- 确认 `运行` 页系统状态卡片更紧凑，但 `状态 / 安全原因 / 影响` 仍能一眼看清。
- 确认 `采样` 页不再让用户误以为必须先开启采样模式；直接点击 `开始采样` 后能出现采样会话。
- 按需展开 `采样设置`，确认仍能访问设备侧 `开启采样模式 / 关闭采样模式`。

第十阶段后建议补充：

- 确认连接设备后 `型号` 页能显示 `当前设备：Base（无距离传感器）` 或 `当前设备：Plus（有距离传感器）`。
- 点击 `确认` 后，确认中 / 成功 / 超时 / 失败状态无需展开详情即可看到。
- 确认 `当前设备` 不会随着只点击 Base / Plus 选择项立刻变化，只有设备回传真值后才变化。

## 未覆盖范围

- 校准页已完成第一轮主流程深压缩；仍未做视觉主题级重设计。
- 采样页已完成第一轮内部深压缩；导出弹窗、曲线视觉和数据表专业化仍未做。
- 未改视觉主题、图表绘制和底部波形控制条。
- 未做多 ViewModel / Navigation 架构重写。
- 未实现“已写入归零后的撤回”。当前固件 / APP 合同只支持发送 `SCALE:ZERO` / `CAL:ZERO`，没有可逆事务或自动恢复旧零点能力；本阶段只实现发送前取消。
- 未真机复核 `律动离开` 对实际离开平台停波行为的影响；当前只完成 Demo APP 到固件既有命令 / ACK / snapshot 的代码接入，本轮仍需 UI smoke 和按需保护开关真机证据。
