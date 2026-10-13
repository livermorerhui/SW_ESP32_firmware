# E8 阶段 C：Demo APP 工作人员配置进度

日期：2026-10-13
状态：**页面与业务链已接入代码，Android 四模块离线测试通过；Android Studio Run 和真机 BLE 尚未验收**
范围：Demo APP 的型号、传感器、空载距离与本地历史归属；不包含 J100/CYD 运行状态算法、SW APP 页面或服务器。

## 工作人员眼前的路线

以下是**代码已接入、实物待验证**的操作路线，不是已做完的真机记录。

1. 工作人员用 Android Studio 的 **Run** 安装本轮 Demo APP，连接待配置的 ESP32。APP 先读取设备稳定身份与型号/传感器配置；读回之前，型号写入按钮不可用，也不把本地默认值当成设备真相。此处不经过服务器。
2. 如果这台是全新 Base 板，型号页只显示型号卡片。工作人员选 **Plus** 并写入。APP 等 ESP32 的 SET 回包，再 GET 复核，随后重连**同一台**设备并再次读取身份和配置；在读回一致以前不显示配置完成。Base 本身不要求传感器或空载设置。
3. Plus 页面出现“激光传感器设置”“空载距离设置”和既有保护调试卡片。工作人员按实物选择 **J100-485** 或 **CYD3S1612-IIC** 并写入。APP 再做 SET→GET→同设备重连→重新订阅实时流；只有当前传感器持续产生匹配、有效的新距离，页面才显示距离可用。未选传感器、断流或故障时，空载“设置”不能点。
4. 工作人员确认平台确实空载，查看页面上的**实时距离**，点“设置”并确认。APP 发送 `EMPTY:SET_CURRENT`；ESP32 在收到命令时自行取当前有效样本保存，APP 等 SET 回包和独立 `EMPTY:GET` 回读一致，才显示实际保存的空载数值。清除也是设备操作，需先有已确认空载值，清除后再回读未设置状态。体重模型是否存在不参与这一步。
5. 切换设备或传感器后，校准点、拟合草稿和采样历史按**设备稳定 ID + 传感器 profile**取各自记录；旧版没有设备归属的历史单独只读展示，不进入当前拟合、采样或导出。设备回读的空载距离和体重模型仍以 ESP32 为准，本机历史文件不充当设备当前参数。

## 代码层已接入

| 工作人员能看到或执行的动作 | 代码证据 | 当前边界 |
|---|---|---|
| Base 只显示型号；Plus 才显示传感器、空载、保护调试卡片 | `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/screens/MainScreen.kt` 的 `DeviceToolsContent`；`ui/components/DeviceProfileSection.kt` | Pro/Ultra 仍是禁用的预留项；页面可见不等于设备已写入 |
| 型号和传感器写入 | `DemoViewModel.kt` 的 `sendProfileConfig`、`handleProfileConfigResult`、`reconnectAfterProfileWrite`；`ProfileDeviceTransactions.kt` | 要有当前连接身份、schema 1 GET、generation 和同连接回包；SET ACK 之后还要 GET，切换后还要重连复核 |
| 测距可用性与空载按钮 | `DemoViewModel.kt` 的 `enableDemoRealtimeStreamIfSupported`、`updateProfileDistanceReadiness`、`startProfileDistanceExpiryWatchdog`；`DeviceProfileSection.kt` 的 `EmptyDistanceSection` | UI 要有当前订阅 ACK、匹配 profile 的正式流、新递增序号和连续有效样本；断流撤销可用；ESP32 对 SET_CURRENT 再独立检查 |
| 空载保存与清除 | `DemoViewModel.kt` 的 `sendEmptyDistanceSet`/`sendEmptyDistanceClear`；`ProfileDeviceTransactions.kt` 的 `ProfileEmptyTransaction` | 页面先显示待确认，设备 GET 的 profile、generation、revision、状态和值一致才确认；不是把 APP 显示的数值直接写入 |
| 历史记录归属 | `DemoSessionPersistence.kt`、`ProfileSessionFileKey.kt`、`DemoViewModel.kt` 的 `activateProfileHistory`；校准/采样组件的旧历史只读区 | 新文件由稳定 ID+profile 派生安全文件名；损坏文件阻断继续录点/采样并保留原文件；旧 v1/v2 文件不自动归属 |

失败分支也已按设备真相收口：配置 GET 失败会撤销本次配置确认并要求重连；空载 GET 失败会撤销旧空载数值和设置资格，忽略当前连接后续排队回包。旧连接的 BLE 原始行不会再更新新连接的测试记录；本机历史保存失败时，未落盘快照会保留并阻止旧磁盘记录覆盖它，重新连接时先尝试补存。

