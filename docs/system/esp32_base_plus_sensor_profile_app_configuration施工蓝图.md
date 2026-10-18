# ESP32 Base/Plus 与 Demo APP 传感器配置施工蓝图

版本：E8-BASE-PLUS-SENSOR-PROFILE
日期：2026-10-12
最近复核：2026-10-13
状态：阶段 B **代码层收口（有限）**；Demo APP 阶段 C **离线测试通过，待 Android Studio Run 与真机 BLE 验收**；E8-C02 旧机过桥、E8-P02 完整 golden/低 MTU、E8-M 主链集成回放、旧镜像回刷与真机仍待验收
适用范围：ESP32 统一固件、Demo APP、BLE 配置与真机验收
明确不包含：CYD3S1612-IIC 的最终判断参数、传感器品牌自动识别、SW APP 页面改造

本文件是当前“新设备默认 Base、已有设备烧录或升级后保留原型号、由 Demo APP 配置 Plus 和传感器”的正式施工入口。旧蓝图中的自动探测、默认 Plus、按构建环境区分传感器等内容保留为历史审计证据；涉及本专题时，以本文件为准。

阶段 A 的具体命令、状态优先级、存储边界与失败测试以 [阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md) 为准；下文较早的“建议/待冻结”措辞按该合同修订。合同已形成不等于代码或真机通过。

截至 2026-10-13，固件配置层的实现与验证边界见 [阶段 B 进度记录](./e8_stage_b_firmware_progress_20261013.md)。Demo APP 已接入工作人员配置、空载设置和历史隔离的代码，边界见 [阶段 C 进度记录](./e8_stage_c_demo_app_progress_20261013.md)；整条新流程尚未用 Android Studio Run 和真机 BLE 验收。

## 0. 一眼看懂

### 0.1 实际操作路线（目标，不是当前已实现行为）

1. **出厂或升级：**工作人员烧录同一份固件。新 ESP32 首次上电是 Base；原来已设为 Base/Plus 的设备，普通重烧或升级后仍保持原型号。
2. **工作人员调试 Plus：**接好 J100-485 或 CYD-IIC → 在 Demo APP 的型号页选 Plus → 选实际安装的传感器 → 看见实时距离持续更新，确认通信正常 → 设置空载距离。体重校准可跳过；它不决定设备能否判断运行状态。更换传感器后，旧体重模型不用于新传感器，需要测体重时再重新校准。
3. **用户使用：**J100 Plus 在完成传感器、空载和 J100 判断算法验收后，由 ESP32 用真实距离判断状态并报告给 SW APP。两个保护都关闭时，目标是免站稳开始且不弹确认提示；ESP32 重启后两项保护默认开启。Base 没有激光判断和保护开关入口。CYD 目前只列入配置与测距验证，状态判断要另采数据、另验收。

这条设备配置与控制路线不经过服务器。Pro、Ultra 仅预留名称，本轮不实现。

### 0.2 当前走到哪里（2026-10-13 代码核对）

1. **出厂：代码已做，实物未验。**统一固件已是默认构建，空白配置运行 Base；旧 Base/Plus 有效配置可保留。旧设备仍须先核实台账、原 NVS 和必要的过桥，不能直接批量升级。
2. **调试：固件和 Demo APP 代码已接入，实物只验一部分。**ESP32 支持明确选择 J100/CYD、配置回读、按型号启动 reader、由当前距离保存空载点。旧 Plus 已在真机设置 J100 并收到距离；旧版 Demo 写入成功时主动重连导致页面闪烁，新版改为当前连接 ACK→GET 复核后重新订阅，型号/传感器写入无闪烁仍待真机验收。新版 Demo 在当前 Plus/J100 上的空载设置、清除、重设及重启/断电后回读已通过一次实物核对。
3. **用户使用：完成一部分。**Demo APP 的采样和运行曲线已改用距离；J100 距离空载/站稳链已有入口，但律动判断仍比较固定距离差，尚未用已讨论的 16%/28% 距离比例；双保护关闭免站稳尚未实现。CYD 的运行判断参数仍需单独实验。

### 0.3 接下来按同一顺序收口

1. **阶段 B 先做有限的代码层收口：**配置存储、reader 热切、旧 BLE 距离帧和配置故障停波的独立主机检查已完成；完整调用链、旧机过桥、旧镜像回刷和真机仍待验证。旧 Base/Plus 的身份清单与 NVS 过桥必须早于实体升级。全新板默认 Base 仍待设备验收。
2. **阶段 C 的 Demo APP 代码已接入：**型号页增加传感器选择；APP 发配置命令并在当前连接回读核对、重新订阅，当前传感器的有效距离达到界面门禁后才允许点“设置”保存空载值。成功写入不主动断连；失败或真实断线时清除旧实时值，不显示设置成功。离线测试与真机 BLE 结果单独记录，不用页面可见代替验收。
3. **最后验证用户运行：**J100 的距离比例判断与双保护开始规则另做回放和实测；CYD 状态算法另采数据。正式 SW APP 页面先保持不变，重连后的两个保护开关回显须专项验证。

## 1. 阶段 A 施工前启动门（历史快照）

下表记录阶段 A **当时**的施工许可边界。“当前禁止直接改代码”只约束当时的蓝图审计轮次；阶段 B 固件代码已经按冻结合同施工。最新实现和验证状态以 [阶段 B 收口记录](./e8_stage_b_firmware_progress_20261013.md)及本文件第 13 节为准。

| 项目 | 阶段 A 当时结论 |
|---|---|
| 任务类型 | 审计 + 施工蓝图 + 施工前准备 |
| 主题编号 | E8：Runtime Sensor Profile Configuration |
| 目标链路 | 工作人员操作 Demo APP → APP 发送 BLE 配置 → ESP32 校验并持久化 → ESP32 按配置选择驱动和状态策略 → APP 读取设备真相并显示 |
| 相关链路 | BLE 初始化和实时流、OTA 首启动、空载距离、体重模型、保护开关、SW APP 状态消费 |
| 真相源 | ESP32 持久化配置、ACK、SNAPSHOT、正式 EVT；APP 本地状态不能冒充设备真相 |
| 证据源 | 当前 C++/Kotlin 代码、项目协议和蓝图、传感器 PDF、Espressif/Android 官方资料、自动化测试、真机日志 |
| 本轮不触碰 | ESP32 业务算法实现、Demo APP 页面实现、SW APP、服务器、OTA 分片协议、CYD 状态阈值 |
| 当前允许 | 更新文档、补测试设计、确定协议和迁移合同、建立验收矩阵 |
| 当前禁止 | 直接改代码、删除旧入口、把自动探测改成隐式猜测、把实验参数写成固定产品参数 |

### 1.1 跨层所有权

- 工作人员：明确知道设备是 Base、Plus，以及所安装的传感器型号。
- Demo APP：展示配置页面，发送配置命令，等待 ACK 和回读，显示 ESP32 返回的实际状态。
- ESP32：保存正式配置，选择对应驱动，执行状态判断、保护和启停门禁。
- SW APP：继续只消费正式状态和控制合同，不参与传感器选择、空载设置或体重校准。
- 服务器：不参与这三个本地设备配置动作。

## 2. 用户需求冻结

### 2.1 烧录和上电

1. 生产或维修烧录使用同一份包含多种传感器驱动的统一固件。
2. 从未保存过型号的新设备首次启动时默认 Base。
3. 已保存 Base 或 Plus 的设备，普通 USB 重新烧录和固件升级后继续保持原型号；Plus 不得被自动改回 Base。
4. 工作人员需要 Plus 时，再通过 Demo APP 选择 Plus；设置成功后，重启和断电也继续保持 Plus。
5. Base 不需要激光传感器设置、空载距离设置和体重校准，连接后按原来的 Base 路径使用。

### 2.2 Plus 配置

工作人员在 Demo APP 的型号页面依次完成：

1. 选择型号 Plus；
2. 选择激光传感器：J100-485、CYD3S1612-IIC 或以后加入的受支持 profile；
3. 等待 ESP32 启动对应 reader，并确认能持续读到有效距离；
4. 设置空载距离。

摔倒保护和律动离开保护是用户通过正式 SW APP 使用的运行时控制，不属于工作人员的出厂设置步骤。用户切换后，只要 ESP32 没有重新启动，设备就保持当前开关值；APP 退出、BLE 断开重连、暂停或停止律动都不应改动它。ESP32 断电重上电或其他重新启动后，两项都默认开启。Demo APP 当前的保护卡片是已有调试入口，不能因此把它列为出厂配置项目。

只显示“已选择传感器”不算配置完成。必须有有效距离数据；没有距离数据时，APP 显示传感器通信故障，不开放正常的空载设置和运行路径。

APP 不能根据 BLE 名称、APP 名称、接口类型或一次读数猜测传感器型号。

### 2.3 两个保护开关同时关闭

这里的“两个开关”专指：

- 摔倒保护；
- 律动离开保护。

当两者同时关闭时，Plus 直接进入不要求站稳的降级状态。其开始效果与激光故障降级相似，但这是独立触发条件，不需要故障确认：

- 不要求用户先站稳；
- 不要求先形成稳定基线才能开始律动；
- start_ready 按降级合同处理；
- 不弹确认提示；
- APP 只显示 ESP32 返回的降级状态，不在本地重新判断。

当至少一个开关仍开启时，Plus 回到正常保护路径；正常路径需要有效的传感器 profile、有效测量和有效空载配置。

现有实际交互必须与上述施工目标分开描述：

1. 工作人员在阶段 C 代码中可于 Demo APP 型号页看到型号、传感器、空载距离三个设置入口；Base 只显示型号卡片，Plus 才显示后两者及保护调试卡片。设备身份和配置未回读前不能写入。保护卡片仍是调试入口，不代表工作人员要永久关闭保护；上述新交互尚待 Android Studio Run 和实物验收。
2. 普通用户连接正式 SW APP 后，Base 没有保护开关入口。健康的 Plus 在手机上需要用户主动打开顶栏设置，在平板上通过操作栏查看；连接成功不会自动弹出两个开关。当前 SW APP 只允许在空闲、就绪、暂停状态切换，运行中不能切换。
3. Plus 出现测距故障时，正式 SW APP 的设置入口不可操作，并走现有的故障提醒及授权降级链路；这不是“两个开关关闭”场景。
4. 当前固件冷启动会将两个保护设为开启，但“双关免站稳”尚未实现。施工时必须让 ESP32 决定 `start_ready`，APP 只依据设备结果显示和操作；不得把本节目标写成现有行为。

