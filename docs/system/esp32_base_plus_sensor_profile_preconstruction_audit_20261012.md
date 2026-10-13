# E8 施工前审计报告：Base/Plus 与传感器 profile 配置

日期：2026-10-12；复核：2026-10-13
范围：ESP32 统一固件、Demo APP、BLE 配置、OTA 和真机测试准备
结论：六轮资料与跨层代码审计、最小测试矩阵已补齐；可进入合同冻结和旧机过桥实现，尚不能直接批量烧录或宣布 J100/CYD 运行验收

## 1. 任务分类和冻结边界

- 任务类型：施工蓝图、跨层代码审计、公开资料复核、测试准备。
- 目标链路：Demo APP 型号页 → BLE 配置命令 → ESP32 持久化画像 → reader/profile → 距离状态和启动门禁 → APP 状态显示。
- 真相源：ESP32 配置存储、ACK、SNAPSHOT、正式 EVT。
- 不修改：CYD 判断参数、SW APP 页面、服务器、OTA 分片和业务代码。
- 不允许：自动根据接口或一次探测结果猜品牌/型号。

## 2. 当前代码事实

| 项目 | 证据 | 结论 |
|---|---|---|
| 默认配置 | src/modules/laser/DeviceConfigStore.cpp、src/core/DeviceConfig.h | 当前代码缺配置时默认 PLUS；目标是新设备默认 Base，已有 Base/Plus 经普通重烧或 OTA 保持原型号 |
| 构建入口 | platformio.ini | default_envs 仍为 esp32s3，统一环境要明确为生产烧录入口 |
| profile | src/modules/laser/LaserSensorProfile.h | 当前只有 J100 profile，需要新增稳定 CYD profile |
| 传感器选择 | src/modules/laser/LaserMeasurementReader.cpp | 当前统一固件自动探测，和 APP 明确选择的目标不一致 |
| 配置协议 | src/core/ProtocolCodec.h、src/main.cpp | 当前只支持 platform_model 和 laser_installed |
| Demo APP 页面 | tools/android_demo/app-demo/.../MainScreen.kt | 当前有型号、空载、保护卡片，没有传感器型号卡片；Base 也会渲染保护卡片，与产品规则不符 |
| 正式 SW APP 入口 | TopBarStateProvider.kt、SonicWaveProtectionActionGatePolicy.kt | Base 无保护入口；Plus 手机主动打开设置、平板通过操作栏，运行中不能切换，测距故障时入口不可操作 |
| 空载距离 | src/main.cpp、src/modules/laser/LaserModule.cpp | 已有独立 GET/SET/CLEAR，不要求体重模型，可复用但要绑定 profile |
| 双开关降级 | src/core/RuntimeProtectionPolicy.cpp | 单开关只影响对应保护动作，两个同时关闭尚未改变 start gate |
| 距离/体重分离 | src/modules/laser/LaserModule.cpp | 距离状态可独立于体重模型，符合目标方向 |

## 3. 设计判定

### 已判定

1. 一份 unified 固件包含已支持的多个 reader。
2. 从未保存型号的新设备默认 BASE；已有 Base/Plus 配置的设备，经普通 USB 重烧或 OTA 后保持原型号。
3. 工作人员在 Demo APP 选择 PLUS 和具体 sensor_profile。
4. sensor_profile 是正式配置，laser_installed 仅保留兼容派生字段。
5. profile 切换后旧空载距离和体重模型不能自动复用；旧体重记录只作查看，需要测体重时按新传感器重新校准。
6. 两个保护开关同时关闭时，直接进入降级、不要求站稳，也不弹确认提示。
7. 体重模型不参与距离状态门禁。
8. 任意已选定且能稳定输出有效距离的传感器都可以建立体重模型；模型只用于体重计算和显示。
9. SW APP 不参与上述配置。

### 已明确

1. 双保护关闭直接降级，不弹确认提示。
2. 更换传感器后，旧体重校准参数不自动使用；历史记录可以保留查看，重新测体重时按新传感器校准。
3. Pro、Ultra 只是未来预留名称，本轮不实现具体产品行为。
4. 保护开关是用户在正式 SW APP 中使用的运行时控制，不是工作人员出厂配置项目。ESP32 未重新启动时保持当前开关值，APP 退出、BLE 断开重连、暂停或停止律动都不改变它；ESP32 断电重上电或其他重新启动后，两项都默认开启。Plus 设置界面须经可靠设备回读后才把值标为已确认；Base 没有保护开关入口。

### 仍需合同冻结

