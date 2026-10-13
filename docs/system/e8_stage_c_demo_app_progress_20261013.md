# E8 阶段 C：Demo APP 工作人员配置进度

日期：2026-10-13
状态：**旧 Plus + J100-485 设置、回读和距离已验证；先前安装的修复版 Demo 的连接、空载清除/重设及配置断电保留已在实机验证；提交前新增的失败/并发门禁只通过代码测试；型号/传感器写入无闪烁仍待合适设备验收**
范围：Demo APP 的型号、传感器、空载距离与本地历史归属；不包含 J100/CYD 运行状态算法、SW APP 页面或服务器。

## 工作人员眼前的路线

以下是**目标操作路线**。本台旧 Plus 的 J100 设置、回读和距离已有实物证据；新版 Demo 的空载设置、清除和重设也已在这台设备实测。新版 Demo 的型号/传感器写入无闪烁、全新 Base→Plus 仍待验收。

1. 工作人员用 Android Studio 的 **Run** 安装本轮 Demo APP，连接待配置的 ESP32。APP 先读取设备稳定身份与型号/传感器配置；读回之前，型号写入按钮不可用，也不把本地默认值当成设备真相。此处不经过服务器。
2. 如果这台是全新 Base 板，型号页只显示型号卡片。工作人员选 **Plus** 并写入。APP 等 ESP32 的 SET 回包，再在当前连接用独立 GET 复核；页面保持连接和型号卡片，在读回一致以前不显示配置完成。Base 本身不要求传感器或空载设置。
3. Plus 页面出现“激光传感器设置”“空载距离设置”和既有保护调试卡片。工作人员按实物选择 **J100-485** 或 **CYD3S1612-IIC** 并写入。APP 做 SET→GET、刷新设备状态和空载值、重新订阅实时流；只有当前传感器持续产生匹配、有效的新距离，页面才显示距离可用。未选传感器、断流或故障时，空载“设置”不能点。
4. 工作人员确认平台确实空载，查看页面上的**实时距离**，点“设置”并确认。APP 发送 `EMPTY:SET_CURRENT`；ESP32 在收到命令时自行取当前有效样本保存，APP 等 SET 回包和独立 `EMPTY:GET` 回读一致，才显示实际保存的空载数值。清除也是设备操作，需先有已确认空载值，清除后再回读未设置状态。体重模型是否存在不参与这一步。
5. 切换设备或传感器后，校准点、拟合草稿和采样历史按**设备稳定 ID + 传感器 profile**取各自记录；旧版没有设备归属的历史单独只读展示，不进入当前拟合、采样或导出。设备回读的空载距离和体重模型仍以 ESP32 为准，本机历史文件不充当设备当前参数。

## 代码层已接入

| 工作人员能看到或执行的动作 | 代码证据 | 当前边界 |
|---|---|---|
| Base 只显示型号；Plus 才显示传感器、空载、保护调试卡片 | `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/screens/MainScreen.kt` 的 `DeviceToolsContent`；`ui/components/DeviceProfileSection.kt` | Pro/Ultra 仍是禁用的预留项；页面可见不等于设备已写入 |
| 型号和传感器写入 | `DemoViewModel.kt` 的 `sendProfileConfig`、`handleProfileConfigResult`；`ProfileDeviceTransactions.kt` | 要有当前连接身份、schema 1 GET、generation 和同连接回包；SET ACK 之后独立 GET 核对，成功后不主动断连，旧订阅/距离撤销后重新订阅和等待新样本 |
| 测距可用性与空载按钮 | `DemoViewModel.kt` 的 `enableDemoRealtimeStreamIfSupported`、`updateProfileDistanceReadiness`、`startProfileDistanceExpiryWatchdog`；`ProfileDeviceTransactions.kt` 的订阅门禁；`DeviceProfileSection.kt` 的 `EmptyDistanceSection` | 写入时旧帧不再进入距离和曲线；UI 要有新的订阅 ACK、匹配 profile 的正式流、新递增序号和连续有效样本；断流撤销可用；ESP32 对 SET_CURRENT 再独立检查 |
| 开始按钮与体重参数 | `UiModels.kt` 的 `waveStartAvailability`；`DemoViewModel.kt` 的写入后 CAP/SNAPSHOT 与 `CAL:GET_MODEL` 回读 | 写入中及新 SNAPSHOT 前沿用旧“开始就绪”被禁止；旧体重模型先撤销，设备回读后再显示；SNAPSHOT 无请求 ID，仍须真机检查异步时序 |
| 空载保存与清除 | `DemoViewModel.kt` 的 `sendEmptyDistanceSet`/`sendEmptyDistanceClear`；`ProfileDeviceTransactions.kt` 的 `ProfileEmptyTransaction` | 页面先显示待确认，设备 GET 的 profile、generation、revision、状态和值一致才确认；不是把 APP 显示的数值直接写入 |
| 历史记录归属 | `DemoSessionPersistence.kt`、`ProfileSessionFileKey.kt`、`DemoViewModel.kt` 的 `activateProfileHistory`；校准/采样组件的旧历史只读区 | 新文件由稳定 ID+profile 派生安全文件名；损坏文件阻断继续录点/采样并保留原文件；旧 v1/v2 文件不自动归属 |

