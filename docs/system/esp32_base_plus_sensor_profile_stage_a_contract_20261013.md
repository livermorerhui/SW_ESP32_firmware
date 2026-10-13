# E8 阶段 A：Base/Plus 与传感器配置合同

日期：2026-10-13
状态：可独立确定的配置、协议和状态边界已冻结；正式 SW APP 摔倒开关重连回显仍为跨端阻断，J100/CYD 的实测参数仍待数据
范围：合同与测试设计，不代表固件或 Demo APP 已实现，也不代表真机验收

## 1. 工作人员和用户实际会怎么操作

1. **生产/维修人员烧录：**确认是全新空白板，烧录统一固件后看到 Base；如果是旧设备，先按设备台账核实旧型号和 NVS 记录，再普通重烧或 OTA，回读后仍是原 Base/Plus。服务器不参与。
2. **工作人员配置 Plus：**Demo APP 连接、读取 ESP32 身份及配置能力 → 选 Plus → 选实际安装的 J100-485 或 CYD3S1612-IIC → ESP32 保存并启动对应 reader → Demo APP 自动重连并重新确认配置/订阅实时流 → 看见该传感器持续产生有效距离 → 平台空载时点“设置” → ESP32 取自己当前新鲜的距离保存为空载距离，并把实际保存值回给 APP。体重校准是可选的独立步骤。
3. **用户日常使用：**Base 按无激光路径运行；已验收的 J100 Plus 由 ESP32 依据距离判断状态，SW APP 只使用设备上报的状态。用户在 SW APP 中切换摔倒保护、律动离开保护；ESP32 未重启时保留开关值，重启后两项都开。健康、已配置的 Plus 两项都关时直接免站稳开始，不弹旧激光故障确认；这条无保护路径对 CYD 也列入待验收目标，不能当作 CYD 保护算法通过。CYD 的受保护运行另做实验。

## 2. 型号、身份和配置真相

| 对象 | 固定合同 |
|---|---|
| `platform_model` | 新 profile 配置事务本轮可写 `BASE`、`PLUS`；`PRO`、`ULTRA` 是已有枚举但无当前产品策略，新事务返回 `UNSUPPORTED_MODEL`。旧命令和旧 NVS 中的 2/3 不是坏类型或未知值，保留旧兼容语义与原始证据，不重解释成 Base/Plus，也不宣称新 profile 功能支持它们。|
| `sensor_profile` 线上 ID | `NONE`、`J100_485`、`CYD3S1612_IIC`；大小写、拼写固定。新增同 UART/IIC 的其他型号时必须分配新 ID，不能靠总线推断型号。|
| 持久化 profile 数值 | `NONE=0`、`J100_485=1`、`CYD3S1612_IIC=2`；未知数值保留原记录、不可开始、报 `UNSUPPORTED_PROFILE`，不能改成 NONE。现有内部 `LaserSensorProfileId::WANG_WTX_J100_485=0` 不是线上/存储 ID，必须显式映射。|
| 传输后端 | J100 使用现有 485 驱动，CYD 使用现有 IIC 驱动；后端类型不等于传感器型号，也不决定另一型号的状态阈值。|
| 稳定设备身份 | 复用既有 `DEVICE:IDENTITY?` → `ACK:DEVICE_ID id=sw1_<32位小写十六进制> version=1`；来源是 ESP32 eFuse 基础 MAC 的域隔离摘要。配置 GET 不重复造一个 `device_id`。Demo 历史以设备 ID + profile 归属；旧无归属记录只读。|
| `laser_installed` | 现有 `ACK:CAP` 和旧配置 ACK 的兼容语义保留：Base=0，Plus=1，包括 `PLUS+NONE`；精简 `SNAPSHOT` **没有**这个字段。它表示 Plus 需要激光硬件，不表示已选中型号或实时测距健康。|
| 空载与体重 | D_e 独立于体重模型。设备记录隐含设备身份，显式绑定 profile 和 `generation`；空载每次设置/清除更新 `empty_revision`，模型记录绑定该修订号，修改 D_e 后旧模型失效。Demo 本地记录还需绑定设备 ID。更换 profile 后旧值与旧模型不参与当前判断/显示，历史仍可查看。无有效模型时体重无效，但距离与已验收的状态判断不受阻。|