实际操作边界已经核对：工作人员目前在 Demo APP 能配置型号和空载距离，传感器型号选择尚未开发；普通用户在正式 SW APP 使用保护开关。正式 SW APP 运行中和测距故障时均不能操作开关。原来列出的“工作人员调试时关双保护、用户随后开始”和“运行中重新打开保护”不是已证实的使用流程，已从蓝图移除。

工程合同（不需要用户决定字段名）：

1. 正式配置命令、ACK 和独立 GET 回读的最终字段，以及 J100/CYD 的稳定 profile ID。
2. `PLUS + NONE` 的旧兼容字段、SW APP 消费和故障提示；旧固件可能忽略新增 profile 字段，APP 必须先确认能力并回读。
3. D_e 清除或变化后模型记录保留但失效、NVS 写入失败时的回滚顺序；当前代码会删除模型，与目标不一致。
4. Plus 摔倒与律动离开开关的完整回读：现有 `SNAPSHOT` 只带 `leave_stop_enabled`，不能用 APP 本地默认值冒充两项均已设备确认。

## 4. 公开资料检索记录

| 轮次 | 来源 | 得到的规则 | SW 映射 |
|---|---|---|---|
| 1 | Arduino-ESP32 Preferences、ESP-IDF Storage | NVS 可跨重启/断电，应用要自行管理 schema/有效性 | profile、D_e、模型使用版本、generation、CRC |
| 2 | Android BLE overview、Transfer BLE data | 连接后要发现 GATT 服务并配置通知，应用命令不能替代通知证据 | APP 写配置后 ACK + GET 回读，实时流继续显式订阅 |
| 3 | Espressif I2C/GPIO | SDA/SCL 为总线信号，需要合适上拉；GPIO/API 资料不能证明传感器身份 | IIC reader 与 profile 分离，型号由工作人员选择 |
| 4 | Espressif OTA | 新镜像首启动要自检并确认，否则回滚；NVS 配置不随 APP 镜像自动回滚 | 保持当前先确认镜像、后初始化 reader 的边界；另外验证配置/reader/BLE，旧镜像读新配置也要测 |
| 5 | NIST TN 1297/VIM 术语 | 重复性需用数据表达，不能由公开资料直接给业务阈值 | J100/CYD 阈值和容差分别采集、回放、冻结 |
| 6 | 项目内 J100/CYD PDF、PlatformIO 构建文档 | 模块标称单位/量程/接线；`default_envs` 决定默认构建入口 | 不混用型号和状态参数；统一环境尚未成为默认正式烧录目标 |

来源：

- https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/index.html
- https://developer.android.com/develop/connectivity/bluetooth/ble/ble-overview
- https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html
- https://www.nist.gov/pml/nist-technical-note-1297
- https://docs.platformio.org/en/latest/projectconf/sections/platformio/options/generic/default_envs.html

公开资料只约束通用工程方法，不替代本项目协议和真机数据。

## 5. 施工前测试包

以下是**待施工实现**的测试设计，不是本轮已经通过的 E8 测试；主蓝图 §9.2.1 给出 E8-C/P/M/S/O/J 共 17 个场景 ID、输入、预期和证据层级，另有真机操作矩阵。

### 固件

- 默认 Base 和旧 Plus 配置迁移；
- profile 合法性、CRC、generation、回读；
- J100/CYD reader 选择、Base 不探测、切换后旧任务/旧帧隔离；
- profile 切换后 D_e/模型失效；
- 两个开关同时关闭、单开关关闭、两个开启；
- 两开关关闭后冷启动，确认都恢复开启且 APP 回读显示一致；
- 无 D_e、无模型、传感器故障；
- J100 空载/有人/站稳与 3 kg/5 kg 标签数据，`R_D=|MA12_D-D_b|/|D_b-D_e|` 的 16%/28% 边界、1500/700 ms 时序、低跨度和断流窗口；离台、摔倒的正式停波/原因/恢复与反例；
- OTA 首启动成功、失败、回滚旧镜像后仍能读旧配置。

### 协议

- DEVICE:SET_CONFIG 新旧字段；
- CAP/SNAPSHOT 旧 golden frame 与 182 字节预算保持；profile 只走独立配置 GET；
- EMPTY:* 和 CAL:* 相互独立；
- ACK/NACK、原因码、generation；
- EVT:STREAM distance_valid/weight_valid/backend。

### Demo APP