失败分支也已按设备真相收口：配置 GET 失败会撤销本次配置确认并要求重连；空载 GET 失败会撤销旧空载数值和设置资格，忽略当前连接后续排队回包。旧连接的 BLE 原始行不会再更新新连接的测试记录；本机历史保存失败时，未落盘快照会保留并阻止旧磁盘记录覆盖它，重新连接时先尝试补存。

页面上还有两个需区分的状态：**“等数据”**表示尚未确认当前传感器连续输出；**“通信故障”**是断流超时或收到不合要求的数据后的提示。当前 UI 使用连续 2 个样本、3 秒断流窗口作为**开发测试门禁**，可以在测试设备上按路线操作空载设置；这些数值尚未用 J100、CYD 各自 reader 周期和实物数据验证，不能写成正式工程参数或产品验收。ESP32 对空载命令的检查是第二道独立门禁。

## 离线检查、旧 Plus 烧录与证据边界

- 在 `tools/android_demo` 执行 `./gradlew :sonicwave-protocol:test :sonicwave-transport:testDebugUnitTest :sonicwave-sdk:testDebugUnitTest :app-demo:testDebugUnitTest --offline --no-daemon`，结果 `BUILD SUCCESSFUL`。提交前再运行改动模块 `:app-demo:testDebugUnitTest` 成功；当前 JUnit XML 为协议 42、传输 6、SDK 2、Demo APP 120，共 **170 项**，均 0 failure、0 error、0 skipped。首次阶段 C 快照为 159 项，后续补丁增加了回归用例。这证明离线编译与单元测试通过，不能证明 BLE 实物交互或页面无闪烁。
- 固件侧统一镜像 `r1-27` 已编译；`python3 tools/run_evaluator_unit_tests.py` 与 `python3 tools/test_profile_empty_distance_store.py` 等相关主机测试通过。构建和主机测试覆盖部分命令/回包、空载记录行为，不能替代 ESP32 串口、BLE、J100/CYD 实物证据。
- 其中 `ProfileDeviceTransactionsTest` 对同设备/同连接的配置 ACK→GET、旧订阅 ACK 隔离、写后 SNAPSHOT 门禁、空载 revision/数值回读和清除回读建立测试；`DemoStartReadyRegressionTest` 覆盖配置写入期间和新状态回读前的开始按钮；`ProfileSessionFileKeyTest` 覆盖文件名安全和 profile 隔离；`ProfileSessionLoadResultTest` 覆盖文件缺失、损坏与读取失败的区分。协议解析测试位于 `sonicwave-protocol/src/test/.../ProtocolCodecTest.kt`。
- 代码核对确认：旧全局 `demo_session_state.json` 只读；新记录保存为 `demo_session_v3_*.json`，文件内容写有设备 ID 和 profile，读取时复核归属。损坏的当前 profile 文件不会当作“新空白历史”覆盖。
- **旧 Plus 实机准备已完成：**用 Espressif `esptool` 在烧录前读取了 16 MB 整片 Flash，保存校验摘要；原机分区表经官方工具解码，与统一固件目标布局一致，其中 NVS 为 `0x9000/0x5000`。官方 NVS 解析确认 `scale_cal/cfg_model` 和 `cfg_laser` 均为 `uint8_t=1`、两页 CRC 均为 OK，且没有 `cfg_slot0/1`；第二次读取的 NVS 与整片备份中的 NVS 逐字节一致。因此这台设备不需要烧录旧配置维护镜像。
- **旧 Plus 固件烧录已完成：**通过 PlatformIO USB 上传统一固件 `r1-27`，`esptool` 对各写入镜像均报告哈希校验通过；烧录后再次读取的 NVS 与烧录前备份逐字节一致。私有原始备份、解析输出和上传日志保存在 `.artifacts/e8_preupgrade/`，不进入 Git。
- **旧 Plus 型号真机回读已通过：**用户连接 Demo APP 后在型号页看到 Plus。同期 Android logcat 记录 GATT 连接及服务发现成功、通知启用，Demo 收到 `ACK:DEVICE_CONFIG op=GET schema=1 platform_model=PLUS sensor_profile=NONE laser_installed=1 generation=0`。这使“升级后仍为 Plus”同时具备页面观察与设备回包证据；`NONE` 表示尚未选传感器，不应解释为已验收激光通讯。日志还有 `ACK:STREAM enabled=1`，后续 `EVT:STREAM valid=0 distance_valid=0 reason=SENSOR_UNAVAILABLE`，与未选传感器一致。该次连接时间窗内未见 Demo APP 的 FATAL/ANR；日志里其他系统进程的旧崩溃不归因于 Demo APP。私有 logcat 证据保存在 `.artifacts/e8_preupgrade/`，不进入 Git。
- **设置操作实机失败，根因已定位：**工作人员反馈点击“设置型号”或“设置激光传感器”会断连，重连后设置未生效。先前手机日志记录了无 SET ACK/NACK、GATT status 133 和设备运行时间归零，但当时尚无可读串口。随后在小米 8 + 同一旧 Plus + 实物 J100-485 下，工作人员仅点一次传感器“写入”；PlatformIO 串口原始十六进制采集解码后明确出现 `***ERROR*** A stack overflow in task BleCtrl has been detected.`，紧接着 `Rebooting`，下一次启动为 `rst:0xc` / `重启原因=PANIC`。小米 8 日志记录重连后 GET 仍为 `PLUS/NONE/generation=0`，无任何配置 SET ACK/NACK。由此确认固件 `BleCtrl` 栈溢出导致设备重启和 APP 断连；手机日志没有原始 TX 明文，不能用其单独还原写入字节。
- **失败后存储只读复核：**使用项目既有 Espressif `esptool v4.11` 分别在早期失败后和小米 8 单次 J100 复现后读取 NVS `0x9000/0x5000`，均与首次烧录后备份逐字节相同；本台设备的失败设置没有留下双槽配置。读取会按工具流程重启设备，但不会写入 Flash。原始日志、NVS 与串口解码均保存在私有 `.artifacts/e8_preupgrade/`，不进入 Git。
- **定点修复已施工：**`BleCtrl` 原分配 4096 字节，现分配 12288 字节并检查任务创建是否成功；仅在 `DEVICE:SET_CONFIG` 时记录解析前、分发前、回包后的栈最低剩余字节和内部内存余量。没有修改命令、ACK/NACK、BLE 连接生命周期、APP 或 J100 读数逻辑。ESP-IDF 4.4.7 的任务栈参数和 `uxTaskGetStackHighWaterMark` 单位均为字节；12288 是为取得安全余量而设的待实测值，不是已验证的最小需求。
- **修复版离线与烧录验证：**统一镜像 `esp32s3_unified` 编译通过；`run_evaluator_unit_tests.py`、`test_profile_empty_distance_store.py`、`test_stage_b_stream_and_stop.py` 与 `git diff --check` 通过。PlatformIO 上传报告镜像哈希校验通过；上传后 NVS 与上传前逐字节相同。编译和烧录仅是进入实物验证的条件。
- **旧版 Demo 的 J100-485 一次设置真机通过，但页面体验不通过：**工作人员在 Demo APP 连接旧 Plus 后仅点一次 J100-485“写入”，反馈页面短暂刷新，随后实时距离随动作变化。小米 8 日志 19:41:29 收到 `ACK:DEVICE_CONFIG op=SET ... PLUS/J100_485/generation=1`，紧接着 GET 同值；当时 APP 的成功路径主动断开并重连同一设备，19:41:30 再次 GET 同值。页面短闪由此产生，用户明确要求修正原合同；这次短闪不是前次固件 PANIC。其后连续 `EVT:STREAM distance_valid=1 backend=LASER_485 distance_mm=112.32/112.33`；`weight_valid=0` 符合尚无体重模型，不妨碍距离。ESP32 串口记录 `BleCtrl` 配置后最低剩余 8068 字节、内部内存最小空闲 182448 字节，未见栈溢出或 PANIC；该水位仅覆盖本次 J100 设置和观察时间，不能替代全部工作场景。配置后 NVS 与设置前不同；官方读 Flash 过程使 ESP32 重启，重连后 19:43:27 再次 GET 仍为 `PLUS/J100_485/generation=1`，随后仍收到有效 J100 距离流。此证明一次软件重启后的保留，尚未单独证明物理断电后保留。
- **两次额外断连的证据边界：**19:43:06 手机 GATT `status=8` 的断连紧随本机 `esptool read_flash`，工具日志明确写 `Hard resetting via RTS pin`，本次可归于诊断操作使 ESP32 重启。19:43:58 第二次 `status=8` 在串口监视器关闭约 4 秒后，时序高度相关，但没有第二次 ESP32 复位原因，不能把直接原因写为已证明；两次都没有新的配置 SET。随后停止所有 ESP32 串口/esptool 操作，仅以手机日志观察 19:49:06 后约 2 分钟：GET 仍为 `PLUS/J100_485/generation=1`，持续收到有效距离，窗口内无断连回调；用户也确认“没有自动断连”。这只说明该隔离窗口稳定，不能保证以后绝无断连。
- **新版 Demo 定点修复已完成代码层检查：**成功写入与失败后能读回实际配置时，都留在当前 BLE 连接，不再调用主动断连；写入期间保留型号卡片，清旧距离和体重模型；新 `ACK:STREAM` 前不消费距离，写后新 SNAPSHOT 前不把旧 `start_ready` 显示为就绪，随后重查 `CAL:GET_MODEL`。相关单元测试、四模块离线测试及独立代码复核通过。新版 Demo 的配置写入按钮仍未重新真机点击，不能把代码检查写成该按钮的无闪烁验收；`SNAPSHOT` 无请求 ID 的时序边界仍需实物观察。
- **新版 Demo 的连接与空载设置已做一轮真机验证：**用户用 Android Studio Run 安装新版 Demo 后，连接现有 Plus/J100，观察到实时距离持续更新，未见自动断连。仅采手机日志，未打开 ESP32 串口或运行 esptool：20:16:27 GATT 连接回调 `status=0`；20:16:29 设备 GET 返回 `PLUS/J100_485/generation=1`，随后 `ACK:STREAM enabled=1` 和 `ACK:EMPTY op=GET status=UNSET empty_revision=0`。在“平台无人、设备停波”的测试前置条件提示下，用户仅点击空载“设置”一次，反馈页面没有闪烁，显示 **112.33 mm**；20:19:01 设备返回 `ACK:EMPTY op=SET_CURRENT ... distance_mm=112.33 ... empty_revision=1`，独立 `ACK:EMPTY op=GET` 再次返回同值及同 revision。之后仍持续收到 `backend=LASER_485 distance_valid=1 distance_mm=112.33` 的实时流；本轮日志截至 20:19:44 未见 GATT 断连回调或 AndroidRuntime 崩溃。原始元数据、手机日志及空 stderr 在 `.artifacts/e8_stage_c_phone_20261013_201429/`；这一路没有 ESP32 串口，设备侧执行只由 BLE ACK/GET 证实。新版安装时间窗口有包替换和新进程记录；“由 Android Studio Run 点击安装”来自用户操作反馈。
- **首次空载实测后的代码审计发现并修复体重模型显示缺陷：**固件每次成功 SET_CURRENT/CLEAR 都撤销旧体重模型，即使 D_e 数值不变；APP 旧代码将空载置为 PENDING 后，成功回读时误用 PENDING 状态中的“旧确认值”判断是否清模型，导致校准页可能继续显示旧 abc。现在点按即将旧模型标为未确认；同连接的 SET/CLEAR ACK→独立 GET 成功后，无条件撤销旧模型并以当前连接查询 `CAL:GET_MODEL`。旧 `ready=1` 模型回包不能在新空载 revision 后复活旧模型；`ready=0` 不展示占位“当前模型”。空载修改与体重模型写入、profile 写入互斥；旧连接的异步回调不能覆盖新连接。若模型回读 10 秒仍未到达，页面提示重新连接核对，并继续禁止写模型/空载，避免永久显示“等待中”或把旧模型当有效。NACK、GET 失败或超时时也保守隐藏旧模型。新增同值重设、清除、迟到旧模型回包、并发/超时门禁回归测试；当时 `:app-demo:testDebugUnitTest --offline --no-daemon` **14 suites / 116 项通过，0 失败、0 错误、0 跳过**，`git diff --check` 通过；后续已安装该补丁并完成下一条所述的空载按钮实物测试。`CAL_MODEL` 回包仍没有 request ID/empty_revision，代码和单元测试不能单独证明所有同连接迟到包的先后顺序。
- **提交前审计继续补了失败/并发门禁，尚未重新安装实测：**配置 SET 的异步发送结果按连接代际和本次请求序号核对，旧连接或同连接前一次写入的迟到失败不能清掉新事务；`EMPTY:GET` 收到 NACK 或发送失败时，校准页明确提示重新连接核对，不再永久显示“等待设备回读”，旧 GET 的迟到失败也不能终止同连接的下一次空载操作；配置/空载事务和写后状态刷新期间，体重参数写入与“参数归零”按钮和命令入口均禁用。新增四项针对性回归用例，当前 Demo APP **120 项**和四模块合计 **170 项**均无失败。上述新门禁尚未通过 Android Studio Run 和真机 BLE 验收；已有 Plus/J100 的空载实测记录属于前一安装版本。
- **修复版 Demo 的空载清除与重设已做一轮真机验证：**用户用 Android Studio Run 再安装修复版，连接旧 Plus/J100；20:45:39 设备 GET 回 `D_e=112.33 mm/revision=1`，页面显示正常且实时距离更新。用户仅点一次“清除”，页面变为“未设置”，无闪烁或断连；20:46:18 手机日志依次收到 `ACK:EMPTY op=CLEAR status=UNSET empty_revision=2`、独立 GET 同状态与 revision，随后 `ACK:CAL_MODEL ready=0`。用户再仅点一次“设置”，页面显示 **112.34 mm**，无闪烁；20:47:55 收到 `ACK:EMPTY op=SET_CURRENT ... distance_mm=112.34 empty_revision=3`、独立 GET 同值与 revision，随后 `ACK:CAL_MODEL ready=0`，有效 J100 距离流持续。在设置/清除的时间窗口内未见 GATT 断连回调或 AndroidRuntime 崩溃。手机日志与元数据在 `.artifacts/e8_stage_c_phone_20261013_204430/`；本轮未开 ESP32 串口，故无法单独报告设备侧串口执行日志。原设备没有已校准体重模型，模型失效的代码路径由单元测试覆盖，不能将 `ready=0` 的真机回包当成“已有模型重设后失效”的实物验收。
- **RST 复位后的配置回读已单独验证：**用户明确反馈操作的是 ESP32 的 RST。手机日志 20:50:12 记录 BLE 断开，20:50:19 重新连接；20:50:21 GET 仍为 `PLUS/J100_485/generation=1`、`D_e=112.34 mm/empty_revision=3`，重新收到 `ACK:STREAM enabled=1`、`ACK:CAL_MODEL ready=0` 与有效 J100 距离流。用户反馈重连后一切正常。RST 只证明一次复位保留；随后另做了下一条的实际断电上电测试。
- **物理断电再上电后的配置回读已验证：**用户按“只断开 ESP32 供电、数秒后接回，手机继续连接电脑”的单步要求操作，反馈重连后一切正常。手机日志 20:52:01 记录 BLE 断开、20:52:15 重新连接，20:52:16～17 GET 仍为 `PLUS/J100_485/generation=1` 和 `D_e=112.34 mm/empty_revision=3`，重新收到 `ACK:STREAM enabled=1`、`ACK:CAL_MODEL ready=0` 及有效 `backend=LASER_485` 距离流。物理动作由用户反馈证实，手机日志支持重连后的设备回读；本轮未采串口，不能报告上电原因或固件内部启动轨迹。
- **证据边界：**旧版 Demo 的 J100 配置设置、回读和实时距离已通过，但写入时无闪烁要求未通过；修复版 Demo 的连接、空载清除/重设及本台设备断电后的配置回读已通过本轮观察。修复版的型号/传感器写入无闪烁、全新 Base 改 Plus、CYD 切换及已有有效体重模型后修改空载值的实物路径仍未通过。当前设备已经是 `PLUS/J100_485`，同值配置按钮禁用，不为重测而错误切换实物传感器。