线上 `generation` 固定为十进制 `uint32`，范围 0～4294967295：仅 Base/Plus 或 profile **实际变化**且保存成功才递增；新板运行默认 Base 但尚未显式保存时为 0。同值重发不重复写入；溢出时拒绝配置并交维修。D_e 的设置/清除只递增 `empty_revision`，模型的设置/清除只递增 `model_revision`，都不改配置 `generation`。运行中的 reader/sample 另有内存代际，切换后重新开始；不能把旧内部 `J100=0` 或 BLE stream 序号当配置代际。

## 3. 出厂、旧机与 NVS

1. **确认空白的新板：**无有效旧/新型号记录时，运行默认 `BASE+NONE`。不要因为启动时发现 `cfg_model` 缺键就自动写 Base；否则漏做过桥的旧 Plus 会失去缺键证据。只有出厂确认或工作人员显式配置事务才落盘。
2. **任何旧设备：**先由实物和台账确认设备身份及原 Base/Plus。打开 `scale_cal` 必须检查 `Preferences.begin` 成功，再检查 `cfg_model` 键存在、类型 `PT_U8`、值为 0/1；不能只读 `getUChar` 默认值。**覆盖前**保存设备 ID、旧固件版本、原键类型/原值及必要的 NVS 备份。缺键/坏键的旧 Base/Plus 走过桥维护镜像/USB 流程，显式写入核实的原型号，要求 `putUChar==1`、直接读回和断电重启后直接读回一致，然后才升级。NVS 打不开则隔离，不写入。旧记录为合法预留值 Pro/Ultra 时保留并交产品核实，不擅自改成 Base/Plus。旧 Plus 不是唯一需要过桥的旧机。
3. **新记录：**用独立、版本化的设备配置记录保存 schema、model、profile、generation、commit marker 和按规范字节序列计算的 CRC；两个槽保存当前和上份有效记录。写非当前槽、检查写入长度、读回校验、提交、再读回；任何失败不能 ACK 成功或启用新 reader。**CRC 损坏或写入中断**时可恢复同旧兼容键一致的上份有效记录；完整有效但当前镜像不认识的 schema/profile 则保留原记录、禁开始并报告 `UNSUPPORTED_PROFILE`/版本不支持，绝不自动退回旧 profile。无可恢复记录或跨键不一致为 `CONFIG_ERROR`。
4. **旧镜像兼容：**`scale_cal/cfg_model`、`cfg_laser` 在 OTA 可回滚期保持可读，分别镜像 Base/Plus 与旧激光兼容位。新记录与旧键是跨键写入，NVS 不保证整体事务；不一致时禁开始并进入维修恢复，不静默覆盖为 Base。**新配置写入后回刷旧镜像**属于受控降级测试，不是自动 OTA 回滚：当前固件在传感器/BLE 初始化前已确认新镜像，工作人员能写配置时自动回滚窗口已结束。旧镜像能读旧键仅证明型号可读，不证明它理解新 profile 或能安全运行；只允许经旧镜像与传感器实物组合验证的降级。普通应用烧录/OTA 保留 NVS；整片擦除须先备份并恢复配置，不属于普通升级。
5. **OTA：**保留现有固件自检与镜像确认顺序。未选传感器、外部传感器故障、未设置 D_e 不作为镜像损坏触发 OTA 回滚；应用镜像回滚也不会自动回滚 NVS。

## 4. BLE 配置合同（新 Demo 与新固件）

GATT UUID、换行分帧、`CAP?`、`SNAPSHOT?`、`WAVE:*`、`STREAM:SET`、旧正式状态字段和 `APP_DRIVEN_SESSION` 不变。现有 `ACK:CAP` golden 帧已占 182/182 字节；不向 CAP/SNAPSHOT 塞 profile，也不把旧 `proto` 或通用 ACK 当新配置能力。

~~~text
DEVICE:GET_CONFIG
ACK:DEVICE_CONFIG op=GET schema=1 platform_model=PLUS sensor_profile=J100_485 laser_installed=1 generation=12