- 型号页 Base/Plus 显示；
- sensor_profile 写入、ACK、GET 一致性；普通 ACK 不算最终成功；
- 空载设置/清除；过期、跨 profile、偏离当前读数或运行中的恶意/迟到 `EMPTY:SET` 必须被固件拒绝；
- profile 切换后过期提示；
- 选择传感器后必须收到有效距离，才能显示传感器可用并开放空载设置和体重校准；
- 双开关降级只接受 ESP32 truth；
- 无体重模型时距离页面不被阻塞；任意有效距离传感器均可单独建立体重模型；
- 本地历史按稳定设备身份/profile 隔离，无法归属的旧记录只读；
- 第二次记录、拟合、写入、回读无闪退。

### 真机

必须收集 ESP32 串口、Demo APP BLE TX/RX、结构化状态、操作标记和界面结果。SW APP 只做状态和启停回归。

## 6. 预检结果

- 已跟踪改动的 `git diff --check`：通过；未跟踪的新蓝图另做空白行与链接检查。
- `python3 tools/run_evaluator_unit_tests.py`：现有基线通过；该脚本未覆盖 E8 新测试，不能据此宣称配置流程通过。
- PlatformIO unified 构建：尝试时发现工具环境自动安装 tool-scons；为避免擅自改变环境已中止，因此不能记为构建通过。
- Android Studio Run、APK 安装、ESP32 烧录和真机 BLE：本轮按用户规则未执行。

## 7. 施工入口

下一步不是直接改业务代码，而是先冻结第 8 节的 P0 合同，之后按主蓝图阶段 A → E 施工：

1. 配置和协议合同；
2. 固件存储和 profile 选择；
3. Demo APP 型号页面；
4. 校准、采样、运行、日志；
5. 编译、OTA 和真机证据。

详细蓝图见：

docs/system/esp32_base_plus_sensor_profile_app_configuration施工蓝图.md

## 8. 2026-10-13 补充审计：施工前尚缺什么

本轮只读复核固件、Demo APP、BLE golden frames 和测试入口；没有修改业务代码、构建固件或操作设备。以下 P0 合同和相应测试补齐后，才可称为代码施工准备充分。