## 下一道验收门

1. 修复版 Demo 的代码复核、离线测试、Android Studio Run 安装后的连接、空载清除/重设及复位/断电后回读已完成上述逐步核对。对**当前已升级的 Plus/J100 板**继续现有真机验证，不需要重烧固件、重启手机/ESP32、清 APP 数据或连接服务器；新代码若需实际安装，仍由工作人员用 Android Studio Run。只观察当前板的 BLE 行为时，AI 优先采手机 BLE/APP 日志，不打开 ESP32 串口、不运行 esptool。另一块旧固件候选板的升级必须先按旧机流程备份和核验。
   本轮手机侧证据可判定是否发生 GATT 断连、APP 是否主动关闭连接、配置 ACK/GET 与距离流是否连续；若再发生无法解释的断连，缺少同时采到的 ESP32 串口就不能断言设备是否重启，需另设计在手机连接前已稳定启动的设备侧采集。
2. 本机已是 `PLUS/J100_485`，型号和传感器同值“写入”按钮禁用；不能为了复现闪烁而乱切换实物传感器。用户补充手边有一块**旧固件候选板**，但原 Base/Plus、板型、传感器、原 NVS 和升级可行性尚未核验；旧固件自身也不能验新版配置写入。须先按[旧机升级流程](./e8_stage_b_legacy_bridge_20261013.md)完成备份、核验和保留配置的统一固件升级，才可用它做修复版配置写入无闪烁验收。当前板的空载距离设置、清除、重设及回读均已通过各一次实物操作。
3. 修复版已通过一次“读回 112.33 mm→清除 revision=2→重设 112.34 mm/revision=3→RST 重连→实际断电上电重连”的逐步真机核对。后两次重连均读回 `PLUS/J100_485/D_e=112.34 mm`，实时距离恢复。全新 Base→Plus、修复版配置写入无闪烁及另一种传感器的路线待合适硬件另测，不能用这台已设 Plus/J100 的设备代替。
4. 还需验证切换 profile、无距离、损坏/旧历史的页面表现；没有 APP 与设备侧可读证据不能写“通过”。J100 距离版律动判断、双保护关闭规则、CYD 运行判断和任意传感器体重校准属于后续阶段。当前校准采点门禁仍限制 `LASER_485`，不能把阶段 C 的历史隔离误写成“CYD 体重校准已开放”。正式 SW APP 页面和服务器本阶段未修改。