DEVICE:SET_CONFIG platform_model=PLUS,sensor_profile=CYD3S1612_IIC
ACK:DEVICE_CONFIG op=SET schema=1 platform_model=PLUS sensor_profile=CYD3S1612_IIC laser_installed=1 generation=13
DEVICE:GET_CONFIG
ACK:DEVICE_CONFIG op=GET schema=1 platform_model=PLUS sensor_profile=CYD3S1612_IIC laser_installed=1 generation=13
~~~

- 新 Demo 必须先收到当前连接中字段完整、类型合法、`schema=1` 的 `op=GET`，才允许新选型。旧固件对 GET 可能返回不同 NACK，或被旧 `F/I/E` 宽松解析误判；一律按“没有有效 GET”处理，不能依赖 NACK 的具体字样。
- 新 SET **不带**旧 `laser_installed` 参数，避免旧固件忽略新增 profile 却成功 ACK；新固件按型号推导兼容位。新 SET 解析分支须在旧 `DEVICE:SET_CONFIG` 前以 `sensor_profile` 明确分流并校验整条命令；拒绝未知参数、未知 profile、`BASE+非NONE`、`PLUS+不支持型号`、运行中切换。`PLUS+NONE` 是合法但不能投入正常使用的调试中间态。
- `ACK:DEVICE_CONFIG op=SET` 只能在持久化和读回成功后发送；Demo 收到后还须发送 GET，核对设备身份、model、profile 和 generation。变更时 generation 必须大于 SET 前 GET；同值幂等请求可以不递增。断线、超时、无关 ACK、旧 ACK、迟到 GET 或 GET 不一致均不能显示“设置成功”。同一连接只允许一个配置事务在途。
- 新版回复 `NACK:DEVICE_CONFIG op=SET reason=<code>`，固定 code 至少覆盖 `UNSUPPORTED_MODEL`、`UNSUPPORTED_PROFILE`、`INVALID_COMBINATION`、`NOT_IDLE`、`STORAGE_FAILED`、`VERIFY_FAILED`。配置读回不可用用 `NACK:DEVICE_CONFIG op=GET reason=CONFIG_ERROR`，普通 Demo 阻止继续配置并提示维修。旧通用 NACK 只作为能力缺失/失败显示，不视为成功。
- 既有旧 Demo 的 `DEVICE:SET_CONFIG platform_model=...,laser_installed=...` 和旧 `ACK:DEVICE_CONFIG platform_model=... laser_installed=...` 是**明确标注的旧兼容路径**，不据此选择 profile。旧命令把 Plus→Base 时清当前 profile 并让旧 D_e/模型失效，Base→Plus 时为 `PLUS+NONE`、必须由新 Demo 显式选传感器，同一 Plus 重发保留已选 profile；旧版 Pro/Ultra 命令仍按既有旧兼容语义处理，不纳入新 profile 事务。旧客户端不发送新 GET；新 Demo 的 parser 必须按精确前缀与 `op` 分流，不能用 `startsWith("ACK:DEVICE_CONFIG")` 吞掉所有回复，通用 ACK 不能清除待回读事务。
- 新 GET 命令解析必须先于旧 `F/I/E` 子串兼容分支。配置回复、最长 profile/generation、空载回复及失效流帧都要按实际 MTU 和通知队列测长度与完整性；旧 CAP/SNAPSHOT golden 字节不变。

## 5. 当前距离、空载设置与切换