### 2.4 体重模型边界

- 空载距离是运行状态判断所需的独立配置。
- 体重模型 a/b/c 只决定体重是否可计算和显示。
- 没有体重模型时，距离仍可上报、采样和用于状态链；体重字段标记为未校准。
- 任意已选择且能稳定输出有效距离的传感器都可以建立体重模型；模型只用于体重计算和显示，不参与运行状态判断。
- CYD3S1612-IIC 的体重精度仍需实测评估，但精度风险不改变“可以建立模型”的功能边界，也不影响距离状态链。

## 3. 施工前代码审计快照

下表是阶段 A 施工前的差距快照，用来追溯为何开工，**不是**阶段 C 后的代码现状。阶段 B/C 的实际进度分别看第 11、13 节及对应进度记录。

| 审计项 | 当前事实 | 对本方案的影响 |
|---|---|---|
| 固件默认画像 | DeviceConfigStore 缺省和非法值回退为 PLUS，DeviceConfigSnapshot 也默认为 PLUS + laser=true | P0：仅无已保存配置时默认 Base；有效 Base/Plus 配置经 USB 重烧或 OTA 保持原值 |
| Demo 默认选择 | UiState 和 DemoViewModel 初始选择仍是 Plus/有激光 | P0：APP 初始显示必须反映设备回读，不用本地 Plus 默认覆盖设备 |
| PlatformIO 正式目标 | default_envs=esp32s3，统一环境 esp32s3_unified 需要手动指定 | P0：明确统一固件为生产入口，旧环境只作维护/回退 |
| 传感器 profile | LaserSensorProfileId 目前只有 WANG_WTX_J100_485 | P0：增加稳定 profile ID 和 CYD profile，不只改 UI |
| 统一固件选择方式 | 统一环境上电独立探测 IIC/485，并可能产生 conflict/unavailable | P0：正式运行改为读取持久化 profile；自动探测只能保留为诊断或测试 |
| 配置协议 | DEVICE:SET_CONFIG 目前只收 platform_model 和 laser_installed | P0：增加 sensor_profile；旧字段兼容值须结合 `PLUS + NONE` 与 SW APP 消费方式单独冻结 |
| 空载距离 | 已有 EMPTY:GET/SET/CLEAR 和设备回读，不依赖体重模型 | 可复用；需要绑定 profile、版本和迁移状态 |
| 距离/体重链 | 当前代码已允许距离状态链独立于体重有效性 | 保留；体重未校准不能阻塞距离链 |
| 保护开关 | 单个开关关闭目前只把对应动作降为 warning，不改变 start gate | P0：增加“两者同时关闭”的降级条件 |
| 正式 SW APP 保护入口 | Base 无入口；Plus 手机需主动打开顶栏设置，平板经操作栏；故障时入口不可操作，运行中也不允许切换 | 沿用实际交互，不构造工作人员运行中改开关或故障后改开关的流程 |
| Demo 型号页 | 当前有型号、空载距离、保护开关，暂无独立传感器选择卡片；Base 也会渲染保护卡片 | P0：加入传感器设置卡片；按产品规则隐藏 Base 的保护卡片 |
| 保护状态回读 | 当前 `SNAPSHOT` 带律动离开开关状态，未带摔倒开关状态；正式 SW APP 重连时摔倒开关可能显示本地默认“开”，设备却仍是“关”；若把双关误报为旧故障降级，还会禁用设置入口 | 两项值都需设备证据。优先检验不修改 SW APP 的固件兼容回显是否可靠；若不可靠，必须显式列为跨仓合同问题，不得宣称零改动已验收 |
| Demo 体重校准 | 当前采点门禁一处只允许 LASER_485，另一处重算漏掉该限制；部署仍只允许 LASER_485 | 统一门禁为当前 profile 有新鲜有效距离、空载点已确认；体重模型不参与状态判断 |
| Demo 历史归属 | 校准点和采样历史存在单个本地文件，尚无稳定设备 ID/profile 归属 | 旧历史标记为未归属，不自动作为新设备或新 profile 的拟合输入；先定义稳定设备身份回读 |

代码证据：

- DeviceConfigStore.cpp:6
- DeviceConfig.h:104
- platformio.ini:11
- LaserMeasurementReader.cpp:18
- LaserSensorProfile.h:5
- ProtocolCodec.h:168
- RuntimeProtectionPolicy.cpp:3
- MainScreen.kt:320
- LaserModule.cpp:274

## 4. 目标架构

~~~text
统一固件烧录或升级
      ↓
读取已保存配置：有则保持 Base/Plus；从未设置过则默认 Base
      ↓
Demo APP 型号页
      ├─ 型号：Base / Plus
      ├─ 传感器：None / J100-485 / CYD3S1612-IIC / future profile
      └─ 空载距离：设置 / 清除 / 回读
      ↓
ESP32 持久化运行画像
      ↓
按画像选择传感器驱动和状态策略
      ↓
SNAPSHOT、EVT:STATE、EVT:SAFETY、EVT:STOP、EVT:STREAM
      ↓
Demo APP / SW APP 各自消费正式字段
~~~

### 4.1 运行画像

正式画像至少包含：

~~~text
platform_model       BASE | PLUS | PRO | ULTRA
sensor_profile       NONE | J100_485 | CYD3S1612_IIC | ...
transport_backend    NONE | UART_485 | IIC | ...（驱动内部属性，不作型号 ID）
laser_installed      兼容字段：BASE=0，PLUS=1（含 PLUS + NONE）
empty_distance       value + tolerance + profile_id + generation + empty_revision
weight_model         status + profile_id + generation + empty_revision + model_revision + coefficients
config_schema
generation
commit_marker
crc
~~~

规则：

- BASE 强制 sensor_profile=NONE、laser_installed=0。
- PLUS 必须选择一个已内置的传感器 profile 才能进入正常测量路径。
- `PLUS + NONE` 表示已选 Plus、尚未选传感器，与 `BASE + NONE` 不同；兼容字段 `laser_installed=1` 表示 Plus 预期安装激光，不表示已选具体型号或测距可用。该中间态不能正常开始。
- empty_distance 必须带 profile 身份；切换 profile 后旧值不能自动复用。
- weight_model 必须带 profile 身份；切换 profile 后旧模型不能用于新 profile。
- `sensor_profile` 标识受支持的具体型号与判断策略；`transport_backend` 只标识电气/驱动接口。同为 UART 或同为 IIC 的新型号也必须有独立 profile，不能共用旧判断参数。
- 配置写入采用“校验 → 写临时记录 → 校验读回 → 写提交标记 → 切换 active generation”的事务顺序。
- `fall_stop_enabled` 和 `leave_stop_enabled` 是与上述持久化画像分离的运行时状态：ESP32 未重新启动时保持当前值，APP 退出或 BLE 断开重连不复位；ESP32 重新启动后都默认开启，不从 NVS 恢复上次的“关闭”状态。Plus 的 APP 设置界面须用设备回显核实；当前 SW APP 未实现摔倒开关的可靠重连回读，施工不得把本地默认值当成设备真相。Base 不提供保护开关入口。

### 4.2 运行模式

| 模式 | 条件 | 开始门禁 |
|---|---|---|
| BASE_IMMEDIATE | Base + 无传感器 profile | 连接后按 Base 原逻辑 |
| PLUS_NOT_CONFIGURED | Plus 但 profile 未设置或不受支持 | 不允许正常开始，提示先设置传感器 |
| PLUS_NORMAL | Plus + 已验收的 profile 状态算法 + 传感器健康 + 空载距离有效 + 至少一个保护开关开启 | 需要正常距离状态和稳定基线；本轮目标先验收 J100 |
| PLUS_POLICY_PENDING | Plus + 传感器可通讯，但该 profile 的状态算法尚未完成验证（例如当前 CYD），至少一个保护开关开启 | 可调试距离和空载；`start_ready=0`、拒绝 WAVE:START，不能把临时阈值当受保护运行 |
| PLUS_DEGRADED | 健康、已选 profile 且 D_e 已设置的 Plus + 两个保护开关同时关闭 | 不要求站稳/基线，直接允许开始，不弹旧故障确认；保持真实的健康字段和可操作的设置入口，已有故障锁定/暂停优先；CYD 此路径单列待验收 |
| PLUS_FAULT_LOCKED | Plus + profile 有效，但保护仍开启且传感器故障/空载缺失 | 保持未就绪，按正式原因码提示 |
| CONFIG_ERROR | 配置损坏且无法恢复上一份有效记录 | 保留原始记录供排障；禁止 Plus 测距和开始，上报配置故障，等待明确修复 |

旧版有效 Plus 配置缺少新字段时属于迁移，不属于 CONFIG_ERROR：保留 Plus 型号，迁移为 `PLUS + NONE` 待选传感器，不能自动变成 Base。配置损坏时优先恢复上一份有效记录；无法恢复也不能写入 Base 覆盖原记录。

`PLUS_DEGRADED` 是用户关闭两项保护后的独立运行规则，不能冒充激光故障降级：特别不能仅因双关就设置会使 SW APP 隐藏保护入口的故障字段，也不能触发旧故障确认弹窗。配置损坏、`PLUS+NONE` 优先阻断；健康、配置完成的 J100/CYD 双关可绕过站稳，但 CYD 此路径尚未实物验收，不能把它标成保护算法已通过。真实测距故障与双关的交互仍需分别联调。完整优先级见阶段 A 合同 §6。

## 5. 从烧录到运行的完整流程

### 5.1 烧录或升级后上电