| P0 问题 | 当前证据 | 施工前须确定 | 最小可重复测试 |
|---|---|---|---|
| 新设备默认 Base、旧设备保持原型号 | `DeviceConfigStore.cpp` 当前缺配置时默认 Plus；NVS 保存配置；普通上传与整片擦除不同 | 已明确：从未保存型号的新设备默认 Base；已有 Base/Plus 经普通 USB 重烧、OTA 保持原型号；烧录流程不得擦除 NVS。有效旧 Plus 即使缺新 profile 字段，也应保留 Plus 并进入待选传感器状态；损坏配置不能被写成 Base | 新设备、旧 Base、旧 Plus 分别普通重烧/OTA/断电并读回型号及配置；另测旧 Plus 迁移、上一份有效配置恢复、损坏配置显式故障 |
| 旧 Plus 型号可能尚未落盘 | 当前 `DeviceConfigStore::load` 在 `cfg_model` 缺失时默认 Plus；`DeviceConfigStore::save` 不检查 NVS 写入结果 | 工作人员按台账/实物核实旧 Plus；过渡镜像或 USB 维护流程直接查 `scale_cal/cfg_model`，必要时写入并检查返回值、键类型和值，重启再直读；失败阻断升级。空白新板直接统一固件默认 Base。不能凭 APP 显示 Plus 判断落盘 | E8-C02：写成功/失败/读回不一致及旧 Plus 实机前后 NVS 证据 |
| Plus 待配置和兼容字段 | 蓝图允许 `PLUS + NONE`；现有 `laser_installed` 由平台型号约束，SW APP 依赖正式状态 | `BASE + NONE` 与 `PLUS + NONE` 必须区分；冻结待配置状态下 `laser_installed`、`laser_available`、故障原因和旧客户端表现，不能简单按 `profile != NONE` 派生 | 旧 SW APP 连接 Plus 待配置设备，检查开始门禁、故障提示和状态，不改旧正式字段含义 |
| OTA 镜像有效性与传感器健康 | 当前 `main.cpp` 在传感器初始化前调用 `FirmwareRollbackConfirmation::confirmIfPending`；NVS 与 APP 镜像分区不同 | 保持镜像自检与外部传感器故障分离；传感器坏、未选 profile、缺空载不能触发镜像回滚；回滚旧镜像后还要能读未损坏的配置 | E8-O01：成功/失败镜像、无传感器、PLUS/NONE、旧镜像读配置 |
| Plus 的两步设置 | 蓝图先选 Plus 再选传感器，但原协议无 `PLUS + NONE` | 允许“Plus 待选传感器”中间状态；此时不得开始 | Base→Plus 待配置→J100/CYD→Base，逐步 ACK/GET 回读 |
| 传感器可用判据 | 固件 `setEmptyDistance` 只检传入值和 backend；APP 空载设置只看最后一次 `distanceValid` | APP 确认当前 profile/代际连续有效帧；ESP32 用自己当前新鲜样本验证空载写入，旧帧/停更/跨 profile 拒绝；具体连续次数和超时要用采样率与实测冻结 | E8-M01：选错型号、无帧、单帧、停更、切换旧帧、断线重连 |
| 双开关直接降级 | 固件只支持无激光或故障后授权；当前双关不改变 start gate | 双关专用来源/字段组合，不走故障确认；不能通过 `protection_degraded=1` 或 `laser_available=0` 把 SW APP 设置入口禁用；PLUS/NONE/CONFIG_ERROR/CYD 未验收策略的优先级要冻结 | E8-S01：双开/单关/双关、缺 D_e/无距离/未选 profile，检查 START、弹窗和入口 |
| 保护界面和回读 | 正式 SW APP 的 Base 没有入口；Demo 对 Base 仍渲染保护卡片；`SNAPSHOT` 缺摔倒状态。SW SDK 等待首条 `ACK:FALL_STOP`，不核对请求值，主动补发 ACK 可能与点击竞态 | Demo 隐藏 Base 卡片；保持 SW 页面不变先验证固件兼容回显时序，重连不写默认值且两项显示真实。若无法可靠验证，明确提出最小 SW 协议/Repository 改动，不能宣称零修改完成 | E8-S02：关开关→APP 退出/重连→立即点击→设备与 UI 值对齐；冷启动两项恢复开 |
| 空载与体重模型分离 | 蓝图写 `EMPTY:CLEAR` 不清模型，当前 `LaserModule.cpp` 却同时清模型；模型坐标依赖空载参考 | 清空或改动空载距离后，体重模型字节保留与计算失效的关系；写入失败如何回滚 | CLEAR/SET 分别检查空载和模型，模型失效、断电及 NVS 失败 |
| 唯一 0 kg 定点 | 型号页独立设置 D_e，校准页仍可采 0 kg；回弹使两次距离可能不同 | 以已确认 D_e 为唯一必过 `(D_e,0)`；后续 0 kg 采样只核验容差，不生成第二个零点；一次/二次模型写入回读都验证 `y(D_e)=0` | E8-M03：0 kg 复采略偏、超差、D_e 清除/重设、模型失效 |
| 传感器切换与体重校准 | 当前模型和 APP 历史校准点没有 profile ID；Demo 有 485 采点/部署限制且采点门禁两处不一致；本地只有全局历史文件 | 点、拟合、模型绑定稳定设备身份和 profile；旧无归属历史只读；统一采点/拟合/部署门禁；历史可看但不能跨设备/profile 参与新拟合 | E8-M02：A→B 设备、J100→CYD、APP 重启，采点/拟合/部署与历史隔离 |
| BLE 配置回读与现有合同 | `SNAPSHOT` golden frame 禁止 profile，单通知预算 182 字节；APP 普通 ACK 可结束等待 | 建议独立 `DEVICE:GET_CONFIG` 返回配置详情；普通 ACK 只能触发回读，profile/generation 一致才算成功；保持 SW APP 的 SNAPSHOT 不变 | 新旧帧 golden test、长度预算、ACK 丢失/迟到、GET 不一致、断线重连 |
| 正式构建入口和工具环境 | `platformio.ini` 默认还是旧环境；统一构建上次因工具自动安装中止 | 明确统一固件为正式烧录环境，并在不擅自安装新工具的条件下证明可构建 | release build 合同、统一环境编译、固件身份和 OTA 分区核对 |
| J100 比例算法与完整运行链未落地 | 旧蓝图 §13.25 有 `R_D=|MA12_D-D_b|/|D_b-D_e|` 与 16%/28%、1500/700 ms；当前 `RhythmStateJudge` 距离分支仍用绝对距离差，离台/摔倒正式动作也需逐链核对 | 独立 D_e 和 D_b 同 profile/代际且跨度足够；无效帧/断流不进 MA12、不累计连续危险；`T_span_min_D`、空载/站稳及离台/摔倒参数待 J100 标签数据冻结 | E8-J01～J05：比例、时序、离台/摔倒正反例及恢复、空载/3 kg/5 kg/站稳；真机再证实停波和 APP 事件 |
| CYD 可通讯不等于状态算法已验收 | CYD 模块手册给出接口/标称量程，但缺本项目动作标签数据 | 本轮验收 CYD 的选择、距离、空载操作；保护开启的运行状态需另采数/回放，不借 J100 16%/28% | CYD 配置/测距 D；未来 CYD 策略另设测试，不写成 J100 通过 |