- Demo 仍按既有合同显式 `STREAM:SET enabled=1,rate_hz=10`，确认 `ACK:STREAM` 后才观察 `EVT:STREAM`。**profile 实际切换后，Demo 在 SET ACK/GET 一致后自动断开并重连**，重新取得同一设备 ID 与 GET 配置，再重新订阅实时流；只接受新连接的距离帧。断连、切换 profile、流超时立即撤销“传感器可用”和空载“设置”资格，清掉旧 UI 实时值；旧 BLE 后端名不能证明新 profile。同一 UART/IIC 后端的未来新型号也执行这一步，避免旧连接内没有 profile 标记的流帧混入新型号。重连失败保留已落盘的新 profile，但 APP 显示“待确认/不可设置空载”，不得退回旧 UI 数值。
- 固件在 profile 切换时先拒绝运行中操作，隔离并清空旧 reader、测量队列、基线/站稳和内部 sample 代际；持久化成功后启动新 reader。新鲜样本必须同时满足当前设备配置 generation、profile、内部测量代际、质量有效、有限且在物理量程、单调接收序号和 `age_ms≤T_fresh(profile)`。`T_fresh(profile)` 与 UI 所需连续新样本数/断流窗口是 **profile 参数**，必须由读取周期和真机日志定值；阶段 A 不把 J100 的 50 Hz 或 Demo 的 10 Hz 错当 CYD 保证值。
- 空载按钮使用新命令 `EMPTY:SET_CURRENT`：ESP32 **在处理命令时**取当前 profile 的新鲜有效实测值，而不是相信 APP 屏幕缓存；无样本、过期、错误 profile/代际、正在运行、NVS 写读失败均拒绝，原 D_e 不变。成功回 `ACK:EMPTY op=SET_CURRENT status=SET distance_mm=<实际保存值> tolerance_mm=<当前profile值> sensor_profile=<ID> generation=<配置代际> empty_revision=<新修订>`；Demo 再 `EMPTY:GET` 回读相同设备、profile、generation、修订和值后显示具体数值。新解析器必须在旧 `EMPTY:SET` 前按完整命令边界匹配 `EMPTY:SET_CURRENT`。未设置/清除时回 `status=UNSET`，不伪造距离和容差。
- 旧 `EMPTY:SET distance_mm=...` 保持既有有限值/非运行中的**旧兼容语义**，但不能作为新 profile 设备的安装流程：当设备进入新 `schema=1` profile 管理后，该旧命令返回 `NACK:EMPTY op=SET reason=USE_SET_CURRENT`，须用新 Demo。旧 Demo + 新 profile 固件不属于本轮可交付组合，需明确展示升级要求；这避免为“兼容”悄悄改旧命令的验收窗口，却让过期 APP 值写入新 profile 的 D_e。
- D_e 清除/重设不清除历史，但会让依赖旧 D_e 的体重模型失效；模型为空时不上传有效体重。空载距离与体重校准各自独立，CYD 同样可以保存 D_e 或可选模型，但 CYD 受保护运行算法仍待实测。

## 6. Plus 状态优先级与 SW APP 边界

| 先判条件 | ESP32 目标结果 | 对现有 SW APP 的边界 |
|---|---|---|
| `BASE+NONE` | 按 Base 原路径；`laser_installed=0`，不需要 D_e/模型/测距 | 无 Plus 保护入口。|
| 配置损坏、未知 profile、非法 Base 组合 | `CONFIG_ERROR`/不允许开始，不自动猜传感器或落 Base | 此为维护故障，不冒充离台/摔倒正式安全事件。|
| `PLUS+NONE` | 保留 Plus，提示工作人员选传感器；不允许正常开始，两关也不绕过配置缺失 | `laser_installed=1` 仅指硬件预期，不能把此中间态宣称可交付给用户。|
| Plus 已选 profile，但 D_e 缺失 | 配置未完成，不允许开始；两关只免站稳，不免除空载配置 | Demo 提示先设置空载；不能用旧故障确认绕过配置。|
| Plus 已选 profile，但传感器失效 | 保护开启时不就绪；真实故障沿旧授权路径，已有锁定故障优先 | 双关对**真实故障**的 APP 提示/设置入口不能从健康场景推断，需单独联调。|
| Plus + CYD 有效距离，但状态算法未验收，至少一项保护开启 | 允许工作人员设置和查看 D_e；`start_ready=0`，`WAVE:START` 拒绝，不让当前临时阈值冒充已验收的保护算法 | 不给用户误报“受保护运行可用”；双关目标按下一行单独验收。|
| 健康、已配置且状态策略已验收的 Plus，至少一项保护开启 | 走对应 profile 的距离站稳与状态算法；单关只改变对应动作 | SW 只看正式状态，不接收体重作为安全输入。|
| 同一健康、已选 profile 且 D_e 已设置的 Plus 两项都关闭 | 无论该 profile 的受保护策略是否已验收，ESP32 的有效运行/开始门禁为真，不要求站稳或稳定基线；基线/在位的真实观测值不伪造；既有锁定故障和恢复中暂停仍优先 | 保持健康事实 `laser_installed=1`、`laser_available=1`、`measurement_health=READY`，既有 SNAPSHOT 中 `start_ready=1`、`degraded_start_available=0`、`degraded_start_enabled=0`；内部 `protection_degraded=0`。不弹旧激光故障确认，设置入口仍可操作；CYD 此路径须独立实物验收。|