1. ESP32 读取配置 schema、generation、commit marker 和 CRC。
2. 有已保存且有效的型号配置时，继续使用原型号：Base 仍是 Base，Plus 仍是 Plus；USB 普通重烧和 OTA 均不得主动清除或覆盖该配置。
3. 经生产确认的全新空白板从未设置过型号时，在运行中使用 Base 默认画像；不要仅因缺键而自动写入 Base，以保留漏做过桥的旧设备证据。明确出厂确认或工作人员设置时才落盘。
4. 工作人员在 APP 选择 Plus、传感器和空载距离后，ESP32 保存这些设置；重启、断电、USB 普通重烧和 OTA 后继续使用。
5. 如果旧固件只有 Plus/激光开关记录，迁移时保留 Plus，但不猜 J100 或 CYD；APP 要求工作人员重新选择传感器。
6. APP 连接、完成 GATT 服务发现和通知订阅后，读取设备配置并显示 ESP32 的实际结果。

整片擦除会删除 NVS，不能作为普通重烧或升级步骤；确需整片擦除时，应先备份并恢复设备配置，否则无法保证“原来是什么，烧录后仍是什么”。配置损坏另报明确故障，不得把正常旧 Plus 配置误判为首次使用。

旧固件可能在没有 `cfg_model` 存储键时直接默认运行 Plus，且当前保存代码没有检查写入结果。这样的旧设备与全新空白设备，仅凭新固件启动时的“无型号记录”无法区分。**任何旧设备**升级的过桥步骤：① 按实物/台账确认设备身份和原 Base/Plus；② 用过渡镜像或 USB 维护流程直接检查 `Preferences.begin("scale_cal")` 成功、`cfg_model` 键存在、类型为 `PT_U8` 且值为 0/1；**覆盖前**保存设备 ID、固件版本、原键类型和值及必要备份，缺键/坏键时写入经核实的原型号，要求 `putUChar==1`、类型和值读回一致；③ 断电重启再次直接读 NVS，保存读写日志；④ 全部通过后才用保留 NVS 的普通 USB/OTA 升级，确认仍为原型号，再配置传感器。NVS 打不开则隔离，不写入；合法预留值 Pro/Ultra 不是坏值，保留证据另核实，不改成 Base/Plus。旧 APP ACK、重启后显示的型号或传感器读数均不能证明已落盘。全新空白板需生产确认后才按 Base 默认启动。

### 5.2 工作人员设置 Plus

1. APP 先取得当前设备 `DEVICE:IDENTITY?` 身份和完整 `DEVICE:GET_CONFIG` 回包，未证明支持新配置时不开放新流程；工作人员再在型号卡片选择 Plus。
2. APP 发送新设备配置命令。
3. ESP32 校验型号和 profile 组合，成功保存后返回 ACK。
4. APP 用独立的 DEVICE:GET_CONFIG 回读型号与 generation；CAP? 和 SNAPSHOT? 仍用于确认原有能力及运行状态。
5. 只有设备回读确认成功，APP 才开放传感器选择。

施工前须补齐 Plus 已选择、传感器尚未选择的中间状态（建议为 PLUS + NONE）：允许保存并回读，但必须显示“待选传感器”，不得进入正常测距和开始路径。

### 5.3 工作人员设置传感器

1. APP 展示已内置 profile：J100-485、CYD3S1612-IIC。
2. 工作人员按实际接线选择，不依赖自动识别。
3. APP 发送 profile。
4. ESP32 先拒绝运行中切换，停止旧 reader，撤销旧距离可用/站稳状态并增加数据代际；旧任务剩余帧不得进入新 profile 的判断链。
5. ESP32 校验并保存新 profile，检查写入结果和读回值；写入失败保持旧有效配置、重启旧 reader、撤销旧 readiness 直到旧 profile 的新鲜样本重新确认，并返回 NACK。成功后启动对应通信参数、距离换算和独立状态策略，旧 profile 绑定的 D_e 与体重模型不得自动复用。
6. ESP32 返回 profile ACK；APP 在同一 BLE 连接内再用独立 DEVICE:GET_CONFIG 核对设备身份、profile 与 generation，不主动断开连接。确认后撤销旧距离/旧订阅的资格，显式重新订阅实时流并刷新能力与运行快照；之后才观察本次 profile 的新鲜距离。ACK 不能代替回读，切换前的排队帧不能充当新传感器样本。连接意外中断时按正常重连流程重新读取设备真相。
7. APP 只有收到当前连接、当前 profile、当前数据代际的连续新鲜有效距离后，才显示“传感器可用”并开放空载距离设置；断流超时撤销可用。阶段 C 代码暂以连续 2 个有效样本、3 秒断流窗口作为**开发测试门禁**，工作人员可以在测试设备上操作空载设置；ESP32 处理 `EMPTY:SET_CURRENT` 时还须独立检查当前样本。`T_fresh(profile)`、连续次数和断流窗口的**正式工程参数**仍须依据 J100/CYD 各自 reader 周期与真机数据定值并回放，未完成前不能把开发测试结果当作产品验收；不能凭一次旧帧设为空载。阶段 A 冻结的是判据和数据来源，不是 CYD 最终数值。

### 5.4 工作人员设置空载距离

1. APP 打开空载距离卡片，持续显示 ESP32 的实时 distance_mm。
2. 用户确认平台空载后点击“设置”。
3. APP 发 `EMPTY:SET_CURRENT`；现有 `EMPTY:SET distance_mm=...` 留作旧客户端兼容，但 APP 缓存的数值不能单独成为写入依据。
4. ESP32 只从当前 profile 与数据代际取新鲜有效实测样本，校验运行状态和量程；样本过期或 profile 不同即拒绝，不能把旧帧写成 D_e。旧 `EMPTY:SET distance_mm` 在新 profile 模式直接拒绝并要求新 Demo，不参与此步骤。
5. ESP32 保存 profile 绑定的 D_e，检查写入/读回成功后返回 ACK。
6. APP 再发 GET，只有 GET 与写入值一致才显示“已设置”。
7. 点击“清除”只清除 D_e，不清除型号、传感器 profile 或体重历史。

## 6. 协议设计

### 6.1 保持不变的合同

以下合同默认冻结：

- GATT UUID、RX write、TX notify、换行分帧；
- CAP?、SNAPSHOT?、WAVE:SET、WAVE:START、WAVE:STOP；
- STREAM:SET、ACK:STREAM 和 EVT:STREAM 的订阅语义；
- OTA 分片、双分区、回滚和 SW APP 正式状态字段。

### 6.2 新增或扩展的设备配置

阶段 A 已冻结以下新命令和稳定 ID；阶段 B 固件端及阶段 C Demo APP 端已接入代码，协议与离线测试的已通过范围及剩余真机门槛见各阶段进度记录：

~~~text
DEVICE:SET_CONFIG platform_model=BASE,sensor_profile=NONE
DEVICE:SET_CONFIG platform_model=PLUS,sensor_profile=NONE
DEVICE:SET_CONFIG platform_model=PLUS,sensor_profile=J100_485
DEVICE:SET_CONFIG platform_model=PLUS,sensor_profile=CYD3S1612_IIC
DEVICE:GET_CONFIG
~~~

独立的旧版配置命令兼容路径仍使用：

~~~text
laser_installed=<0|1>
~~~

但正式真相改为 sensor_profile：

- laser_installed 是兼容字段；`BASE+NONE` 为 0，`PLUS+NONE` 和已选 profile 的 Plus 为 1，不能从 profile 是否为 NONE 单独推导；
- APP 不以 laser_installed=1 推测具体型号；
- `BASE + NONE` 表示 Base 无传感器，`PLUS + NONE` 表示 Plus 待选传感器；
- profile 不受支持时返回 `NACK:DEVICE_CONFIG op=SET reason=UNSUPPORTED_PROFILE`；对不认识 `sensor_profile` 字段的旧固件，Demo APP 必须识别能力缺失并停止新配置流程，不能因通用 ACK 就显示设置成功。

新配置回包（旧客户端的无 `op` 回包仍保留）：

~~~text
ACK:DEVICE_CONFIG op=SET schema=1 platform_model=PLUS sensor_profile=J100_485 laser_installed=1 generation=12
ACK:DEVICE_CONFIG op=GET schema=1 platform_model=PLUS sensor_profile=J100_485 laser_installed=1 generation=12
DEVICE:IDENTITY?
ACK:DEVICE_ID id=sw1_<32位小写十六进制> version=1
~~~

稳定 ID 复用现有 eFuse 派生身份命令，配置 GET 不增加另一份 `device_id`。它用于 Demo APP 区分不同物理 ESP32 的校准点和采样历史，不以可能改变的 BLE 展示名或临时连接地址替代。旧设备既存无归属历史只读，不能自动进入当前拟合。旧固件可能忽略新增 `sensor_profile` 却返回 ACK；新 Demo 必须先收到完整 `op=GET schema=1`，新 SET 不带旧 `laser_installed`，且 SET ACK 后再次 GET 一致才算成功；详情见阶段 A 合同 §4。

### 6.3 空载和体重模型合同

保留已有独立语义：

~~~text
EMPTY:GET
EMPTY:SET_CURRENT
EMPTY:SET distance_mm=<finite>
EMPTY:CLEAR
CAL:GET_MODEL
CAL:SET_MODEL ...
CAL:CLEAR_WEIGHT_MODEL
~~~

新增约束：

- 新 `EMPTY:SET_CURRENT` 必须在旧 `EMPTY:SET` 前按完整命令边界解析；新版空载回包含 sensor_profile、状态、配置 generation 和 empty_revision，只有 SET 状态才包含实测距离及容差，CLEAR/UNSET 不伪造数值；帧预算与旧客户端组合须单测。旧 `EMPTY:SET distance_mm` 在新 profile 管理模式拒绝并要求新 Demo，在旧模式保持既有语义，不能暗改旧协议又称兼容；
- CAL:* 的回包必须包含模型所属 profile；
- EMPTY:CLEAR 不得清除体重模型；
- CAL:CLEAR_WEIGHT_MODEL 不得清除空载距离；
- EMPTY:CLEAR 或修改 D_e 后，旧体重模型虽可保留记录，但必须停止参与计算和上报，直到在新 D_e 下重新校准并写入有效模型；
- 运行中拒绝配置清除和 profile 切换；
- BLE 断开不会清除设备配置。