BLE 实时距离还需复核 `STREAM:SET`、`ACK:STREAM` 和当前 profile 的新鲜数据；重连后重新订阅，不能把旧的 UI 数值当作正在通信。CYD 本轮可测试选择型号、通信、距离、空载设置和体重模型操作；其空载/站稳/离台/摔倒阈值仍需单独采集数据并回放，不能把临时阈值当作已验收算法。J100 的运行状态则需要 J100 真机证据。进入真机阶段前，还要先备好 ESP32 串口、Demo BLE 和操作标记的采集命令；本轮未执行真机测试。

## 9. 复核后的实际施工边界

1. **工作人员先烧录：**新板可以以 Base 为目标；旧 Plus 若未存 `cfg_model`，必须先按设备身份过桥、直接读写 NVS 并在重启后复查。旧固件的 ACK 和 APP 显示 Plus 均不能代替落盘证据。
2. **工作人员再设置：**Demo APP 只有在设备回读 Plus/J100 或 Plus/CYD、当前 profile 持续提供新鲜距离后，才开放空载设置。切换后旧帧、旧空载和旧体重模型不可进入新工作集；历史可以保留查看。
3. **用户最后使用：**J100 要先实现并验证距离比例算法、双保护开始规则和 SW APP 两开关真实回显，才进入运行链真机验收。CYD 这次先验收配置与测距，不能因“能通讯”宣称它已能可靠判断空载、离台或摔倒。

2026-10-12 本轮完成的是文档与测试**设计**、既有代码和资料审计，以及现有 host 测试基线。尚未写入 E8 业务实现，也未构建统一固件、安装 APK、烧录或采集新真机证据。2026-10-13 的阶段 A 结果见下方追加审计；旧设备批量升级、J100 运行交付和 CYD 状态算法分别受主蓝图 §12 的分段门槛约束。

## 10. 2026-10-13 阶段 A 补充审计

具体命令、稳定 ID、出厂/过桥存储合同、空载样本所有权、状态优先级和新增测试向量，见 [阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md)。本节是审计追加，不把 2026-10-12 的预检结果追认为新测试通过。

1. **出厂：**`DeviceConfigStore::load()` 当前以 `getUChar(...,PLUS)` 判断，无键、坏类型和 NVS 打不开会混成 Plus；`save()` 未查 `putUChar` 返回，当前设置 ACK 不能证明落盘。任何旧 Base/Plus 均须按台账、键类型、写返回和重启直读过桥；新板默认 Base 时不能仅因缺键自动写入，防止抹掉漏迁移旧机的证据。[Espressif Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)
2. **Demo APP 设置：**旧固件只解析 `platform_model/laser_installed`，可能忽略额外 `sensor_profile` 后返回旧 ACK；Demo 的 `genericAck()` 当前会清除等待。新 Demo 必须先读到完整 `DEVICE:GET_CONFIG` 专用回包，再允许新 SET，并在 SET ACK 后 GET 比对；旧 GET 的 NACK 文案不稳定，不能当能力判断。现有 `CAP` golden 已占满 182/182 字节，不新增 profile 字段。
3. **身份与空载：**ESP32 已有 `DEVICE:IDENTITY?` 及 eFuse 派生 `ACK:DEVICE_ID`，无需造第二套 ID。Demo 当前全局历史缺设备/profile 归属，空载按钮使用 UI 上次显示的距离且不检查当前配置代际；新 `EMPTY:SET_CURRENT` 由 ESP32 在点击时取当前新鲜样本，APP 再 GET 确认。[Android BLE 数据传输](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)
4. **SW APP：**ESP32 断线后保留两开关值，与正式 SW APP 重连后的正确显示是两件事。Android Repository 在没有摔倒保护 GET 时投影默认“开”，SDK 等待首条 `ACK:FALL_STOP` 又不核用户请求值；固件主动 ACK 不能静态认定可靠。iOS 缺设备值时也使用默认“开”。“SW APP 零改且两项重连回显正确”列为跨端阻断；健康双关的状态字段不得伪装成激光故障，避免设置入口被禁用。
5. **证据等级：**阶段 A 新增 A-C/P/M/S/O 测试向量均未实现、未运行；未构建固件、未用 Android Studio Run、未烧录或作真机 BLE 验证。`T_fresh(profile)` 和 CYD/J100 业务数值必须从相应设备数据定值，不能用先前公开资料直接给毫米阈值。