## 依据

- 产品与协议边界：[阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md)、[总蓝图](./esp32_base_plus_sensor_profile_app_configuration施工蓝图.md)、[阶段 B 记录](./e8_stage_b_firmware_progress_20261013.md)。阶段 B 文档中“Demo APP 尚未接入”描述的是当时的快照，以本阶段记录的当前代码状态为准。
- 旧 Plus 的整片备份、原 NVS 解析、二次读取和上传校验证据保存在本地 `.artifacts/e8_preupgrade/`。方法依据为 [Espressif esptool 读写说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)、[ESP-IDF 分区表说明](https://docs.espressif.com/projects/esp-idf/en/v5.1.1/esp32s3/api-guides/partition-tables.html)及 [官方 NVS 解析工具](https://docs.espressif.com/projects/esp-idf/en/v5.1.7/esp32s3/api-reference/storage/nvs_partition_parse.html)；本机执行结果仍以保存的原始日志和备份为准。
- Android 官方 [BLE GATT 连接流程](https://developer.android.com/develop/connectivity/bluetooth/ble/connect-gatt-server)和[传输 BLE 数据/通知](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)支持“先连接和启用通知，再验证实际收发”的验证顺序；SET ACK 后 GET 与同设备身份核对是本项目合同，不是 Android 自动保证。
- [ESP-IDF 4.4.7 FreeRTOS API](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/freertos.html)说明任务栈大小及高水位单位均为字节；[官方 RAM 使用指南](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/performance/ram-usage.html)要求结合实际运行路径测任务栈最低余量。此处栈溢出结论来自本机串口，不来自公开资料推断。
- Android 官方 [`AtomicFile`](https://developer.android.com/reference/android/util/AtomicFile)说明文件原子写入语义；设备/profile 归属、旧历史只读和损坏文件阻断均由本项目代码实现，仍需本机重启/损坏场景验证。