### 6.4 正式状态字段

配置详情由独立的 DEVICE:GET_CONFIG 回包提供，不能直接塞入现有连接 SNAPSHOT。现有 CAP golden 帧已占满 182 字节单通知预算，SNAPSHOT 也保持原有 golden frame；若确需改正式状态帧，须作为另一轮协议变更审计 SW APP。

Demo APP 至少需要从设备回读以下配置与运行事实（分属配置回包、现有 SNAPSHOT 和实时测量，不要求在单帧内全部出现）：

~~~text
platform_model
sensor_profile
laser_installed
laser_available
empty_distance_state
weight_model_state
runtime_ready
baseline_ready
start_ready
degraded_start_available
degraded_start_enabled
degraded_reason
~~~

字段原则：

- 稳定枚举用于程序判断；
- 品牌/型号显示名只用于界面和诊断；
- warning/debug/compat 字段不得被 APP 升级成正式安全分类；
- APP 不根据缺少体重模型推断传感器故障；`ACK:CAP stream_control_supported=1` 仍只表示能力，真实订阅须看 `ACK:STREAM enabled=1` 或设备执行日志。

## 7. 固件施工方案

### 7.1 配置存储

1. 经确认的新板无型号键时运行默认 Base，不因启动自动写入 Base；有效的已存 Base/Plus 配置必须保持原型号。任一旧机缺键/坏键须先过桥，不直接升级。
2. 增加 sensor_profile、schema、generation、commit marker、CRC。
   配置 generation 只随型号/profile 的实际变化递增；D_e 和模型各有独立修订号，D_e 更新使旧模型失效，不把一次设置 D_e 当成 profile 切换。
3. 旧配置过桥时保留原始证据；新配置写入保持旧 `cfg_model/cfg_laser` 兼容镜像，不能假设旧/新记录跨键原子提交。
4. CRC 损坏或写入中断时，才考虑恢复与旧兼容键一致的上份有效 generation；完整有效但当前镜像不认识的 schema/profile 必须保留并禁开始，不能自动退回旧 profile。无可恢复记录、跨键不一致或非法组合则进入 CONFIG_ERROR，禁止开始并保留原始证据，不写入 Base 覆盖旧配置。有效旧版 Plus 记录走迁移路径，保留 Plus 型号。
5. profile、空载距离、体重模型分开保存，不能用一个 zero/factor 或首个读数代替。
6. 正式配置跨重启、断电和普通 OTA 保持；显式清除才删除对应项。
7. 任一旧设备 `cfg_model` 缺失/坏类型/未知值或 NVS 打不开时先执行 5.1 的过桥/阻断；旧镜像读取所依赖的键在 OTA 可回滚期内必须保持可读。新镜像写新 schema 后用**受控回刷旧镜像**测试旧键可读，不能把它写成自动 OTA 回滚，更不能凭旧键可读就宣称旧镜像能安全运行新 profile。

### 7.2 传感器 reader

1. 统一固件内置 J100-485 和 CYD-IIC 两个 reader。
2. reader 由持久化 sensor_profile 选择。
3. 取消自动探测作为正式选择路径。
4. 可保留独立 probe 诊断命令，但 probe 结果不能偷偷改写正式 profile。
5. J100 继续在 ESP32 转换为真实 distance_mm：D = 100 - displacement。
6. CYD 继续按其绝对毫米输出，使用独立有效范围、启动等待和质量原因。
7. 两个 profile 的空载容差、距离范围、采样窗口和状态参数独立保存。
8. Base 启动不等待 J100/CYD 探测，也不因传感器缺席报故障；配置为 Plus 后才启动所选 reader。传感器 profile ID 与 UART/IIC 后端类型分开；同总线的新型号不继承旧型号的算法。
9. profile 切换以“拒绝运行中切换 → 屏蔽旧任务和帧 → 撤销旧 readiness → 存储与回读 → 启动新 reader → 当前代际新样本确认”为顺序；**存储/读回失败**时保留上一份有效配置、重启旧 reader、旧样本重新确认后才恢复 readiness，并 NACK；存储成功但新传感器无信号时保留新 profile、报告通信故障并禁止正常开始，方便工作人员查接线。不能 ACK 成功却仍读旧 reader。

### 7.3 Plus 状态门禁

1. Base 直接沿用无激光路径。
2. Plus 正常路径使用选定 profile 的距离状态链。
3. Plus 缺 profile 时不允许正常开始，两关也不绕过；缺 D_e 或传感器故障时，保护开启则保持未就绪。CYD 保护算法未验收，至少一个保护开启时 `start_ready=0`、拒绝 `WAVE:START`；可调试距离和空载。
4. 健康、已选 profile 且有 D_e 的 Plus 两个保护开关同时关闭时，由 ESP32 直接启用免站稳开始，不要求 APP 确认；激光故障原有授权路径单独保留。线上 SNAPSHOT 仍报告真实测距健康、`start_ready=1` 与 `degraded_start_available=0/degraded_start_enabled=0`；不伪造内部基线/在位事实。真实故障与双关的交互、SW 页面回显仍是专项联调阻断。
5. 单独关闭一个开关只影响对应保护动作，不能误判为组合降级。
6. APP 以设备正式状态为准，不本地计算开始按钮；健康双关时不把 `degraded_start_*` 当旧故障授权提示。
7. 双关免站稳仅是 ESP32 待实现的开始门禁规则；配置错误、`PLUS+NONE` 与已有锁定故障/暂停不得被双关清除。任何一项保护重新打开，豁免立即撤销，未重新站稳时不得直接继续。正式 SW APP 运行中和测距故障时不能操作保护开关，不把这两种动作写成用户流程；`PLUS+NONE`/配置故障对旧 SW 的精确字段投影待专项联调。

#### J100 律动期的目标公式（当前代码尚未实现）

独立设置的空载距离 `D_e`、开始前站稳基线 `D_b`、律动期最近最多 12 次**有效状态判断样本**平均值 `MA12_D`，均使用 J100 profile 转换后的真实距离、同一单位和同一配置代际。现有体重分支以 1～12 个有效样本渐进形成 MA12；距离版先保持相同窗口和跨 16%/28% 的计时状态转移，必须用逐步喂样本及跨界回放证明，不能把 20 ms 串口轮询的 12 帧误作 12 次状态调用：

~~~text
跨度 S_D = |D_b - D_e|
当 S_D ≥ T_span_min_D 时：R_D = |MA12_D - D_b| / S_D
~~~

`R_D ≤ 16%` 为正常；`16% < R_D ≤ 28%` 为可恢复异常，持续 1500 ms 仍异常则进入危险候选；`R_D > 28%` 为直接危险，危险连续 700 ms 请求停波；异常满 1500 ms 后仍未恢复，再连续危险 700 ms 请求停波。无 D_e、D_e/D_b 所属 profile 或代际不同、非有限值、样本无效、`S_D < T_span_min_D` 时均不得计算比例，更不能按比例零判为正常。`T_span_min_D` 的毫米值及律动中跨度不足时的停波/故障原因，仍需 J100 逐台数据和状态合同确定，不能由公式猜出。短暂断流后的窗口、1500/700 ms 计时器如何重置，以及 >28% 与 16～28% 反复跨界时危险计时是否延续，均以既有体重分支状态转移为基线并用回放测试锁定，不能把断流时间累计成连续危险时间。

当前 `RhythmStateJudge` 距离分支仍用绝对距离差，J100 传入的是固定绝对阈值；上述公式属于**待施工目标**，不是已实现行为。当前独立 `EMPTY` 记录已能注入距离状态链；旧统一蓝图中“首个有效样本临时作为空载点”的现状描述已经过期，不得重新实现。CYD 的状态参数和判断策略另做实验，不能复制 J100 的 16%/28%。

### 7.4 体重模型

- 选定传感器的体重模型由 Demo APP 拟合、ESP32 保存和回读；
- 独立设置的 `D_e` 是体重模型**唯一**必过的 0 kg 定点 `(D_e, 0)`；校准页后来记录的 0 kg 样本只核验回弹误差/重复性，不另设第二个零点，也不静默覆盖 D_e；超出当前传感器容差时提示重新检查空载设置；
- 模型坐标使用 `u = distance_mm - D_e`，一次 `y=k·u`、二次 `y=a·u²+b·u`，两者在 `u=0` 都为 0；若设备仍以绝对距离的 `a/b/c` 保存，写入时须换算并回读验证 `y(D_e)=0`；
- 没有有效模型时输出 weight_valid=0，但保留有效距离和距离状态；
- 切换 profile 后不能继续使用另一个 profile 的模型；
- 体重模型失败不应把 distance_valid 改成 false。

### 7.5 OTA

沿用当前 OTA 镜像确认边界：

~~~text
启动 → 固件自身启动自检 → 确认或回滚待验证镜像
→ 读取/迁移设备配置 → 初始化 reader 与 BLE → 上报运行状态或配置/传感器故障
~~~

外部传感器无信号、未选 profile 或空载距离缺失属于设备运行/配置故障，不能直接当作新镜像损坏而触发 OTA 回滚。当前固件在初始化传感器之前确认镜像；若未来扩大镜像自检范围，须单独审计回滚条件，避免坏传感器使正常固件反复回滚。OTA 只回滚应用镜像，不自动恢复本轮改写的 NVS 配置；因此须验证新旧镜像共用配置时的兼容性和失败恢复。工作人员能写新配置时自动回滚窗口已经结束；“新配置写后回刷旧镜像”是受控降级测试。旧镜像能读原型号不代表它理解新 profile 或可安全运行，须按旧镜像与实物组合另验。

## 8. Demo APP 施工方案

### 8.1 型号页面

固定顺序：

1. 型号设置；
2. 激光传感器设置；
3. 空载距离设置；
4. Plus 的保护开关调试区（不属于传感器安装必填步骤）。

显示规则：