页面上还有两个需区分的状态：**“等数据”**表示尚未确认当前传感器连续输出；**“通信故障”**是断流超时或收到不合要求的数据后的提示。当前 UI 使用连续 2 个样本、3 秒断流窗口作为**开发测试门禁**，可以在测试设备上按路线操作空载设置；这些数值尚未用 J100、CYD 各自 reader 周期和实物数据验证，不能写成正式工程参数或产品验收。ESP32 对空载命令的检查是第二道独立门禁。

## 离线检查与证据边界

- 在 `tools/android_demo` 执行 `./gradlew :sonicwave-protocol:test :sonicwave-transport:testDebugUnitTest :sonicwave-sdk:testDebugUnitTest :app-demo:testDebugUnitTest --offline --no-daemon`，结果 `BUILD SUCCESSFUL`。JUnit XML 合计协议 42、传输 6、SDK 2、Demo APP 105，共 **155 项**，均 0 failure、0 error、0 skipped。`git diff --check` 通过。这证明离线编译与已有单元测试通过，不能证明 BLE 实物交互通过。
- 固件侧 focused gate：`python3 tools/run_evaluator_unit_tests.py` 与 `python3 tools/test_profile_empty_distance_store.py` 均通过；它们覆盖命令/回包和空载记录的部分主机行为，仍不能替代 ESP32 串口、BLE、J100/CYD 实物证据。
- 其中 `ProfileDeviceTransactionsTest` 对同设备/同连接的配置 ACK→GET、Base 同值写入、空载 revision/数值回读和清除回读建立测试；`ProfileSessionFileKeyTest` 覆盖文件名安全和 profile 隔离；`ProfileSessionLoadResultTest` 覆盖文件缺失、损坏与读取失败的区分。协议解析测试位于 `sonicwave-protocol/src/test/.../ProtocolCodecTest.kt`。
- 代码核对确认：旧全局 `demo_session_state.json` 只读；新记录保存为 `demo_session_v3_*.json`，文件内容写有设备 ID 和 profile，读取时复核归属。损坏的当前 profile 文件不会当作“新空白历史”覆盖。
- 本阶段**没有**通过 Android Studio Run 安装 APP，没有操作手机、ESP32、J100 或 CYD，没有 BLE 原始收发和串口证据；因此工作人员完整路线、断连/重连、实际距离和空载写入都不能标为真机通过。

## 下一道验收门

1. 离线编译、四模块单元测试和 `git diff --check` 已完成；下一步由用户通过 Android Studio **Run** 安装并操作。AI 不构建 APK、不代装，也不使用真机或模拟器操作。
2. 进入真机前核对设备固件是否包含阶段 B 的配置/空载新命令；是否需要重新烧录取决于设备现有版本，**当前未核实**。旧 Plus 设备若缺 `cfg_model` 键，须先按阶段 B 旧机过桥说明处理，不能拿它直接冒险升级。
3. 由用户操作两条实物路线：Base→Plus→J100→实时距离→设置/清除空载；Base→Plus→CYD→实时距离→设置/清除空载。再验证断线、重连、切换 profile、无距离、损坏/旧历史的页面表现。AI 先准备 BLE 与串口自动采集和时间标记，并在用户反馈后复核日志；没有可读证据不能写“通过”。
4. J100 距离版律动判断、双保护关闭规则、CYD 运行判断和任意传感器体重校准是后续阶段。当前校准采点门禁仍限制 `LASER_485`，不能把阶段 C 的历史隔离误写成“CYD 体重校准已开放”。正式 SW APP 页面和服务器本阶段未修改。

## 依据

- 产品与协议边界：[阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md)、[总蓝图](./esp32_base_plus_sensor_profile_app_configuration施工蓝图.md)、[阶段 B 记录](./e8_stage_b_firmware_progress_20261013.md)。阶段 B 文档中“Demo APP 尚未接入”描述的是当时的快照，以本阶段记录的当前代码状态为准。
- Android 官方 [BLE GATT 连接流程](https://developer.android.com/develop/connectivity/bluetooth/ble/connect-gatt-server)和[传输 BLE 数据/通知](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)支持“先连接和启用通知，再验证实际收发”的验证顺序；SET ACK 后 GET 与同设备身份核对是本项目合同，不是 Android 自动保证。
- Android 官方 [`AtomicFile`](https://developer.android.com/reference/android/util/AtomicFile)说明文件原子写入语义；设备/profile 归属、旧历史只读和损坏文件阻断均由本项目代码实现，仍需本机重启/损坏场景验证。