此表最后一行只冻结**健康、配置完成**的双关目标，不把“故障中也无提示”“未选型也可开始”写成已授权结果。`PLUS+NONE`/配置故障时开始被禁止，但它们在线上旧状态字段中的详细故障投影和 SW 页面效果仍需专项联调。CYD 双关后的免站稳使用需要在阶段 D 的兼容回放和真机中单列验收，不能借 J100 的保护算法通过结论。

ESP32 未重启时两开关保持；冷启动都设为开。**已发现跨端阻断：**现有 Android SW APP 重连后对摔倒开关没有设备 GET，Repository 会投影默认开；iOS 缺读回时也回退默认开。固件主动发旧 `ACK:FALL_STOP` 可能与用户点击命令 ACK 交错，Android SDK 只取同连接第一条 ACK、未核请求值，不能作为已证明可靠的正式方案。阶段 A 不承诺“SW APP 源码零修改且重连两项回显正确”；先做时序测试，若无法证明，进入最小跨仓回读合同审查，仍不改 SW 页面或用户操作。不能为了回显把双关伪装成 `laser_available=0`、`protection_degraded=1` 或旧故障原因。

## 7. 状态语义审计记录

- **当前正式分类：**既有 `TopState`、正式 `EVT:SAFETY/STOP/FAULT` 和 `USER_LEFT_PLATFORM`、`FALL_SUSPECTED` 等含义保持；本轮不新增停波主分类。
- **候选原因：**`CONFIG_ERROR`、`UNSUPPORTED_PROFILE`、`PLUS+NONE`、CYD 策略未验收是配置/能力原因；`MEASUREMENT_UNAVAILABLE` 是测量故障；双关是用户关闭保护后的开始策略。它们不合并为摔倒或离台。
- **上游合同：**配置原因走独立 GET/NACK 与设备日志；正式安全事件只由原状态 owner 按原停波合同上报；warning/debug/兼容值不得升级为正式停波原因。
- **APP 映射：**Demo 显示准确的配置/通信状态；SW 保持既有状态消费，双关健康时不触发旧故障提示；摔倒开关回显阻断单独处理。
- **backend 分类：**本轮是本地 BLE 设备配置，不修改服务器会话分类或报表。
- **admin-web / iOS 展示：**admin-web 不涉及；iOS SW 保护回显需与 Android 分别验证，不能以其能解析可选字段证明 Android 已可回读。
- **真实数据：**本阶段无新 J100/CYD 操作标签、BLE 时序或服务器样本；故不能把实测阈值和跨端回显写成已通过。
- **能否升级正式分类：**目前不能；配置失败和 CYD 策略待验收不是新的正式安全事件。
- **稳定分类 vs 动态身份：**安全分类保持既有枚举；`sensor_profile` 与设备 ID 是稳定协议身份，品牌/型号展示名可变；传输后端和原始原因只是技术证据。
- **验证计划：**执行 §8 的配置、协议、时序、状态与实物证据；问题复现前保存原始命令、回包、日志与操作标记。
- **禁止改动边界：**不改 SW 日常页面/操作、服务器分类、现有 CAP/SNAPSHOT/golden、OTA 分片与 APP 驱动的会话模型；不以 warning/debug 或旧故障降级伪装双关。

## 8. 阶段 A 形成的测试门槛