- Base：只显示型号卡片；传感器、空载和保护调试卡片不显示，也不显示激光故障；
- Plus 未选 profile：传感器卡片可用，空载卡片显示但“设置/清除”不可用；
- Plus 已选 profile 且**当前连接已确认实时流订阅、连续收到本 profile 的新鲜有效距离**：显示距离可用，开放空载“设置”；ESP32 仍在执行 `EMPTY:SET_CURRENT` 时独立判断当前样本是否有效；
- Plus 已选 profile 但没有有效距离：先显示等待数据，超时或无效帧显示通信故障，不开放空载“设置”；正常运行门禁仍由 ESP32 决定；
- 回读未知：显示“等待设备确认”，不显示已设置；
- 切换 profile：旧 D_e 显示过期，要求重新设置。

### 8.2 体重校准页

- 页名改为“体重校准”；
- 顶部显示适用设备、模型状态、模型类型、a/b/c、generation；
- 对所有已选定、当前距离持续有效且 0 kg 空载点已确认的传感器开放采点和写入；采点、拟合、部署三处使用同一门禁；
- 传感器没有有效距离时，校准页不能记录校准点或写入模型；
- 参数归零只清 a/b/c；
- 型号页已确认的 D_e 是拟合必须经过的唯一 `(D_e,0)`；在校准页另记的 0 kg 距离只检查其与 D_e 的偏差，不能新增第二个不同的零点；
- 没有模型时不显示伪造体重；
- 写入后必须 GET 回读并比对；
- APP 本地持久化只保存历史和草稿，不冒充设备当前模型。
- 校准点、拟合结果和采样历史应按稳定设备身份与 profile 隔离。现有无归属的本地文件只作历史查看，不能在连接一台新设备时自动进入当前拟合工作集；BLE 显示名不能充当稳定身份。

### 8.3 采样、运行、日志页

- 采样曲线和导出主列为 distance_mm；
- 运行遥测和图像使用距离；
- 体重可以显示为参考列，模型缺失时为空并带原因；
- 运行按钮只跟随 ESP32 状态；
- 日志保留命令、ACK、回读、状态变化和原因码；
- EVT:STREAM 必须经过显式订阅，不能根据 APP 名称自动推断。

### 8.4 SW APP

本专题目标是不修改 SW APP 页面和日常操作：

- 不增加传感器配置；
- 不增加空载距离设置；
- 不增加体重校准；
- 只继续消费正式状态和控制字段。

但当前 SW APP 连接重建时没有摔倒保护的正式 GET 回读，可能把本地默认“开”显示给用户。固件若主动补发旧 `ACK:FALL_STOP`，属于把命令回包用作状态推送；目前 SDK 等待第一条此类 ACK，不核对它是否与刚发送的命令对应，所以不能只凭代码推断这一捷径可靠。阶段 A 先做时序和竞态专项测试：连接握手时读到真实两项值、用户随即点击仍只认对应 ACK、重连不写默认值、双关后设置入口仍可操作。若固件兼容方案不能通过，必须单独说明 SW APP 最小协议/Repository 回读修改需求，不能报告“SW APP 零改动且回显正确”。不得将双关伪装成 `protection_degraded=1`、`laser_available=0` 或旧故障确认原因，导致正式 APP 禁用开关入口或弹出确认。

## 9. 测试与审计准备

### 9.1 静态审计

下列是原施工前完整检查表；勾选表示对应条目**整体**通过，不能因为阶段 B 的部分方法测试通过就勾选。当前分项证据见 [阶段 B 收口记录](./e8_stage_b_firmware_progress_20261013.md)。

- [ ] 仅从未保存型号的新设备默认 Base；有效旧 Base/Plus 的读取和迁移保持原型号；
- [ ] 旧固件默认运行 Plus 但 `cfg_model` 缺失的设备，在升级前直接校验 NVS 键、写入结果和读回值；不能仅凭重启后显示 Plus 就当作已经落盘；
- [ ] platformio.ini 正式烧录目标明确为 unified；
- [ ] 自动探测不再写入正式 profile；
- [ ] sensor_profile 在 C++、协议、Kotlin 与**独立配置 ACK/GET** 中含义一致；现有 SNAPSHOT 不加 profile，保持旧 golden frame 和单通知预算；
- [ ] Base 不依赖 D_e、体重模型或激光健康；
- [ ] Plus 两开关组合条件只在 ESP32 owner 中判断；
- [ ] 单个开关关闭仍只是对应保护动作变化；
- [ ] 空载距离和体重模型可独立清除；
- [ ] profile 切换不会复用旧空载或旧模型；
- [ ] BLE、OTA、SW APP 正式字段未被静默改名；
- [ ] 旧配置迁移不会把 Plus 自动猜成 J100；
- [ ] 配置经独立回读确认，现有 SNAPSHOT 的运行真相和 182 字节预算保持兼容；
- [ ] 无效距离、短读、通信错误不会进入状态链。
- [ ] Base 启动不探测激光、不因无传感器报故障；CYD 能通讯不等于 CYD 状态策略已验收；
- [ ] J100 使用独立 D_e、稳定 D_b 和 MA12 比例式；低跨度/断流不会伪造正常或跨 profile 累积危险计时；
- [ ] SW APP 重连后两项保护值有设备证据，双关状态不禁用用户重新打开开关的入口。

### 9.2 单元测试

固件：

- 配置缺失、非法、CRC 错误、generation 回退；
- 旧 Base/Plus 配置迁移；旧 Plus 缺少 profile 时仍为 Plus 待配置，损坏配置不被静默覆盖成 Base；
- 新设备默认 Base，以及已配置 Base/Plus 经普通 USB 重烧、OTA 和断电后保留原型号及有效配置；
- 旧固件默认 Plus 且缺 `cfg_model` 的设备先校验 NVS 键并持久化 Plus 再升级；跳过迁移准备时测试明确阻止交付；
- Base、Plus/J100、Plus/CYD 约束；
- profile 切换后 D_e 和模型过期；
- 两个保护开关组合；
- 两个开关关闭后，APP 退出、BLE 断开重连、暂停和停止律动均不复位 ESP32 当前开关值；APP 重连不得主动写入默认值；
- 冷启动后两个保护开关都恢复开启；Plus 设置页完成可靠回读后显示设备实际值，回读前不伪装成已确认；
- 单个保护开关关闭不触发 degraded-start；
- 缺 D_e、传感器故障、体重模型缺失；
- 配置写入中断和回读不一致；
- OTA 首启动自检成功/失败。

协议模块：

- 新配置命令解析；
- 旧 laser_installed 兼容；
- ACK/NACK 及原因码；
- CAP、SNAPSHOT、EVT:STREAM golden frames；
- profile、D_e、模型 generation 一致性。
- Plus + NONE 中间状态；当前 profile 新鲜距离的连续确认、超时和旧帧隔离；
- 双保护关闭直接启用且不触发故障确认，单开关关闭及恢复开启；
- EMPTY:CLEAR/SET 对体重模型的失效和 NVS 失败行为；

Demo APP：

- 型号页显示和禁用规则；
- profile 写入后 ACK + GET 回读；
- 普通 ACK 不当作最终配置成功，回读 profile/generation 不一致时继续显示未确认；
- 切换传感器后旧距离、旧校准点不能参与当前设置或拟合，断流后撤销“传感器可用”；
- D_e 设置/清除；
- profile 切换后显示过期；
- 两开关同时关闭只接受 ESP32 degraded truth；
- 体重模型缺失时距离仍显示；
- 进程重启后本地历史保留但设备事实重新回读；
- 第二次记录、拟合、写入、回读不闪退。

#### 9.2.1 最小可重复测试包（施工时逐项实现，当前均未作为 E8 通过）

`H`＝无设备固件 host / Android JVM，`P`＝协议 golden fixture，`D`＝真机串口＋BLE＋界面，`O`＝OTA 分区/回滚证据。H/P 通过不能代替 D/O。

| ID | 输入或操作 | 必须断言 | 证据层级 |
|---|---|---|---|
| E8-C01 | 空白 NVS、旧 Base、有 `cfg_model=PLUS` 但无 profile | 仅全新空白板默认 BASE/NONE；旧 Base 保持 Base；有效旧 Plus 保持 PLUS/NONE，不猜传感器 | H；重烧另需 D |
| E8-C02 | 旧 Plus 实机无 `cfg_model`；模拟过桥写成功、失败和读回不符 | 只有写入返回成功、键存在且重启后直读仍为 Plus 才放行升级；APP 显示 Plus 或 ACK 不算落盘 | H＋D |
| E8-C03 | 配置写入中断、CRC 错、generation 错、完整但未知的 schema/profile，分别有/无上份有效记录 | 中断/损坏才可恢复一致的上份有效记录；完整未知配置保留并禁开始，不自动退旧 profile；无可恢复记录则 CONFIG_ERROR，证据不被 Base 覆盖 | H；掉电边界需 D |
| E8-P01 | Plus/NONE→J100/CYD；注入 ACK、GET 一致/不一致/丢失/迟到 | 只在当前连接的 profile 和 generation 回读一致时显示已设置，普通 ACK 不完成确认 | H(Android)＋P＋D |
| E8-P02 | 旧 CAP/SNAPSHOT/EVT golden 帧与独立新配置回包 | 旧字段和 182 字节预算不变；profile 不进入 SNAPSHOT；新 GET 配置帧也满足实际 MTU/分帧合同，旧 SW APP 仍解析原正式状态 | P＋H＋D |
| E8-M01 | J100→CYD，注入旧 J100 帧、新 CYD 单帧/停更/连续新帧，再模拟 NVS 写失败 | 旧帧、D_e、模型、校准点不跨 profile；未达新鲜度门槛不能设空载，断流撤销可用；写失败重启旧 reader，旧 profile 新帧确认后才恢复 readiness | H＋P＋D |
| E8-M02 | Demo 在设备 A/J100 采点，切设备 B 或同机 CYD，重启 APP | 历史仍可查看，但未归属/旧设备/旧 profile 数据不进当前拟合，设备配置重新 GET | H(Android)＋D |
| E8-M03 | 已设 D_e，校准页再记一个略不同的 0 kg 距离；分别一次/二次拟合，再清除或重设 D_e | 0 kg 样本只作容差核验；两个模型都满足 `y(D_e)=0`；超差提示；D_e 清除/改变后旧模型不参与体重输出 | H(Android＋固件)＋D |
| E8-M04 | 直接向固件注入过期样本、旧 profile/generation 或运行中的 `EMPTY:SET_CURRENT`；另试旧 `EMPTY:SET` 连新 profile 固件 | 返回可定位的 NACK，原 D_e 与已保存 profile 均不变；新 profile 模式旧命令要求升级 Demo；APP 禁用按钮不能替代固件防护 | H＋P＋D |
| E8-S01 | Plus 双开、单关、双关；正常/无 D_e/无距离/PLUS+NONE/CONFIG_ERROR/CYD 策略待验收；再打开任一保护并尝试继续 | 单关不免站稳；健康配置完成的双关不弹故障确认；配置错误和待选 profile 禁开始，CYD 至少一开未验收时禁开始；重新开一项立即撤销豁免，真实故障的旧 SW 入口/提示另测 | H＋D |
| E8-S02 | 双关后退出/重连/停止，再冷启动；紧跟回读点击一次开关 | 未重启时设备值保持且回显真实；对应命令 ACK 不被主动状态帧抢占；冷启动两开关都恢复开；Base 无入口 | H(Android＋固件)＋D |
| E8-O01 | OTA 新镜像确认前自检成功/失败，附带传感器无信号或 PLUS/NONE；另做写新配置后的受控旧镜像回刷 | 外设故障不触发自动回滚；镜像自检失败才自动回滚；受控旧镜像回刷只先验证旧型号键可读，运行须按旧镜像/实物组合另验 | H＋O |
| E8-J01 | J100 构造 `R_D` 正好 0.16、略高 0.16、正好 0.28、略高 0.28、零/低跨度 | 边界分别正常、可恢复、可恢复、直接危险；低跨度不计算比例且不判正常，具体 reason 待 J100 数据冻结 | H＋D |
| E8-J02 | 按判断调用时钟喂 12 个有效样本、恢复、连续异常 1500 ms、危险 700 ms、断流和旧帧 | MA12 与计时只用当前有效样本；恢复清计时；断流不累计连续危险；确认条件满足后才请求停波 | H＋D |
| E8-J03 | J100 律动中人离台、误触短暂离台、重新站稳；记录波形和 APP 事件 | 只在已冻结的离台持续条件成立后停波并发送正式离台原因；短暂误触不误停，恢复路径按现有 APP/ESP32 合同核对，不由 APP 自行改判 | H＋D |
| E8-J04 | J100 摔倒正例/日常动作反例、故障或无效样本、停波后恢复 | 正例须走正式摔倒确认→停波→原因上报；反例不能误报；只写 warning/debug 的候选不能冒充正式摔倒动作；恢复由当前正式命令/状态链证明 | H＋D |
| E8-J05 | J100 非律动：空载重复测量、已知 3 kg/5 kg、用户站上/离开与站稳/晃动标签 | 由 D_e 和同 profile 距离算空载、有人、站稳；阈值/容差/持续时间用标签数据冻结；无效样本、体重模型缺失不能造出站稳 | 数据采集＋H 回放＋D |

J100 的 `T_span_min_D` 数值、运行中跨度不足的具体动作、离台/摔倒的距离阈值和短断流窗口重建方式仍是待真机数据/状态合同冻结项；不能提前在测试里猜一个毫米值。`python3 tools/run_evaluator_unit_tests.py` 已覆盖新增命令解析和部分配置回包/旧帧 golden；配置存储、空载记录、reader 热切和 BLE 待发帧另有独立主机脚本。它们尚不覆盖 E8-C02 的维护镜像实操、E8-P02 的完整 EVT:STREAM golden/低 MTU、E8-M 的 LaserModule 整链及 E8-S/O/J 实物场景。旧 BLE 交互测试需要设备；现有 SCALE 命令测试会改设备参数，不作为只读冒烟测试。

### 9.3 真机测试矩阵

| 场景 | 前置 | 操作 | 必须看到的证据 |
|---|---|---|---|
| 新设备首次上电 | 从未保存过型号 | 连接 APP | BASE + NONE，无需激光 |
| Base 重启 | Base 已运行 | 重启 ESP32/APP | 仍是 Base，开始门禁不依赖激光 |
| Base USB 重烧/OTA | 已保存 Base | 分别普通重烧和 OTA，再回读 | 仍是 Base，配置未被重置 |
| 旧 Plus 无型号键过桥 | 工作人员核实为旧 Plus，`cfg_model` 缺失 | 过渡流程写入并重启直读，随后普通重烧/OTA | 原始 NVS/读回证据完整；升级后仍 Plus，失败设备不进入升级 |
| Plus/J100 | 接 J100 | APP 选 Plus→J100→确认有效距离→设置 D_e | ACK、GET、距离和 J100 状态更新 |
| Plus/CYD | 接 CYD | APP 选 Plus→CYD→确认有效距离→设置 D_e | 使用 IIC profile，不自动猜型号；本轮不宣称 CYD 状态阈值已验收 |
| 传感器切换 | 已有 J100 配置 | 切换 CYD | 旧 D_e/模型过期，要求重新设置 |
| J100 保护正常 | Plus + J100 + D_e | 至少一个开关开启 | 站稳后才 start_ready；CYD 状态参数另行实测 |
| J100 空载与站稳 | Plus + J100 + D_e，体重模型可为空 | 依次空载、放已知 3 kg/5 kg、用户站上晃动/站稳/离开 | ESP32 距离、空载/有人/稳定基线/start_ready 及 APP 对应状态有带时间标签的证据；阈值经回放确定，不先写固定毫米数 |
| 双保护关闭 | Plus + profile；组合优先级经阶段 A 状态表确认 | 两开关都关 | 按已冻结组合直接允许开始，不要求站稳、不弹故障确认；CYD/缺配置组合不提前宣称通过 |
| 未重启时保持开关 | Plus 正常连接且用户已改保护开关 | 退出/重开 SW APP、断开/重连 BLE、暂停/停止律动，再查看设备状态 | ESP32 两项值保持；APP 不发送默认开关命令，两项回显与设备一致 |
| 保护开关冷启动 | 先把两开关都关闭 | 断电后重新上电，打开 Plus 的设置入口 | 两开关都恢复开启；回读前显示未知，不沿用旧页面的关闭状态 |
| Base 保护入口 | Base 已连接 | 查看正式 SW APP 与 Demo APP 型号页 | 正式 SW APP 无保护入口；Demo APP 完成本轮修改后不渲染保护卡片 |
| Plus 保护入口 | Plus 正常连接 | 手机主动打开顶栏设置，平板查看操作栏 | 开关按设备回读显示；运行中不可切换 |
| 单保护关闭 | Plus + profile | 只关一个 | 不进入组合降级 |
| 无体重模型 | Plus + J100 + D_e，J100 状态策略已验收 | 不做体重校准 | 距离和状态正常，体重无效；CYD 仅核实距离与模型可选，不套用本行状态结论 |
| J100 律动离台与恢复 | Plus + J100 + D_e，已有基线并开始律动 | 离台、误触离台、再站稳，按现有 APP 交互尝试恢复 | ESP32 正式停波和离台原因、APP 对应事件/按钮结果、恢复时新基线与启停命令均有日志 |
| J100 摔倒与恢复 | Plus + J100 + D_e，已有基线并开始律动 | 摔倒正例、日常动作反例，再按现有 APP 交互恢复 | 正例正式确认、停波、摔倒原因；反例不误停；候选 warning 不冒充正式摔倒 |
| 断电保持 | 已设置画像 | 断电再上电 | profile、D_e、模型按规则保持 |
| Plus USB 重烧 | 已保存 Plus + profile + D_e | 普通重烧并连接 Demo APP | 仍是 Plus；已保存配置回读一致，不自动变 Base |
| OTA 成功 | 旧镜像有效 | OTA、新镜像首启 | 自检、确认有效、配置回读 |
| OTA 失败 | 测试失败镜像 | OTA 后故意失败 | 回滚旧镜像，配置仍可读 |
| SW APP 回归 | 固件状态稳定 | SW APP 连接和开始/停止 | 只消费正式状态，原功能不变 |
| SW APP 双关重连竞态 | 健康 Plus，用户依次关闭两个保护 | 退出/重连，立即打开设置并尝试重新开启 | 两项回显来自设备；设置入口仍能使用；命令 ACK 不被主动回显误配，且不出现旧故障确认弹窗 |

### 9.4 证据要求

每个真机场景必须保存与所用 APP 对应的证据，至少包括：

1. ESP32 启动和状态日志；
2. Demo APP 或 SW APP 的 BLE 原始 TX/RX 和结构化状态日志；
3. 操作时间标记；
4. 连接和重连场景的 CAP?、ACK:CAP、SNAPSHOT?、SNAPSHOT；
5. 配置场景的 ACK/GET 回读，保护场景的双开关设备值/APP 回显；
6. 页面实际显示和用户操作结果。

只有页面显示一个数字，或只有用户体感，不足以判定通过。

进入真机阶段前由 AI 准备并先演练采集、标记、停止和回收命令：优先复用 SW 仓 `scripts/device_test_capture.sh`、本仓 `tools/esp32_demo_telemetry_calibration_capture.sh` 中适合的部分；后者目前只覆盖 J100 旧场景，不能直接当 E8 证据。PlatformIO monitor 可人工查看串口，不代替自动采集文件。日志必须带场景 ID、设备身份、固件版本、profile/generation、APP 版本、时间标记与串口/BLE 文件路径。用户用 Android Studio Run 安装 Demo APP 并操作实物，AI 不代装 APK、不代做真机操作。测试前逐项写明是否重装 APP、重烧固件、重启设备/手机、清 APP 数据、登录或切换网络；没有要求的项明确写“不需要”。服务器不参与本地配置，除非该轮另约服务器链路，否则不把它列作虚假的必需证据。某一路日志只有空文件或不可读内容，必须单独标为证据未通过。