| ID | 注入/操作 | 通过条件 |
|---|---|---|
| A-C01 | 全新空白、旧 Base/Plus 有键、任一旧机无键/坏键、合法预留 Pro/Ultra、NVS `begin` 失败 | 只有确认空白的新板默认 Base；覆盖前保存设备身份/原键证据；旧 Base/Plus 先过桥，键类型/写返回/重启直读齐全；Pro/Ultra 保留隔离；NVS 打不开则不写不升级。|
| A-C02 | 双槽写中断、CRC/commit/generation 错、旧键与新记录不一致、未知 profile/schema、NVS 近满 | 中断/损坏可恢复上份一致的有效记录；完整但未知的 schema/profile 保留并禁开始，不降到旧 profile；跨键不一致和无可恢复记录报配置故障。|
| A-P01 | 新 Demo 连旧固件，GET 返回任意 NACK/无响应；旧固件忽略新字段但回旧 ACK | 新配置入口不开放；通用/旧 ACK 不能确认 profile。|
| A-P02 | SET ACK 丢失/迟到、GET 不一致/跨连接/跨设备、同值重发、未知 profile、Pro/Ultra | 仅身份、model/profile/generation 一致且完成 GET 才显示成功；NACK 原因可定位。|
| A-P03 | 旧 CAP/SNAPSHOT/EVT golden，最长新配置/空载回复、低 MTU 分片，GET 命令与旧 F/I/E 解析冲突 | 旧帧字节不变；新回复可完整接收；新 GET 不被旧兼容分支吞掉。|
| A-M01 | J100→CYD、未来同后端新 profile，旧 reader 帧和 BLE 队列迟到、自动重连失败/成功、单帧/停更/连续新样本 | SET 后同连接只作 GET 确认；新连接身份/GET/STREAM ACK 完成前不显示可用；旧帧不写成新 D_e，断流撤销设置资格。|
| A-M02 | 解析器实际分派 `EMPTY:SET_CURRENT`/旧 `EMPTY:SET`，覆盖无样本/过期/运行中/存储失败及旧 Demo 连新 profile 固件 | 新命令不被旧前缀吞掉；无效请求 NACK、原 D_e 不变；成功回复实际保存值和修订，GET 一致；旧命令在新 profile 模式明确要求升级。|
| A-S01 | 健康 J100/CYD 双开/单关/双关、站稳/未站稳、已有故障/暂停、`PLUS+NONE`；双关开始→暂停→重新开任一保护→未站稳继续 | 单关不免站稳；健康双关直接开始无确认；重新开一项立即撤销豁免；CYD 至少一开未验收时拒绝开始；故障/未配置不被双关伪装为正常。|
| A-S02 | 关闭摔倒保护后退出/重连、迅速点击、旧主动 ACK 与点击 ACK 交错、冷启动 | ESP32 未重启保持、重启两项开；SW 两项回显与设备一致且点击 ACK 不错配。**当前代码审计已显示此项未满足，跨端阻断。**|
| A-O01 | 普通 USB、确认前 OTA 自动回滚、写入新配置后的受控旧镜像回刷、整片擦除对照 | 原 Base/Plus 与旧键保持；自动回滚不伪称已写新配置；受控回刷仅证明旧键可读，实际运行须按旧镜像/实物组合另验；整片擦除需备份恢复。|

上表是待实现测试，不是测试通过记录。J100 16%/28%、`T_span_min_D`、CYD 状态阈值仍按主蓝图后续阶段的有标签数据验证，不用阶段 A 想象的毫米数填空。

## 9. 阶段 A 收口与下一阶段门槛

本次已完成合同审计与文档冻结：现场路线、稳定 profile ID、独立身份命令、配置 GET/SET/ACK/NACK、旧机过桥、样本来源、健康双关状态边界和回归向量。尚未完成代码、自动化测试、固件构建、Android Studio Run 或真机验收。

**阶段 A 不能标为全部通过：**SW APP 摔倒开关重连回显有当前代码层面的阻断；`T_fresh(profile)`、连续样本数及 J100/CYD 算法参数需设备数据；真实故障与双关的 APP 交互仍须联调。开始阶段 B 的配置存储/协议实现可以依据已冻结部分推进，但对外承诺“SW APP 完全零改且两项回显正确”、旧机批量升级、受保护运行交付均须等对应阻断解除。

参考：[Espressif Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)、[Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html)、[Espressif OTA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html)、[Android BLE 数据传输](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)。这些资料约束存储、回滚和通信方法；本项目的命令字、状态组合与测试门槛来自现有代码和产品需求。