## 10. 多轮资料复核

### 10.1 第一轮：ESP32 持久化

Espressif Arduino-ESP32 Preferences 官方文档确认 NVS 数据可跨重启和断电保存，`begin` 可能失败，`isKey/getType` 可检查键及类型，`putUChar` 返回 1 才表示写入成功；`getUChar` 的默认值不能证明键存在。**schema、generation、commit marker、CRC 是本项目为迁移和断电恢复选择的设计，不是 Preferences 自动提供的保证。**任何旧设备升级前都须核实原型号键、类型、写入结果和重启读回。

参考：

- https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
- https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/index.html

### 10.2 第二轮：BLE 配置和实时流

Android 官方 BLE 文档描述连接 GATT、发现服务/特征与启用通知的步骤。**ACK 后再 GET 是本项目的配置正确性合同，并非 Android 官方规定的命令。**本项目须在通知就绪后发送命令，依据当前连接的 ACK 和独立 GET 确认设备已保存；实时流仍遵守本仓库 `STREAM:SET` 合同，ACK 的 enabled/rate/supported 值须与请求匹配，不能收到任意 `ACK:STREAM` 就视为订阅成功。

参考：

- https://developer.android.com/develop/connectivity/bluetooth/ble/ble-overview
- https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data
- ./esp32_realtime_stream_subscription_contract.md

### 10.3 第三轮：I²C 电气和驱动边界

Espressif I²C 资料说明 SDA/SCL 是总线信号，需要合适上拉，频率受总线条件约束。项目内 **CYD 模块 V1.3 手册**把 C 标为 SCL、D 标为 SDA、默认 7 位地址写为 0x30、距离寄存器的单位写为 mm、标称范围写为 20–5000 mm；本项目当前记录为 C→GPIO15、D→GPIO16。其手册不同页对电流写法不一致，不能据此把一个电流数字定为供电验收值。J100 手册列出 100 mm 中心与 ±35 mm 量程；485 的寄存器/串口操作须只取项目 485 操作资料中明确适用于 WTX-J 的段落，并对照已验证原始帧，不能搬用 JD 系列专有命令。手册只约束模块接口和标称条件，不能证明本项目 PCB 连通或从 IIC/485 接口自动识别品牌型号。

参考：

- https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-reference/peripherals/i2c.html
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html
- ./esp32s3_pin_profiles.md
- 传感器资料/芯亿/3683674758CYD3S1612-IIC激光测距模块产品说明书_V1.3.pdf
- 传感器资料/王同学+485/WTX-J系列说明书-V3.0模拟量切换版本.pdf
- 传感器资料/王同学+485/JD系列485通讯操作流程-V1.0.pdf

### 10.4 第四轮：OTA 首启动和回滚

Espressif OTA 官方文档规定，启用回滚时，新镜像首启动须自检并确认镜像可运行；未确认或异常重启会回滚。当前固件的镜像确认发生在 reader 初始化之前，因此**不能**把外部传感器健康、BLE 连接或 SNAPSHOT 收到作为现有镜像确认前置条件。联调仍要在镜像确认后另外核实配置、BLE、reader 与状态输出；新旧镜像共用的 NVS schema 还须做回滚后读回测试。

参考：

- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html

### 10.5 第五轮：测量数据和阈值

NIST 将重复性定义为相同条件下连续测量结果的一致程度；理想弹簧的胡克定律仅支持弹性范围内力与形变成正比，不能证明整个律动仪装配后的 J100 读数全程线性。由此得到两条约束：

- J100 的经验阈值不能直接套给 CYD；
- 用户确认的 J100 16%/28% 是**目标判断边界**；`T_span_min_D`、空载容差、稳定窗口及 CYD 状态参数要由对应 profile 的有标签数据采集和回放验证，不能由公开资料或代码现值直接宣布通过。

参考：

- https://www.nist.gov/pml/nist-technical-note-1297
- https://www.nist.gov/pml/nist-technical-note-1297/nist-tn-1297-appendix-d1-terminology
- https://openstax.org/books/university-physics-volume-1/pages/7-1-work

### 10.6 第六轮：正式构建入口与刷写边界

PlatformIO `default_envs` 决定未指定 `-e` 时的默认构建环境；当前 `platformio.ini` 已将 `esp32s3_unified` 设为默认环境。代码构建通过不等于正式生产烧录验收，仍须核对固件身份/分区，并分别验证普通应用烧录与整片擦除对 NVS 的影响。旧 Plus 升级流程只允许保留 NVS 的方式。

参考：[PlatformIO default_envs](https://docs.platformio.org/en/latest/projectconf/sections/platformio/options/generic/default_envs.html)；[Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html)。

### 10.7 资料边界

- 公开平台资料：约束存储、BLE、I²C、OTA 的通用方法；
- 传感器 PDF：约束地址、寄存器、单位、量程和硬件条件；
- 项目合同：约束 BLE 字段、状态枚举、SW APP 消费方式；
- 真机数据：决定具体 profile 的容差和状态阈值。

任何公开资料都不能替代本项目的 PCB、传感器原始帧、ESP32 日志和用户操作证据。

## 11. 施工顺序和回退

每完成一个阶段，先核对该阶段的代码检查、必要的真机证据和未完成项，再更新对应的阶段收口记录与本蓝图状态，随后进入下一阶段。代码检查通过可标“代码层收口”；没有实物日志的场景仍保持“待真机验收”，不得把它写成整阶段产品验收。

### 阶段 A：合同冻结

先把工作人员和用户会实际碰到的合同固定。2026-10-13 已形成 [阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md)；下列内容中能由代码和产品规则确定的部分已冻结，SW 开关回显和传感器实测参数明确留作阻断：

1. 经确认的新板直接刷统一固件为 Base；**任何旧设备**无型号键、坏键或 NVS 打不开，先走 5.1 的过桥/隔离步骤，写/读/重启直接查 NVS 失败就阻断升级。缺键启动不自动写 Base。
2. 冻结 J100/CYD 稳定 profile ID、与 UART/IIC 后端分离、`DEVICE:SET_CONFIG`、ACK/NACK、独立 GET、稳定设备身份、能力版本与 generation；SNAPSHOT 保持旧字段和 golden frame。
3. 冻结“当前传感器持续有新鲜距离”判据、切换时旧帧隔离，以及 `EMPTY:SET_CURRENT` 由 ESP32 取当前样本；不能凭 APP 上次显示的值写入。每个 profile 的新鲜度毫秒值和连续样本数留待对应传感器实测，不用通用数字代替。
4. 冻结 `BASE+NONE`、`PLUS+NONE`、配置故障、传感器故障、CYD 策略未验收和双关降级的优先级。双关不弹旧故障确认，也不能使 SW APP 的保护开关入口消失。
5. SW APP 当前代码审计已确认摔倒开关重连回显缺设备真相，主动补发 `ACK:FALL_STOP` 与用户点击 ACK 有竞态。必须做专项时序测试；若不能证明可靠，最小跨仓回读合同是正式交付阻断，不凭推测宣称“SW APP 零改且两项回显正确”。
6. 更换传感器后旧体重模型不自动使用，旧历史仅可查看；J100 16%/28% 比例目标保留，`T_span_min_D` 数值及断流时序留给 J100 数据验证。

已冻结的配置/存储/协议部分可作为阶段 B 实现输入；真实测距故障与双关的 APP 交互、SW 回显可靠性、J100/CYD 参数不因本文件而视为验收。

### 阶段 B：固件配置层

1. 先做所有旧 Base/Plus 设备的过桥/阻断流程，再做新板默认 Base 与配置 schema；
2. profile 事务存储、独立回读及新旧 OTA 镜像读配置兼容；
3. reader 按 profile 启停，Base 不探测，切换清旧帧和状态，自动探测仅作诊断；
4. 完成 E8-C/P/M 的固件 host 测试和旧 BLE golden frames 保持测试。

第 1 项规定的是**实体旧设备升级之前**的现场先后顺序。由于旧机台账和实物尚未提供，配置代码先完成了有限收口；它不取消任何旧设备升级前的过桥门槛。

2026-10-13 [阶段 B 代码层收口记录](./e8_stage_b_firmware_progress_20261013.md)列明：配置存储、协议解析、reader 选择、维护镜像的主机故障注入及若干 E8-C/P/M 方法级测试已有通过记录；旧 BLE 的 CAP、两种 SNAPSHOT、摔倒 STOP 等有逐字对照，`EVT:STREAM` 尚无逐字 golden。E8-C02 的旧机实体过桥、E8-P02 的低 MTU、E8-M 的 LaserModule→状态机→BLE 集成回放、旧镜像回刷与真机仍未验收。因此阶段 B 只能标为**有限代码层收口**，不能进入批量升级。

### 阶段 C：Demo APP 型号页

1. 传感器设置卡片；
2. profile 写入与 ACK→GET 回读，连接/切换/断流后的距离新鲜度；
3. Base/Plus 显示和禁用规则；
4. 空载距离按当前 profile 的设备样本设定与回读；
5. 校准点、拟合结果和采样历史按稳定设备身份/profile 隔离；旧未归属历史只读；页面本地缓存不伪造设备真相。

**2026-10-13 代码进度：**五项均已接入对应代码：Base/Plus 卡片显示，`DEVICE:IDENTITY?`/`DEVICE:GET_CONFIG` 回读，新配置 SET ACK 后同连接 GET 核对与实时流重新订阅，距离新鲜度门禁，`EMPTY:SET_CURRENT`/`EMPTY:GET`，以及设备 ID + profile 的本地历史文件与旧历史只读区。新版 Demo 四模块离线测试已有通过记录，固件命令解析与空载记录的两项 focused gate 通过；详细证据见 [阶段 C 进度记录](./e8_stage_c_demo_app_progress_20261013.md)。Android Studio Run 下的连接、空载设置/清除和断电回读已在当前 Plus/J100 验证；新版型号/传感器写入无闪烁、不同传感器和旧板升级仍待验收。

### 阶段 D：校准和其他页面

1. 固件完成 J100 的 D_e/D_b/MA12 比例判断、16%/28% 和 1500/700 ms 状态时序，再做回放测试；`T_span_min_D` 等未有实测值不得伪造通过；
2. ESP32 完成双保护关闭的独立降级与开关状态回显，SW APP 页面/操作先不改，并执行回显竞态回归；
3. Demo APP 完成所有有效距离传感器的可选体重校准，采样、运行、日志以距离为主；
4. CYD 在本轮只验证配置、距离与空载操作；其受保护运行状态另采数、另设算法，不能继承 J100 验收结果。

### 阶段 E：构建、OTA、真机

1. esp32s3_unified 构建；
2. 静态和单元测试；
3. Android Studio Run 安装 Demo APP；
4. J100 验证完整空载/站稳/律动/离台/摔倒链；CYD 只验收本轮已实现的配置和测距链；
5. 断电、重启、USB 普通重烧、OTA 回滚、旧 SW APP 双开关回显；
6. 通过后才考虑清理旧入口；提交、推送与合并按用户明确授权执行。

2026-10-14 的 [J100 未律动真机观察](./e8_j100_idle_real_device_observation_20261014.md)只验证了当前 Plus/J100 在空载→站稳→离台、未点「开始」时的 `start_ready=0→1→0` 和 Demo 底栏灰→绿→灰；日志中的 `weight_valid=0`。阶段 E 的律动、离台停波、摔倒与阈值验收仍未完成；原采集脚本 focus logcat 中断和串口详细诊断缺失已在观察记录单列。

同日的 [J100 轻载停波采数](./e8_j100_lightload_real_device_observation_20261014.md)得到空载 112.33、3 kg 112.03、卸载恢复 112.33、第二次放载 111.98、卸载恢复 112.33 mm；用户事后纠正第二次**实际不足 5 kg、重量未确认**，不能将 111.98 mm 当成 5 kg 标定点。底栏全程灰色，BLE `start_ready=0`；这与当前 J100 的 3 mm“有人”代码门槛一致，却不能据单轮数据冻结新参数。该轮 ESP32 串口文件为空，必须标为设备侧日志证据未通过，不可算 J100 状态判断完整真机验收。

随后在[固定托盘递增加载观察](./e8_j100_incremental_load_observation_20261014.md)中，用户确认 5 kg 对应 111.82 mm，再原位加 3 kg 成为 8 kg 对应 111.53 mm；取下 3 kg 回到 111.81 mm，全部取下回到 112.32 mm。这次短循环约为 0.10 mm/kg，暂支持局部近线性假设，也再次说明已测载荷远低于当前 3 mm 进入门槛。每档只做一次，仍须重复回放；该轮串口亦为空文件，不能写成双端完整验收。

[同托盘重复载荷观察](./e8_j100_repeated_load_observation_20261014.md)补齐空载→3→5→8→5→3→空载，用户依次看到 112.32→112.05→111.84→111.53→111.83→112.04→112.32 mm。两轮确认的 5 kg 读数相差 0.02 mm，8 kg 读数约相同；BLE 全程 `start_ready=0`。当前代码的距离进入／退出门槛分别是 3 mm／1 mm，8 kg 与保存空载点仅差 0.81 mm；“有人”不能建立，按钮灰色符合代码。旧体重版的 5 kg 是在场门槛，站稳还要求大于 5 kg，不能把两者等同。该轮独立 Demo BLE 日志和七个阶段截图齐，但 ESP32 串口仍为 0 字节。下一步用真人站稳、正常晃动及空载回弹的标记序列回放距离阈值，不能以静态载荷直接宣布 J100 运行判断或律动保护验收。

任何阶段失败，回退到上一个已验证的配置/协议边界，不使用 git reset --hard 或清理用户已有证据。

## 12. 分段进入门槛

**阶段 A 合同已记录，阶段 B 已有限代码层收口，阶段 C Demo APP 离线测试已通过；SW APP 摔倒开关回显仍是跨端阻断。**这不等于可以给旧设备批量升级，也不等于 J100/CYD 的运行算法或阶段 C 真机交互已经验收。

- [x] 当前蓝图、产品规则、代码差异和最小测试设计已归档；原有工作区和备份未被清理。
- [x] 引脚记录已与 `GlobalConfig.h` 和 `esp32s3_pin_profiles.md` 对照：485 模块标记 RXD→GPIO17、TXD→GPIO18；CYD C/SCL→GPIO15、D/SDA→GPIO16。实物供电、电平和线束仍待真机核验。
- [x] **配置业务代码输入：**profile ID、命令/回包、能力门禁、`PLUS+NONE` 兼容位、健康双关优先级、空载样本归属和 SW 回显竞态测试已写入阶段 A 合同；仍须按该合同实现并测试。
- [x] **开发测试空载操作：**阶段 C 已用 2 个连续有效样本和 3 秒断流窗口接上页面门禁，ESP32 对 `EMPTY:SET_CURRENT` 再独立验样；可在测试设备上按工作人员路线试设置/清除。
- [ ] **正式开放具体传感器空载设置前：**该 profile 的 `T_fresh`、连续样本数和断流窗口须从 reader 周期/真机数据定值并通过回放；当前 2 样本/3 秒只是开发测试门禁，不能借给 CYD 或 J100 当最终工程参数。
- [ ] **旧设备批量升级前：**设备身份清单和 5.1 过桥流程完成，直接 NVS 读写/重启读回、普通重烧及 OTA 保留配置测试通过。
- [ ] **Demo APP 联调前：**统一固件正式构建环境已固定并成功编译；E8-C/P/M 的 host 与协议测试通过，不以 tool-scons 自动安装中止记录冒充成功。
- [ ] **J100 运行验收前：**J100 比例算法、T_span_min_D 与断流策略有带操作标签的数据和回放结果；E8-J/S 通过，再做实物状态/停波验收。
- [ ] **CYD 受保护运行验收前：**另采 CYD 数据并设计独立判断策略；当前只允许先验收配置、通信、距离、空载操作。
- [ ] **跨端交付前：**SW APP 双开关重连回显与双关后入口可操作通过；若固件兼容方案失败，必须显式调整跨仓范围，不得把风险隐去。

当前已明确、无需再次讨论的三点：

1. 两个保护开关都关闭时，直接降级，不弹确认提示。
2. 更换传感器后，旧体重校准参数不自动继续使用；历史记录可以保留查看，重新测体重时按新传感器校准。
3. Pro、Ultra 只是未来预留名称，本轮没有具体产品行为，不实现、不测试。

## 13. 当前准备状态

| 类别 | 状态 | 说明 |
|---|---|---|
| 施工蓝图与阶段 A 合同 | 可独立确定部分已冻结 | 用户操作、旧机过桥、profile ID、配置 GET/SET、健康双关优先级和失败测试已有独立合同；SW 回显可靠性与实测数值仍待证明 |
| 代码审计 | 已补全 P0 清单 | 包含旧 Plus 键缺失、写入假 ACK、热切换旧帧、Demo 历史归属、SW 开关回显及 J100 断流计时 |
| 公开资料复核 | 六轮完成 | Espressif Preferences/I2C/OTA、Android BLE、NIST、PlatformIO 和项目内 J100/CYD 手册各用于其能证明的边界 |
| 测试矩阵 | E8-C/P/M 的部分方法级测试通过，S/O/J 未收口 | 存储、命令解析、reader 热切、BLE 旧帧、OTA 写入门禁、配置停波及维护镜像故障注入有主机证据；C02 旧机实体过桥、P02 完整 golden/低 MTU、M 整链及实物场景未验收 |
| 自动化预检 | 阶段 B 构建及独立主机脚本有通过记录 | evaluator、配置存储、空载存储、reader、BLE 队列/停波、维护镜像故障注入脚本通过；`pio run` 统一环境、旧环境和维护镜像构建通过。均不能代替旧镜像回刷或设备验收 |
| Demo APP 阶段 C 代码 | 已接入；新写入流程待真机 | 型号/传感器/空载卡片、设备身份和配置回读、写后同连接核对与重订阅、本地历史归属均已有代码；当前四模块 JUnit 170 项 0 失败/错误/跳过，开发测试的 2 样本/3 秒门禁待实物定值 |
| Demo APP 真机 | 当前 Plus/J100 的部分操作通过 | 旧版 J100 写入/距离已见实物，但主动重连造成短闪；新版连接、空载设置/清除/重设和复位/断电回读已验证；未律动时常驻底栏「开始」灰→绿→灰与 ESP32 `start_ready` 同步；新版配置写入无闪烁仍待合适设备实测 |
| 固件施工 | 配置层及 J100 未律动路径部分真机通过 | 旧 Plus 保留、J100 profile 写入回读、J100 距离、空载设置/清除及断电后保留已见实物；J100 未律动的空载→站稳→离台闭环有一次带标签实测；轻载停波采数有 BLE／截图但第一次标称 5 kg 已纠正为重量未确认、串口空文件，不能算完整设备侧验收；新板 Base、CYD 切换及 J100 律动中保护、阈值仍未验收 |
| 协议施工 | 固件端与 Demo APP 端已接入，BLE 部分实测 | J100 SET ACK→GET、订阅/距离与空载 SET/CLEAR→GET 已有手机证据；新版配置写入后同连接恢复、断流等实物链路仍须采集证据 |

## 14. 相关现有文档

- 阶段 C Demo APP 进度：./e8_stage_c_demo_app_progress_20261013.md
- 原统一测距蓝图：./esp32_iic_preserve_all_function施工蓝图.md
- Demo APP 距离优先蓝图：./demo_app_distance_first_施工蓝图.md
- Base/Plus 降级交付计划：./base_plus_degraded_delivery_plan.md
- Base/Plus 降级回归清单：./base_plus_degraded_regression_checklist.md
- BLE 安全重构冻结清单：./esp32_ble_safe_refactor_freeze_checklist.md
- 实时流订阅合同：./esp32_realtime_stream_subscription_contract.md
- ESP32-S3 引脚 profile：./esp32s3_pin_profiles.md
- 距离测量语义：./laser_measurement_semantics.md
