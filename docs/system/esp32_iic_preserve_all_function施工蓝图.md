# ESP32 测距链路施工蓝图：保留原功能并加入 CYD3S1612-IIC

版本：E8-SOURCE-REVIEW
目标：在不减少原有产品功能的前提下，加入 CYD3S1612-IIC 测距，并把“距离状态判断”和“体重测量/校准”分成两条链路；同时让 Demo APP 的校准、采样、运行控制和日志都遵守这两条链路，最终形成一份可通过现有 BLE OTA 发布的统一 ESP32 固件。

当前专题入口：

- “新设备默认 Base、已配置设备烧录或升级后保留原型号、由 Demo APP 选择 Plus/传感器/空载距离”的最新施工合同见 [ESP32 Base/Plus 与 Demo APP 传感器配置施工蓝图](./esp32_base_plus_sensor_profile_app_configuration施工蓝图.md)。
- 该专题文档优先于本文件早期关于“默认 Plus”“统一固件自动探测”的历史描述；历史章节保留作审计证据，不代表新的目标合同。

本次修订重点：

- ESP32 负责把不同传感器的输出解释并转换为统一的真实距离；
- 有激光设备的空载、有人、稳定、离开、律动、摔倒和 `start_ready` 使用距离链路；
- 体重仍由校准模型计算，保留一次和二次拟合，但两者都必须经过 0kg 空载定点；
- BASE 无激光逻辑保持不变；PLUS 在激光不可用时继续使用既有降级到 BASE 律动 4 的逻辑；
- Demo APP 显示距离、体重并负责校准、采样和证据记录；运行按钮只投影 ESP32 正式状态；SW APP 只消费正式状态；
- BLE、OTA 和实时流订阅合同保持原有边界，不根据 APP 名称猜测上报策略。

说明：文档中 E3/E4 小节保留为历史审计证据；第 12 节记录统一固件和协议的施工状态，第 13 节记录 Demo APP 页面审计、公开资料复核和当前未闭合门禁。若历史施工描述与第 13 节页面审计冲突，以第 13 节为准；“协议已实现”不等于“页面交互已验收”。

未律动阶段“体重版空载 → 距离版空载 → 体重版站稳 → 距离版站稳”的公式、待定参数和采数要求见[参数映射及采数方案](./esp32_未律动空载与站稳参数映射及采数方案.md)。律动阶段 J100-485 沿用体重版 16%/28% 边界，并按空载形变基准归一化；当前实现尚未完成该映射，见第 13.25 节。CYD3S1612-IIC 和未来更换的传感器分别建立判断参数，不继承 J100 阈值；空载/站稳和其他传感器的距离阈值需基于对应数据确定，当前默认值仅代表代码现值，不是验证结论。

## 1. 最终目标

施工完成后必须同时满足：

1. 原有 485 测距和体重计算继续工作；有激光设备的运行状态改由统一距离链路判断。
2. CYD3S1612-IIC 能稳定读取距离。
3. ESP32 负责所有传感器的单位换算。
4. ESP32 对外统一输出真实物理距离字段 `distance_mm`。
5. Demo APP 只读取 `distance_mm`，不自行换算、不判断传感器型号。
6. Demo APP 实时显示距离和体重、记录校准点、保存距离优先的采样数据并输出可审计日志；SW APP 只消费正式状态。
7. 0kg 校准点成为一次和二次模型的强制通过点，空载体重不再由自由截距决定。
8. OTA、BLE 连接、实时流订阅和 SW APP 正式状态合同保持原有边界。
9. 485 版本仍可编译、烧录和回退使用，直到统一固件完成验收。
10. 最终统一固件无需按传感器型号区分烧录包，启动时自动选择已安装的测距传感器。

## 2. 硬件引脚合同

当前软件施工假设、待 PCB 通断/波形核验的接线合同：

| 链路 | ESP32 引脚 | PCB 信号 | 固件定义 |
|---|---:|---|---|
| 485 | GPIO17 | 485 RXD -> ESP32 GPIO17 | `RX_PIN = 17` |
| 485 | GPIO18 | 485 TXD -> ESP32 GPIO18 | `TX_PIN = 18` |
| IIC | GPIO15 | SCL | `I2C_SCL_PIN = 15` |
| IIC | GPIO16 | SDA | `I2C_SDA_PIN = 16` |

说明：这里以 PCB 网络命名为准。不得再根据其他 MAX485 模块的丝印自行交换引脚；如硬件原理图与此表不一致，必须先停工核对。

## 3. 软件分层

```text
IIC 驱动 / 485 驱动
        ↓
ESP32 传感器适配层
        ↓
统一 NormalizedDistanceSample
        ├───────────────┐
        ↓               ↓
距离状态链          体重测量链
空载/有人           0kg 定点拟合
稳定/离开           实时体重
律动/摔倒           校准/记录
start_ready
        ↓               ↓
正式状态事件       Demo 测量流
```

传感器只按自己的协议输出测量值；ESP32 是读取、解码、方向处理和单位换算的唯一 owner。底层驱动不得把业务状态或体重含义写入传感器适配器，上层业务不得读取 IIC 或 485 原始寄存器值。

`NormalizedDistanceSample` 至少包含：

```text
raw_value
distance_mm              # 物理毫米，ESP32 唯一换算 owner
distance_runtime         # 仅内部兼容单位，不进入 APP/CSV
transport_ok
sample_valid
quality_code
invalid_reason
timestamp/device_timestamp
freshness/sequence
sensor_type/backend
```

第 4 节的 `distance_valid/weight_valid` 分离字段和 distance-only APP 消费属于目标态；当前正式代码仍把 `valid` 作为整条距离+体重样本标志，必须完成协议评审后才能启用目标态。

状态链使用统一后的距离；体重链使用同一距离和持久化校准模型。距离有效但体重模型未完成时，状态链仍可判断测距状态，体重链必须标记为未校准，不能把体重错误当成测距无效。

## 4. 两种传感器的统一数据合同

### 4.1 IIC

- 总线：GPIO15=SCL，GPIO16=SDA。
- 设备地址、寄存器和读取顺序以原厂说明书为准。
- 原始值单位为毫米。
- ESP32 将原始毫米值转换为内部运行时单位，同时保留物理毫米值供协议输出。
- IIC 读取节奏必须尊重模块约 30 帧/秒的更新能力；统一固件默认以约 33ms 作为 IIC 最小读取间隔，不能把 20ms 轮询次数当成 50Hz 新测量帧率。
- CYD3S1612-IIC 的地址、寄存器、读取顺序和单位以 `3683674758CYD3S1612-IIC激光测距模块产品说明书_V1.3.pdf` 为设备协议真值。
- NDS03 产品应用手册和数据手册仅作为辅助参考，不能直接替换 CYD3S1612-IIC 模块协议。

### 4.2 485

- UART 使用 GPIO17/18；当前正式接线采用本项目 PCB 标记：485 `RXD -> GPIO17`、485 `TXD -> GPIO18`；固件视角为 `RX_PIN=17`、`TX_PIN=18`。
- 保留现有 Modbus 参数和王同学测距仪解码逻辑。
- 不改变原有状态判断使用的内部运行时单位。
- ESP32 生成统一的物理毫米值 `distance_mm`。

### 4.3 对外协议

当前完整有效样本必须包含：

```text
EVT:STREAM ... distance_mm=<physical_mm> weight=<kg> ...
```

- `distance_mm`：ESP32 输出的正式物理距离字段，Demo APP 唯一使用它。
- `weight`：ESP32 根据当前校准模型计算的测量结果；模型未完成时必须标记无效或不可用于校准。
- Demo APP 不做传感器类型判断和单位换算。
- 缺少 `distance_mm` 的样本不得作为新的校准记录点。
- SW APP 不需要消费 `EVT:STREAM` 的距离和体重字段，只消费正式状态事件。

当前合同里 `valid=1` 表示距离和体重组成的整条 measurement plane 都有效；当前固件在 `valid=0` 时可能只上报原因，不上报 `distance_mm`。因此“距离有效但体重无效时，Demo 仍显示并采样距离”不能只改 APP，必须在施工前二选一并冻结：

1. **保持现有合同**：Demo 只有在整条样本 `valid=1` 时消费距离/体重；距离状态验证依靠 ESP32 内部日志和正式状态事件，采样不宣称支持 distance-only 样本；
2. **增加可向后兼容的字段（推荐）**：保留 `valid` 的整体含义，同时增加 `distance_valid`、`weight_valid`、`distance_reason`、`weight_reason`，并允许距离有效时携带 `distance_mm`。旧 APP 仍按 `valid` 工作，新 Demo APP 才使用分离后的有效性。此路线必须同步固件编码、Kotlin 解析器、Demo 消费层、协议文档、golden frames 和测试，不能由 APP 猜字段。

本蓝图采用第 2 条作为目标，但在代码施工前必须先完成协议合同评审；在评审完成前，不得把当前 `valid` 字段静默改成“只代表距离有效”。

## 5. 施工步骤

### 第 0 步：建立回退点

- 保存当前工作区和现有 485 可用版本。
- 不执行破坏性恢复，不删除原有 485 代码。
- 记录当前分支、构建环境和已验证固件。

### 第 1 步：引脚和硬件核对

- 核对 PCB 原理图或走线：GPIO17/18、GPIO15/16。
- 确认 IIC 电源为 3.3V。
- 确认 IIC SDA/SCL 上拉和线长满足模块要求。
- 确认未占用 GPIO15/16 的其他硬件功能。

### 第 2 步：完成底层驱动

- 保留 485 驱动。
- 完成 IIC 驱动初始化、读寄存器、超时、越界和通信错误处理。
- 两个驱动都输出统一的 `MeasurementReadResult`。
- 确保无效距离不会进入体重和状态判断；体重模型未完成只阻断体重链，不阻断有效距离状态链。

### 第 3 步：接入统一业务链

- `LaserModule` 只消费统一测量结果。
- 保留原有状态机的动作 owner、状态转移、确认次数和恢复时序；先把输入信号从体重改为统一距离，再逐项验证阈值和单位。
- 体重算法独立保留，不能反过来决定激光设备的空载、有人、离开、律动和摔倒状态。
- 距离有效但校准模型无效时，距离状态链继续工作，体重链报告 `CALIBRATION_NOT_READY`。
- 485 和 IIC 只在适配层处理不同的单位、方向和错误码；上层不保留传感器分支。

### 第 4 步：修正 Demo APP 数据链路

- ESP32 输出 `distance_mm`。
- Demo APP 读取并直接保存该值。
- 校准记录示例：ESP32 输出 `distance_mm=120.00`，APP 必须记录 `120.00mm`。
- 不允许出现 `12000.00mm`。
- 没有 `distance_mm` 时，记录操作失败并提示数据不完整。

### 第 5 步：保留两个正式构建环境

过渡阶段保留：

```text
esp32s3_laser485
esp32s3_cyd3s1612_iic
```

`cyd3s1612_iic_test` 只用于硬件烟测，正式收口后可删除；两个正式环境不能删除，直到统一固件完成并验收。

这两个环境是过渡和回退入口，不是最终产品交付形态。最终产品交付形态为统一环境：

```text
esp32s3_unified
```

统一固件启动时按以下规则选择传感器：

1. 只检测到 IIC：使用 CYD3S1612-IIC；
2. 只检测到 485：使用王同学 485 测距仪；
3. 两个都检测到：报告硬件配置异常，不随机选择；
4. 两个都未检测到：进入测距不可用状态。

### 第 6 步：代码验证

必须通过：

```text
git diff --check
IIC 固件编译
485 固件编译
Demo APP 协议测试和编译
```

重点测试：

- IIC 有效距离、越界距离、总线错误；
- 485 有效距离和原有异常路径；
- `distance_mm` 解析；
- 校准点不重复换算；
- BLE 连接、重连和实时流订阅；
- SW APP 状态事件不退化。

### 第 7 步：真机验证

前置条件：

- 重新烧录统一固件；
- 安装包含协议修复的 Demo APP；
- ESP32 和手机各重启一次；
- 不清除 APP 数据，除非测试发现旧校准数据污染。

测试顺序：

```text
空载 → 遮挡/目标距离 → 固定距离读数 → 用户站稳
→ 开始使用 → 记录校准点 → 退出/离位 → 重新站稳
```

验收证据必须同时包含 ESP32 串口日志、Demo APP 接收日志和界面记录结果。

真机验证必须使用同一份 `esp32s3_unified` 固件，分别在 485 设备和 CYD3S1612-IIC 设备上完成验证。

## 6. 统一固件的后续阶段

统一固件不是在运行中随意切换传感器，而是在每次启动时完成一次受控探测，成功选择后锁定本次运行的测距后端。

### 6.1 统一探测合同

统一固件启动后按以下顺序执行：

1. 初始化 GPIO15/16 上的 IIC 总线，使用 CYD3S1612-IIC 的模块协议进行探测；
2. IIC 探测至少包含设备地址应答、距离寄存器读取和一次有效范围校验，不能只把总线 ACK 当成传感器可用；
3. 初始化 GPIO17/18 上的 485 UART，按现有王同学测距仪 Modbus 合同发送一次受控探测请求；
4. 485 探测必须区分超时、协议错误、设备无响应和有效响应，不能把任意串口数据当成测距仪存在；
5. 探测默认允许有限重试，重试次数和单次超时必须参数化并写入日志；
6. 只检测到 IIC 时锁定 `CYD3S1612_IIC`；
7. 只检测到 485 时锁定 `LASER_485`；
8. 两个都检测到时锁定 `SENSOR_CONFLICT`，禁止随机选择，也禁止把两个值直接融合；
9. 两个都未检测到时锁定 `SENSOR_UNAVAILABLE`；
10. 后端锁定后，本次运行期间不得因为一次读失败而自动切换到另一后端；
11. 已锁定后端发生临时失联时，进入现有测量健康状态机的不可用/恢复路径，并保留后端身份；
12. 只有设备重启或明确的受控重新探测流程，才允许重新选择后端。

统一探测必须输出结构化日志，至少包含：

```text
[SENSOR_PROBE] phase=start
[SENSOR_PROBE] backend=CYD3S1612_IIC result=pass|fail reason=<reason> attempts=<n>
[SENSOR_PROBE] backend=LASER_485 result=pass|fail reason=<reason> attempts=<n>
[SENSOR_PROBE] selected=<backend> lock=1
```

`selected` 只用于设备侧诊断和 OTA/现场证据，不属于 Demo APP 需要自行解释的正式业务分类。

### 6.1.1 统一探测状态矩阵

| 状态 | 进入条件 | 允许的测距后端 | 对业务层的表现 | 必须证据 |
|---|---|---|---|---|
| `BOOTING` | 上电或重启 | 无 | 不发布有效测量 | 启动日志 |
| `PROBING_IIC` | 基础启动自检完成 | IIC 探测中 | 不进入业务判断 | IIC 探测日志 |
| `PROBING_485` | IIC 探测完成 | 485 探测中 | 不进入业务判断 | 485 探测日志 |
| `SELECTED_IIC` | 仅 IIC 探测成功 | CYD3S1612-IIC | 正常消费统一测量结果 | 选择和锁定日志 |
| `SELECTED_485` | 仅 485 探测成功 | 王同学 485 | 正常消费统一测量结果 | 选择和锁定日志 |
| `SENSOR_CONFLICT` | IIC、485 都探测成功 | 无 | 测距不可用，不随机选择 | 冲突日志和不可用事件 |
| `SENSOR_UNAVAILABLE` | 两个都探测失败 | 无 | 测距不可用，不进入业务判断 | 失败原因和不可用事件 |
| `TEMPORARY_UNAVAILABLE` | 已锁定后端暂时读失败 | 已锁定后端 | 进入测量健康状态机恢复路径 | 后端身份、失败和恢复日志 |
| `RECOVERING` | 已锁定后端重新读到有效数据 | 已锁定后端 | 恢复正常测量，不切换后端 | 恢复日志 |

状态矩阵的硬规则是：`TEMPORARY_UNAVAILABLE` 和 `RECOVERING` 不能重新触发后端选择；只有重启或明确的受控重新探测才允许回到 `PROBING_IIC`。

### 6.1.2 探测决策表

| IIC 探测 | 485 探测 | 统一结果 | 后端锁定 | 业务允许发布有效测量 |
|---|---|---|---|---|
| 通过 | 失败 | `SELECTED_IIC` | `CYD3S1612_IIC` | 是 |
| 失败 | 通过 | `SELECTED_485` | `LASER_485` | 是 |
| 通过 | 通过 | `SENSOR_CONFLICT` | 无 | 否 |
| 失败 | 失败 | `SENSOR_UNAVAILABLE` | 无 | 否 |

探测结果必须由完整协议响应和有效数据共同决定。单独的 IIC ACK、485 串口电平或一次不完整响应都不能判定传感器可用。

### 6.1.3 探测参数初始约束

- IIC 目标初始上限为 3 次，单次通信超时沿用驱动的 50ms 参数；最终值以代码常量、看门狗预算和真机波形验证为准。
- 当前代码的 485 探测实际上只有 1 次（`kUnified485ProbeAttempts=1`），不能在蓝图中写成已经是 3 次；施工时先完成双路独立探测，再依据 Modbus 阻塞时长决定是否提高重试次数。
- 当前代码在 IIC 启动阶段同步等待约 1 秒；因此“不能阻塞 BLE 初始化”目前只是目标，不是已实现事实。施工前必须二选一：把这段等待纳入明确的启动预算并验证 OTA/看门狗，或改为异步探测。
- 所有探测失败必须记录后端、阶段、尝试次数、耗时和结构化原因码；不能只记录一个 `0x00`，因为“通信成功但距离越界”也必须能被区分。

### 6.2 统一固件生命周期

统一固件按以下阶段推进：

1. **创建阶段**：新增 `esp32s3_unified`，保留 `esp32s3_laser485`、`esp32s3_cyd3s1612_iic` 和 `cyd3s1612_iic_test` 作为过渡/回退入口；
2. **编译阶段**：统一固件必须在同一份源码中包含两个底层驱动，但上层仍只消费 `MeasurementReadResult`；
3. **静态验证阶段**：完成 `git diff --check`、统一固件编译、双后端探测路径检查、协议解析测试和 OTA 镜像大小检查；
4. **设备验证阶段**：使用同一份统一固件，分别在 485 设备和 CYD3S1612-IIC 设备上验证启动探测和后端锁定；
5. **业务验证阶段**：继续验证距离、`distance_mm`、体重、baseline、稳定、律动、离位、疑似摔倒和无效数据隔离；
6. **OTA 验证阶段**：确认统一固件使用现有 BLE OTA、目标板身份、双 OTA 分区、哈希校验、重启和回退机制；
7. **发布阶段**：统一固件通过两种设备的真机和 OTA 验收后，才作为新的唯一正式 OTA 发布包；
8. **清理阶段**：发布包确认可回退、现场证据归档后，才允许删除两个旧正式环境和 `cyd3s1612_iic_test`；
9. **回退阶段**：如果统一固件在任一传感器设备上失败，保留旧环境和已验证旧镜像，禁止继续清理或扩大协议变更。

### 6.3 字段和错误码所有权表

| 字段/概念 | 产生层 | 下游用途 | 是否为正式对外合同 |
|---|---|---|---|
| `rawDistanceMm` | CYD3S1612 驱动 | 单位转换、诊断 | 否，设备内部字段 |
| 485 原始寄存器值 | 485 驱动 | 旧解码、诊断 | 否，设备内部字段 |
| `scaledDistance` | 统一测量适配层 | 兼容现有内部计算，迁移期间不得作为传感器原始值 | 内部业务合同 |
| `NormalizedDistanceSample.distance_mm` | 统一测量适配层 | 距离状态、体重模型和 Demo 测量流 | 内部/正式物理距离合同 |
| `distance_mm` | ESP32 协议编码层 | Demo APP 显示、校准、分析 | 是，正式物理距离字段 |
| `validDistance` | 统一测量适配层 | 是否允许进入业务算法 | 内部业务合同 |
| `weight` | 体重测量链 | Demo APP 显示、校准结果和记录 | 正式测量字段；不作为激光状态唯一真值 |
| `main_state` / `start_ready` / `baseline_ready` | 距离状态链和状态机 | SW APP、Demo APP 状态显示 | 是，正式状态合同 |
| `invalidReason` | 驱动/适配层 | 设备日志和无效事件 | 仅按已定义枚举对外；调试文本不能替代状态码 |
| `selected_sensor` | 探测层 | 现场诊断、OTA 后证据 | 否，不由 APP 推导业务逻辑 |

任何驱动原始值都不得由 `LaserModule`、协议 APP 或 Demo APP 重新解释单位。ESP32 是物理距离单位换算的唯一 owner。

### 6.4 施工前验证矩阵

| 验证层 | 最小场景 | 通过条件 | 证据 |
|---|---|---|---|
| 静态接口 | 两个驱动同时编译 | 上层只依赖统一结果 | 编译输出、源码检查 |
| IIC 探测 | 有模块、无模块、读失败、越界 | 状态和原因符合探测决策表 | 串口探测日志 |
| 485 探测 | 有设备、无设备、超时、协议错误 | 不误选、不误报 | 串口/Modbus 日志 |
| 后端锁定 | 单设备启动、重启、临时失联 | 选择一次并保持，不自动串台 | 状态日志 |
| 测量节奏 | IIC 连续读取、485 连续读取 | IIC 不按 20ms 冒充 50Hz 新帧，485 保持原行为 | 时间戳和读取诊断 |
| 业务链 | 有效、越界、通信失败、模型未完成 | 距离状态与体重有效性分别处理 | `EVT:STREAM`、状态事件 |
| 校准链 | 0kg 点、一次拟合、二次拟合 | 两种模型都通过空载点并可回读 | APP 拟合日志、ESP32 回读 |
| 协议链 | `distance_mm`、`weight`、正式状态 | Demo 只保存 `distance_mm` 和体重；SW 只消费状态 | BLE 接收日志、APP 记录 |
| OTA | 统一固件升级、重启、回退 | 镜像可写入、身份正确、失败可回退 | OTA 状态、重启后身份 |
| 双硬件真机 | 同一统一镜像分别运行两种设备 | 两种设备都自动选择并完成业务链 | ESP32、APP、界面三方证据 |

### 6.5 进入代码施工的门槛

只有以下条件全部满足，才允许开始修改统一探测和统一固件代码：

1. 探测状态矩阵和决策表已冻结；
2. IIC、485、统一结果和协议字段的 owner 已冻结；
3. OTA 启动确认不依赖传感器探测的边界已确认；
4. 统一固件的构建入口和旧环境保留策略已确认；
5. 静态、协议、OTA 和真机证据的采集方式已准备；
6. 当前工作区回退点、分支和已验证旧镜像已记录。

在门槛完成前，允许做文档、状态矩阵、测试设计和采集准备；禁止删除旧环境、修改正式状态语义、修改 OTA 合同或重写业务算法。

### 6.6 距离化状态判断：复用边界和改造规则

当前代码审计结论是：原有判断函数可以复用“窗口、计数、确认、恢复、停波动作 owner”等结构，但不能把 `weight` 变量直接全文替换成 `distance`。J100-485 的律动主判断已按第 13.25 节确定以空载形变归一化，并沿用体重版 16%/28% 边界；空载/有人距离门槛仍需通过 0 kg、3 kg、5 kg 实测映射。CYD3S1612-IIC 和未来传感器分别建立自己的门槛和验证结果。

| 当前函数/逻辑 | 当前主要输入 | 目标输入 | 改造要求 |
|---|---|---|---|
| `updatePresenceState` | `weight` 与 `MIN_WEIGHT/LEAVE_TH` | 空载参考距离、距离偏移 | 改为进入/退出距离阈值和滞回；不再依赖体重校准结果 |
| `updateStableState` | 体重窗口，辅以距离 | 距离窗口 | 复用窗口、标准差、范围、漂移和确认次数；稳定基线保存距离 |
| `refreshBaselineFromStable` | 稳定体重 + 距离 | 稳定距离 | 距离是正式基线；体重只作为 Demo 诊断/测量信息 |
| `RhythmStateJudge::updatePrimaryState` | `MA12 weight`、体重偏差比例 | `MA12 distance`、本台空载参考 `D_e`、站稳距离基线 `D_b` | J100-485 按 `R_D = |MA12_D - D_b| / |D_b - D_e|` 归一化，沿用 16%/28% 边界和体重版时间规则；状态判断不依赖体重校准模型 |
| `updateFormalFallCandidate` | 体重速率/累计下降 + 距离下降 | 距离速率/累计下降 | 保留确认样本、保持样本和恢复逻辑；删除体重条件，增加方向归一化 |
| `updateRiskAdvisory` | 体重偏差风险 | 距离偏差风险 | 风险名称和日志字段必须明确是距离信号，不能伪装成体重分类 |
| `StartGateContractEvaluator` | 有人 + `baselineWeight >= MIN_WEIGHT` | 有人 + 距离基线就绪 | 激光路径不再用最低体重放行；BASE 无激光路径保持原合同 |

距离状态输入统一定义为：

```text
distance_mm                 当前物理距离
empty_distance_mm           本台设备空载参考距离
baseline_distance_mm        当前占用周期的稳定距离
delta_from_empty_mm         相对空载距离偏移
delta_from_baseline_mm      相对稳定基线偏移
velocity_mm_per_sec         距离变化速度
direction_normalized_delta  按传感器方向归一化后的变化量
```

J100-485 和 CYD3S1612-IIC 的原始方向可能不同，适配层必须把“受载方向”归一化。状态算法只消费 `direction_normalized_delta`，不能在状态函数内部判断传感器型号。

### 6.7 J100-485 律动比例与空载/有人门槛分开处理

体重公斤值不能直接替换成毫米偏差。J100-485 的律动主判断采用第 13.25 节已确认的相对形变比例：

```text
R_D = |MA12_D - D_b| / |D_b - D_e|
```

该映射建立在弹簧工作范围内“载荷与形变近似成正比”的假设上。分母表示开始律动前的受载形变，不是传感器到平台的绝对距离。实施前仍须用 J100 实测验证传感器方向、距离单位、有效工作范围和重复性；若实测不支持线性假设，应先复核模型，不得静默改动用户确认的 16%/28% 边界。

空载/有人门槛是另一套参数：当前体重版的 3 kg 退出门槛和 5 kg 进入门槛，可用逐台设备的 `D_e`、`D_3kg`、`D_5kg` 重复测量映射为距离回差。单次测量不够，需观察重复加载、卸载后的读数分布。现有距离代码里的 3 mm/1 mm 只是代码现值，不是测量结论。

Demo APP 的线性或二次体重拟合仍用于体重输出；J100-485 的状态判断直接使用距离和空载形变基准，不把拟合模型换算出的体重送入状态判断。CYD3S1612-IIC 和未来传感器须分别采数、配置和验收，不继承 J100-485 的距离门槛。

### 6.8 律动和摔倒算法的安全迁移顺序

不能一次性把正式状态直接切换到距离算法。应按以下顺序施工：

1. 给 `RhythmStateJudge` 增加距离基线、距离移动平均、距离偏差和距离速率的独立字段；
2. 保留当前体重算法作为诊断结果，新增距离算法的 shadow 结果；
3. 用同一段真机数据同时运行体重判断和距离判断，比较 `NORMAL`、`ABNORMAL`、`DANGER`、离位和摔倒候选的转移时刻；
4. 确认距离算法在空载、正常律动、回弹和快速下沉场景没有误触发后，再把距离结果接入正式状态 owner；
5. 正式切换后，体重结果仍继续上报给 Demo APP，但不再作为激光状态判断的必要条件。

正式状态字段仍由 ESP32 产生。若需要上报距离诊断，应新增明确的距离字段或设备日志；不得把原来名为 `stable_weight`、`deviation`、`ratio` 的字段悄悄改成距离含义。SW APP 只消费 `main_state`、`start_ready`、`baseline_ready`、停止原因和正式安全效果；Demo APP 才消费实时 `distance_mm` 和 `weight`。

### 6.9 空载、校准和运行时零点合同

三者必须分开：

```text
empty_distance_mm       本台设备的空载物理参考
calibration_model        通过空载点的体重模型
runtime_empty_window     运行中按距离确认空载的窗口
```

APP 操作顺序：

```text
参数归零
→ ESP32 清除旧体重模型和校准完成标志
→ 空载稳定后记录 0kg 点
→ 生成 (empty_distance_mm, 0kg)
→ 记录多个真实重量点
→ 一次/二次模型都强制通过空载点
→ 写入并回读 ESP32
```

拟合使用：

```text
u = distance - empty_distance_mm
一次：weight = c1*u
二次：weight = c0*u*u + c1*u
```

运行时空载只由距离和传感器对应容差判断。运行时零点观察器不得改变正式校准模型的参考距离；如保留观察器，只能作为诊断或空载窗口辅助，不能把新的临时零点用于正式体重换算。

### 6.10 施工前必须完成的验证矩阵

| 验证项 | 目标 | 证据 |
|---|---|---|
| 适配层 | 485/IIC 都输出同一物理距离接口 | 驱动单测、串口日志 |
| 空载 | 空载距离在容差内时状态为空载 | 距离日志、状态事件 |
| 0kg 定点 | 一次和二次模型都通过空载点 | APP 拟合结果、ESP32 回读 |
| 体重异常隔离 | 模型未完成或体重异常不阻断有效距离状态 | 状态日志、无效原因 |
| 稳定 | 距离窗口能确认站稳和离开 | 窗口统计日志 |
| 律动 | 正常律动不误报危险 | shadow 对比报告 |
| 摔倒 | 距离快速变化和持续保持满足才触发 | 事件日志、动作证据 |
| PLUS 降级 | 激光不可用时仍按既有规则进入律动 4 | CAP/状态日志、APP 操作结果 |
| BASE | 无激光版本行为不受影响 | BASE 固件回归测试 |
| APP 分工 | Demo 显示距离/体重，SW 只显示状态 | 两端接收日志 |

在距离 shadow 对比和校准定点验证完成前，禁止把距离算法直接作为正式停波或摔倒动作的唯一依据。

### 6.11 Demo APP 四条链路的同步改造

Demo APP 不是状态判断 owner，但它是测量数据的展示、校准工作流、采样证据和调试日志 owner。四条链路必须分别处理，不能继续把“距离有效、体重有效、设备可启动、日志是否显示”压缩成一个布尔值。

#### 6.11.1 连接和实时流前置顺序

Demo APP 已有显式实时流订阅能力，本次不新增另一套订阅机制，也不根据 APP 名称或设备名称猜测是否推流。每次连接的正式顺序固定为：

```text
连接 BLE
→ CAP?
→ 确认 stream_control_supported=1
→ STREAM:SET enabled=1,rate_hz=10
→ 等待 ACK:STREAM
→ 再开放实时显示、校准和采样入口
```

- `ACK:CAP` 只代表静态能力；`ACK:STREAM` 才代表本次连接已经开启实时流。
- 连接重建后必须重新订阅，不能沿用上一次连接的本地“已开启”标志。
- 没有实时流时，APP 应显示“没有实时测量数据”，不能把旧距离或旧体重继续当成当前样本。
- 关闭实时流只影响 BLE 上行，不影响 ESP32 内部测量、距离状态、baseline、安全判断和体重计算。

#### 6.11.2 校准链：参数归零、空载定点、拟合和写回

当前代码审计发现：

- `sendCalibrationZero()` 目前只发送 `CAL:ZERO`；固件当前实现会刷新参考距离，但不会清除已部署的体重模型；
- `CalibrationComparisonEngine` 目前把所有点的平均距离作为参考距离，并允许自由截距；
- APP 的“拟合可用”和“可以写入”没有把 `monotonic == true` 作为硬门槛；
- 当前模型写入收到 ACK 后没有强制执行 `CAL:GET_MODEL` 回读比对；
- 当前实时样本消费路径要求距离和体重同时有效，体重无效时会连距离一起丢弃。

当前固件已经有 `CAL:CAPTURE` 稳定采点路径，会返回 `ACK:CAL_POINT`；但 Demo APP 主按钮目前走的是瞬时 `APP_LIVE_SNAPSHOT`，两条路径没有统一。正式施工优先采用设备稳定采点：稳定窗口由 ESP32 按距离合同确认，APP 只提交参考体重并接收设备快照。若过渡期仍保留 APP 快照，必须同时收到明确的距离稳定合同并在点记录中标明来源，不能把两种来源混在同一个点集里。

目标交互顺序固定为：

```text
连接并确认 ACK:STREAM
→ 点击“参数归零”
→ APP 先归档本地旧点和旧拟合，再清空当前编辑态，按钮进入等待状态
→ ESP32 清除已部署体重模型、空载锚点、运行时零点和校准完成标志
→ ESP32 返回归零结果 ACK
→ 空载稳定后，参考体重输入 0kg
→ 点击“记录校准点”，APP 发送 CAL:CAPTURE w=0
→ ESP32 按距离稳定合同返回 ACK:CAL_POINT，APP 才把 distance_mm 记为本台设备 empty_distance_mm
→ 记录多个非零真实重量点
→ 一次和二次模型都强制通过 0kg 点
→ 选择模型并检查单调性、误差和点数
→ CAL:SET_MODEL 写入
→ CAL:GET_MODEL 回读并逐项比对
→ 回读一致后才显示“模型已生效”
```

拟合约束为：

```text
u = distance_mm - empty_distance_mm
一次：weight = c1 * u
二次：weight = c0 * u * u + c1 * u
c2 = 0
```

具体规则：

- `empty_distance_mm` 是每台设备的持久化参数，由本次 0kg 记录产生，不能写死成 112.56mm，也不能用所有点的平均值代替；
- 0kg 点必须是有效、稳定、没有重复冲突的距离样本；没有 0kg 点时，线性和二次结果都不可写入；
- 线性至少需要空载点加一个非零点；二次至少需要空载点加两个不同的非零点；实际验收仍要检查点间距和噪声；
- 校准非零点的最小参考体重若需要设置（例如设备机械结构要求大于某个下限），必须单独写入校准合同和提示文案，不能直接复用运行时的 `MIN_WEIGHT` 或离位阈值；
- `monotonic`、误差上限、距离范围、有限值和模型类型都是写入前置条件；拟合曲线显示“有结果”不等于允许写入；
- 高级工程区的手工 `ref/c0/c1/c2` 覆盖如果继续保留，也必须走同一套 0kg 锚点、有限值、距离范围和单调性校验，并明确标记为工程覆盖；不能用手工输入绕过自动拟合门禁；
- 录点保存 `distance_mm`、参考体重、序号、设备时间、APP 时间、样本有效性、距离稳定性和传感器后端；APP 不再自行把运行时单位换算成毫米；
- “参数归零”与“记录 0kg 点”是两个动作，前者清旧模型，后者建立新锚点；不能把一次动作同时伪装成两者。
- 归零失败时 APP 必须保留已归档的旧点和旧模型状态，不能显示“已清空”或静默丢失校准证据。
- 普通“开始记录”产生的 CSV/遥测会话与“记录校准点”是两个职责；校准点是否成功不能依赖 APP 是否正在录制普通日志，最多把普通录制作为附加证据。
- 当前固件 `CAL:CAPTURE` 的前置条件仍检查 `lastMeasurementValid`、稳定状态和稳定体重；距离化后应把“稳定距离”设为硬条件，体重只作为可选诊断，失败原因继续通过 `NOT_STABLE`、`INVALID_SAMPLE` 等明确返回。

`CAL:ZERO` 是否继续作为兼容命令名，必须在代码施工前冻结其新语义和 ACK/NACK 结果。不能让旧的“只改参考距离”语义与新的“清空模型”语义并存而由 APP 猜测。

#### 6.11.3 采样链：距离优先，体重可缺失

运动采样的目的，是收集验证距离状态算法所需的证据，不是重复执行固件状态判断。当前 `MotionSamplingSessionStore` 只有在距离和体重都存在时才追加行，这会在模型未完成或 CYD 体重暂时无效时丢失最重要的距离样本，必须调整。

采样开始顺序：

```text
确认已连接
→ 确认本次 ACK:STREAM enabled=1
→ 创建采样 session，记录设备、固件、后端、采样率、模型摘要和 empty_distance_mm
→ 发送既有 DEBUG:MOTION_SAMPLING（需要设备侧采样模式时）
→ 接收样本并逐行保存
→ 用户停止、断连或发生错误时保存明确停止原因
```

每一行的最低保存条件是 `distance_mm` 有效。建议字段分层如下：

| 数据 | APP 处理规则 |
|---|---|
| `distance_mm` | 有效就保存；是采样和距离变化计算的主数据 |
| `weight` | 可为空；保存 `weight_valid` 和无效原因，不用 0 代替缺失值 |
| `seq`、设备时间、APP 时间 | 必须保存，用于和 ESP32 日志对齐 |
| `main_state`、`start_ready`、`baseline_ready`、停止/安全原因 | 直接保存 ESP32 正式状态，不由 APP 重算 |
| `distance_valid`、`distance_invalid_reason`、传感器后端 | 保存测量健康证据；后端只作诊断字段 |
| 距离变化量/速度 | 可由 APP 计算用于图表，但必须标记为 APP 派生值 |
| 体重变化量/移动平均 | 只有 `weight_valid` 时计算，不能阻断距离行 |

CSV/JSON 需要升版 schema（不复用旧字段的旧含义），至少加入 `distance_valid`、`weight_valid`、各自原因、`sensor_backend`、`empty_distance_mm`、模型版本摘要、`start_ready`、`baseline_ready`、`main_state` 和 `state_source`。旧脚本若仍读取 `liveWeightKg`，缺失值必须能识别，不能静默写成 0kg。

#### 6.11.4 运行链：APP 只显示设备真相

运行按钮和运行页面遵守以下规则：

- Demo APP 的开始/停止按钮只读取 ESP32 的 `start_ready`、`main_state`、`baseline_ready`、正式 reason、safety effect 和波形输出确认；APP 不用本地体重或本地距离重新判定“是否就绪”；
- `BASE` 没有激光，继续保持原来的无激光可用逻辑；
- `PLUS` 有激光时以 ESP32 的正式 `start_ready` 为准；激光不可用时沿用现有 PLUS 降级到 BASE 律动 4 的交互，不把“没有实时测量流”误显示成普通未就绪；
- `EVT:STREAM` 只负责 Demo 的实时距离/体重显示、校准和采样，不负责替代正式状态事件；
- 距离有效但体重模型未完成时，Demo 仍显示距离并显示“体重未校准/不可用”，不得显示旧体重或把无效体重当成 0；
- 运行测试会话的样本模型也必须允许 `weight` 为空或单独无效；`main_state`、`start_ready`、`stop_reason` 等仍只能来自 ESP32 事件，不能因 APP 没有体重值而丢掉距离样本；
- 模型写入、参数归零或传感器失联导致 ESP32 重置 baseline/start_ready 时，APP 只等待并投影新的正式事件，不自行补写就绪状态；
- 重连后按 `CAP? → ACK:STREAM → SNAPSHOT?` 重新建立界面真相，清除旧连接残留的测量和运行状态。

#### 6.11.5 日志和证据链

当前 `RawConsoleStore` 为了保护 UI，默认过滤 `EVT:STREAM`；这个降噪策略可以保留，但不能因此丢掉校准、采样和状态验收证据。APP 应分成“界面日志”和“结构化证据”两层：

```text
命令发送
→ ACK/NACK
→ ESP32 执行动作/状态变化
→ 样本 seq/设备时间
→ APP 消费结果
→ 导出/回读结论
```

至少保留以下结构化标记：

- `[CAPABILITY]`、`[STREAM_CONTROL]`、`[STREAM_SUBSCRIPTION_RESULT]`；
- `[CAL_UI]`、`[CAL_ZERO]`、`[CAL_CAPTURE_ATTEMPT]`、`[CAL_CAPTURE_RESULT]`、`[CAL_FIT]`、`[CAL_WRITE]`、`[CAL_READBACK]`；
- `[MOTION_SAMPLE]`，包含 session、行号、seq、距离有效性、体重有效性和停止原因；
- `[RUN_UI]` 和 `[STATE_CONSUME]`，记录用户点击与设备正式结果的对应关系；
- `[MEASUREMENT_CONSUME]`，包含 `seq`、`distance_mm`、`weight`、各自 valid、reason、carrier 和协议模式。

每条证据至少要能按 `session_id + seq + device_ts + app_ts` 对齐。正式协议字段、APP 派生字段和 shadow/debug 字段必须分开标注；不得把 `stable_weight`、`deviation`、`ratio` 原字段名静默改成距离含义。

UI 可以节流显示实时流，但结构化证据应保留有界缓存并支持和 CSV/JSON 一起导出。当前 `TelemetryRecorder` 和 `onStreamSample` 在无效样本时会提前返回，不能完整留下断流、距离有效/体重无效和无效原因；施工时要补独立的结构化 evidence 记录，不要只依赖最多 200 行、默认过滤流的 raw console。若没有 Demo TX 原始行，但已有 `ACK:STREAM` 和 ESP32 `[STREAM_CONTROL]` 执行日志，订阅握手可判定完成；如果只是 APP 消费层没有结构化日志，应标记为证据缺口，不能继续扩大固件协议或算法改动。

#### 6.11.6 四条 APP 链路的 owner 表

| 功能 | ESP32 负责 | Demo APP 负责 | SW APP 负责 |
|---|---|---|---|
| 传感器解码和 `distance_mm` | 读取、方向和单位换算 | 直接显示/保存 | 不消费实时测量流 |
| 空载和状态 | 距离状态、baseline、`start_ready`、安全状态 | 显示正式事件 | 消费正式状态 |
| 校准模型 | 校验、持久化、运行时计算 | 点集、线性/二次拟合、0kg 锚点、写回和读回比对 | 不拟合 |
| 运动采样 | 提供测量和正式状态 | 距离优先保存、导出和图表 | 不采样 |
| 运行控制 | 状态机和输出确认 | 发送命令、显示设备真相 | 既有正式控制链保持不变 |
| 日志 | 设备动作、状态和原因 | 传输、消费、校准、采样证据 | 按既有正式日志合同消费 |

#### 6.11.7 APP 施工顺序

APP 改造按以下顺序进行，避免在协议和算法尚未冻结时反复改 UI：

1. 先冻结 `CAL:ZERO` 清模语义、0kg 锚点字段、模型读回结果和采样 schema；
2. 修改 `CalibrationComparison`、`CalibrationSessionStore` 和校准 UI 的可写入门槛；
3. 修改 `MeasurementDisplayStore` 和 `DemoViewModel.onStreamSample`，实现距离/体重有效性解耦；
4. 修改 `MotionSamplingSessionStore`、`MotionSamplingExporter`，保留距离优先样本和正式状态快照；
5. 修改运行页只投影 ESP32 状态，保留 BASE、PLUS 和 PLUS degraded-start 分支；
6. 补齐结构化日志、回读比对和自动化协议测试；
7. 最后与 ESP32 距离 shadow、统一固件和两种传感器真机联调。

#### 6.11.8 距离/体重分离字段的协议门槛

如果采用第 2 条路线，新增字段的正式含义固定为：

```text
valid=distance_valid && weight_valid
distance_valid=<0|1>
weight_valid=<0|1>
distance_mm=<distance_valid 时必须存在>
weight=<weight_valid 时才存在>
distance_reason=<距离无效原因，可选>
weight_reason=<体重无效原因，可选>
```

兼容规则：

- 旧客户端仍可把 `valid=0` 当作整条测量不可用，不会误把半条样本当成完整体重；
- 新 Demo APP 只有在 `distance_valid=1` 时更新距离显示和距离优先采样，在 `weight_valid=1` 时更新体重显示和体重派生量；
- `distance_valid=1, weight_valid=0` 时，APP 不得把缺失体重写成 0，也不得把上一帧体重冒充当前体重；
- SW APP 不增加实时流消费，只继续使用 ESP32 正式状态合同；
- 所有新增字段必须同时出现在固件编码、Kotlin `Event.StreamSample`、协议文档、golden frame fixture、单元测试和真机采集审计脚本中；
- 如果最终决定不扩展正式协议，则必须撤回 6.11.3 中的 distance-only APP 行为，不能留下半实现。

这一步是 Demo APP 施工的硬门槛，因为它决定“低精度传感器仍能验证距离状态”是否能被 APP 直接观察到。

#### 6.11.9 Demo APP 最小测试矩阵

| 链路 | 必测场景 | 通过条件 |
|---|---|---|
| 校准 | 归零成功/失败、0kg 锚点缺失、重复锚点、线性、二次、非单调、写回后重启 | 不满足锚点或单调性不能写；写入后 `CAL:GET_MODEL` 与本地模型一致；重启后模型不变 |
| 采样 | 距离和体重都有效、只有距离有效、只有体重无效、距离无效、断流、断连 | 有效距离逐行保留；缺失体重为空并有原因；无效行和停止原因可追溯 |
| 运行 | BASE、PLUS 正常激光、PLUS 激光不可用降级、模型写入后重新就绪、重连 | 按钮只跟随 ESP32 正式状态；不由 APP 本地体重重算；降级交互不回归 |
| 日志 | TX→ACK/NACK、状态变化、样本 seq/时间、APP 消费、导出 | 能按 session/seq 对齐；UI 过滤实时流不影响结构化证据；证据缺口明确标记 |
| 协议 | 旧客户端解析、增加分离有效性字段、golden frame、异常字段 | 旧 `valid` 语义不被静默改变；新字段缺失或非法时有明确失败结果 |

## 7. 禁止事项

- 不在 Demo APP 内按传感器型号换算距离。
- 不修改 BLE 正式状态字段的含义。
- 不为了 IIC 删除 485 代码。
- 不把调试日志当成正式协议输出。
- 不在未确认 PCB 方向前再次交换 UART 引脚。
- 不以“能读到一个数字”代替体重和状态链路验收。
- 不把体重阈值直接按一个固定斜率换算成距离阈值。
- 不在未经 shadow 对比和真机验证的情况下直接替换正式律动/摔倒动作条件。
- 不把正式校准零点和运行时空载观察零点混为一个可自动改写的字段。

## 8. 参考资料

- `3683674758CYD3S1612-IIC激光测距模块产品说明书_V1.3.pdf`
- `WTX-J系列说明书-V3.0模拟量切换版本.pdf`
- `JD系列485通讯操作流程-V1.0.pdf`
- [GlobalConfig.h](../../src/config/GlobalConfig.h)
- [LaserMeasurementReader.cpp](../../src/modules/laser/LaserMeasurementReader.cpp)
- [Cyd3s1612I2cReader.h](../../src/modules/laser/Cyd3s1612I2cReader.h)
- [RhythmStateJudge.h](../../src/modules/laser/RhythmStateJudge.h)
- [RhythmStateJudge.cpp](../../src/modules/laser/RhythmStateJudge.cpp)
- [LaserModule.cpp](../../src/modules/laser/LaserModule.cpp)
- [CalibrationComparison.kt](../../tools/android_demo/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/CalibrationComparison.kt)
- [CalibrationSessionStore.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/CalibrationSessionStore.kt)
- [DemoViewModel.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt)
- [MeasurementDisplayStore.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/MeasurementDisplayStore.kt)
- [MotionSamplingSessionStore.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/MotionSamplingSessionStore.kt)
- [MotionSamplingExporter.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/MotionSamplingExporter.kt)
- [SessionCaptureModels.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/SessionCaptureModels.kt)
- [TestSessionBridge.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/TestSessionBridge.kt)
- [TelemetryRecorder.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/TelemetryRecorder.kt)
- [RawConsoleStore.kt](../../tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/RawConsoleStore.kt)
- [ProtocolCodec.h](../../src/core/ProtocolCodec.h)
- [Demo APP ProtocolCodec.kt](../../tools/android_demo/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/ProtocolCodec.kt)
- [Demo APP 协议说明](../demo_app_protocol.md)
- [实时流订阅合同](./esp32_realtime_stream_subscription_contract.md)
- [ESP32-S3 引脚配置](./esp32s3_pin_profiles.md)
- [OTA 合同](./esp32_ble_firmware_ota_contract.md)
- [Unitronics：Tare & Zero](https://www.unitronicsplc.com/Download/SoftwareHelp/U90LadderKnowledgebase/Ladder/Functions/Special_functions/Loadcell/Tare___Zero.htm)
- [SICK：Laser Triangulation Sensors](https://www.sick.com/media/docs/9/39/539/product_information_laser_triangulation_sensors_dt20_hi_od_value_od1000_od_mini_od_max_od_precision_od5000_en_im0053539.pdf)

## 9. 多轮资料复核结果（2026-09-19）

### 9.1 原厂设备资料复核

已逐页复核项目内 `3683674758CYD3S1612-IIC激光测距模块产品说明书_V1.3.pdf`：

- 工作电压为 3.3V；
- 测量范围为 20mm～5000mm；
- 原厂给出的测量精度为 ±1cm 或 ≤4%（以说明书条件和较大误差者为准）；这不支持把 J100 的 ±0.10mm 空载容差直接套到 CYD；
- 数据更新时间约 30 帧/秒；
- IIC 默认 7 位地址为 `0x30`；
- 距离数据从 `0x01`、`0x02` 读取，高字节和低字节合成为毫米值；
- 上电初始化约 1 秒；
- IIC 引脚为 SCL、SDA，模块连接器顺序为 GND、SCL、SDA、VCC；
- 说明书给出的 IIC 上限为 500kHz，但这不是本项目必须使用的频率。

当前驱动使用 100kHz、地址 `0x30`、起始寄存器 `0x01`、读取 2 字节和 1 秒启动等待，和上述设备资料一致。

### 9.2 Espressif 官方资料复核

Espressif 官方 Arduino-ESP32 I2C API 明确支持：

```cpp
Wire.begin(sdaPin, sclPin, frequency);
```

因此当前 `Wire.begin(16, 15, 100000)` 的参数顺序正确：GPIO16 为 SDA，GPIO15 为 SCL。

ESP32-S3 官方 GPIO 表显示 GPIO15、GPIO16 可作为普通 GPIO，且不属于 ESP32-S3 的启动 strapping 引脚；GPIO19/20 才是默认 USB-JTAG 相关引脚，GPIO26～37 在 N16R8 上需要避让。最终是否能用仍需以本项目 PCB 走线为准。

Espressif I2C 文档还明确指出 SDA/SCL 是开漏线路，需要上拉，且频率受上拉电阻和总线电容影响。因此“GPIO15/16 可用”不等于“无需检查上拉”，蓝图保留了上拉和 PCB 实物核对步骤。

频率结论按项目实际工具链处理：当前固件固定 Arduino Core 2.0.17/ESP-IDF 4.4.x，模块手册允许到 500kHz，但本项目首选 100kHz；是否提高到 400/500kHz必须结合对应 IDF 版本、上拉、电容和逻辑波形验证，不能把不同版本文档的上限混写成一个无条件平台合同。

### 9.3 协议和分层复核

- ESP32 是距离单位换算的唯一 owner；
- Demo APP 只消费 `distance_mm`，不应按传感器型号或旧字段自行换算；
- 当前正式 Demo 测量字段是 `distance_mm`；Demo APP 不得把其他字段猜成毫米；
- IIC 和 485 必须先进入统一测量结果，再分别进入距离状态链和体重测量链；
- 距离有效但体重模型未完成时，距离状态可以继续工作，体重必须标记为未校准；
- `main_state`、`start_ready`、`baseline_ready` 和正式安全效果继续由 ESP32 产生；
- BLE 正式状态合同、OTA 合同和 SW APP 业务语义不在本次 IIC 施工中重写。

### 9.4 可行性结论

本蓝图在技术上可行，原因是：

1. 两种传感器已经分配到不同 GPIO，接口不会争用同一组线路；
2. ESP32-S3 GPIO 矩阵支持将 IIC 和 UART 外设路由到指定 GPIO；
3. IIC 设备的读取协议、单位和有效范围已有原厂资料支撑；
4. 现有业务层已经有统一测量结果入口，可以复用状态机的窗口、确认、恢复和动作 owner 结构；
5. 状态信号从体重迁移到距离需要独立阈值和 shadow 验证，不能只做变量替换；
6. Demo APP 已有 `distance_mm` 入口，校准拟合需要把参考距离改为 0kg 定点；
7. 现有 OTA 分区和 BLE 更新合同不要求因更换测距器件而改变。

### 9.5 仍需实物确认的事项

资料复核不能替代 PCB 和产品实测，以下事项在烧录前必须确认：

- PCB 的 GPIO15/16 确实已引出并分别接 IIC SCL/SDA；
- IIC 模块供电稳定在 3.3V，电流预算按说明书最大约 40mA 留余量；
- SDA/SCL 有合适上拉，且没有其他器件驱动同一总线；
- PCB 的 485 RXD/TXD 网络命名与当前固件引脚合同一致；
- 在目标深色铁板、安装角度和实际距离范围内，实测有效率、噪声和状态判断结果满足产品要求。
- CYD 模块若需要做盖板串扰/Offset 光学标定，必须按说明书的标定板尺寸、平行度和环境光条件单独执行；这不是 APP 的体重模型校准，当前驱动也没有宣称已经实现该 20 秒模块标定命令。
- J100(P)-485 的 9600 波特率、站号、±35mm 物理范围和“负值到真实距离”的方向，必须以实际设备配置、用户提供的型号表和原始帧共同冻结；`JD系列485通讯操作流程`不能单独证明 WTX-J100 的全部规格。
- 统一固件的“所有 ESP32”范围暂定为使用同一 N16R8 板、引脚 profile、分区表和 BLE OTA 合同的受支持设备；不同板型、不同引脚或不同 bootloader 不能仅靠传感器自动探测保证兼容。

### 9.6 复核后的边界

- 原厂资料是 CYD3S1612 寄存器、地址、单位和标定语义的设备真值；
- Espressif 资料只证明 GPIO/I2C API 和电气通用规则，不替代 PCB 原理图和设备说明书；
- “IIC 能读到距离”不等于“体重、状态判断和 Demo 记录已验收”；
- “两个正式环境能编译”不等于“统一固件已经完成”；
- 真机验收必须同时看 ESP32 日志、Demo APP 接收/记录结果和用户操作结果。

外部官方参考：

- Espressif Arduino-ESP32 I2C API：<https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html>
- Espressif ESP32-S3 GPIO：<https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html>
- Espressif ESP32-S3 硬件设计指南：<https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html>
- Espressif I2C 电气与上拉说明：<https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32/api-reference/peripherals/i2c.html>

## 10. E3 修订记录与施工状态（2026-09-20）

### 10.1 本次代码审计确认

- 当前 `LaserModule::updatePresenceState` 仍以体重为输入；
- 当前 `updateStableState` 同时使用体重窗口和距离；
- 当前 `RhythmStateJudge` 的主判断使用稳定体重、MA12 体重和体重偏差比例，摔倒候选同时使用体重和距离；
- 当前 `StartGateContractEvaluator` 的激光路径仍要求有效体重基线；
- 当前 `CalibrationComparison` 已做横坐标平移，但参考距离是所有校准点的平均值，不是 0kg 点距离；
- 当前固件已有 NVS 模型保存和读取能力；
- 当前运行时零点观察器存在“把临时空载零点用于体重换算”的路径，后续必须与正式校准零点隔离。
- 当前 Demo APP 的校准主按钮走瞬时 `APP_LIVE_SNAPSHOT`，而固件已有 `CAL:CAPTURE` 稳定采点路径，两者需要统一为一条有明确 owner 的路径；
- 当前 Demo APP 拟合参考距离取所有点平均值，线性/二次模型允许自由 `c2`，不能保证通过 0kg 点；
- 当前 Demo APP 的拟合结果可用性没有把 `monotonic == true` 作为写入硬门槛，写入成功后也没有强制 `CAL:GET_MODEL` 回读核对；
- 当前 Demo APP 的 `MeasurementDisplayStore`、`onStreamSample` 和运动采样要求距离、体重同时有效，不能验证“距离有效但体重不可用”的目标路径；
- 当前 Demo APP 的 raw console 默认过滤实时流，无效样本也不完整进入普通录制文件，需新增独立结构化证据链；
- 当前正式 `EVT:STREAM` 的 `valid` 是整条距离+体重样本有效标志；若要让 Demo 直接消费 distance-only 样本，必须采用新增分离有效性字段的协议扩展并同步所有测试。

### 10.2 本次蓝图确定的目标

- 保留原有状态机的窗口、计数、确认、恢复和动作 owner 结构；
- 将激光设备状态判断的正式输入迁移为统一距离；
- 将体重模型限制为测量、显示、记录和校准用途；
- 一次和二次拟合都必须经过 APP 记录的 0kg 空载定点；
- 不把 `MIN_WEIGHT`、`LEAVE_TH` 或体重比例直接当成毫米阈值；
- 先 shadow 对比，再接入正式律动和摔倒动作；
- BASE 和 PLUS 的既有产品边界保持不变；
- Demo APP 的校准、采样、运行和日志按 6.11 的四条链路施工，不能由 APP 重新推导 ESP32 正式状态。

### 10.3 当前允许和禁止的动作

当前允许：

- 更新蓝图、接口、状态矩阵和测试设计；
- 增加距离状态输入的纯算法测试；
- 采集 J100-485 和 CYD3S1612-IIC 的成对距离/体重真机数据；
- 修改 APP 拟合算法，使参考距离来自 0kg 点；
- 在协议合同评审通过后，补齐 Demo APP 的 distance/weight 分离字段、采样 schema 和结构化证据。

当前禁止：

- 直接把生产状态判断中的 `weight` 全文替换成 `distance`；
- 未经 shadow 对比就改变正式摔倒停波条件；
- 把原有 `stable_weight`、`deviation`、`ratio` 字段静默改成距离含义；
- 在统一固件验收前删除旧构建环境或改变 OTA 合同；
- 只修改 Demo APP 的 `valid` 判断而不同时修改固件字段、协议解析器和测试。

## 11. E4 多轮公开资料与代码交叉审计（2026-09-20）

本节记录本轮多次检索、原厂 PDF 复核和代码对照的结果。它的作用是把“资料支持的原则”“本项目已经做到的事实”和“必须靠实机才能确定的参数”分开，避免把建议误写成已经完成的功能。

### 11.1 证据等级和结论边界

| 证据等级 | 资料 | 能证明什么 | 不能证明什么 |
|---|---|---|---|
| 设备真值 | 项目内 `CYD3S1612-IIC` V1.3 PDF | 地址、寄存器、单位、上电等待、量程和模块自身标定步骤 | 不能证明本装配结构的噪声、空载容差或体重拟合精度 |
| 设备/现场真值 | 用户提供的 J100(P)-485 型号表、实际原始帧和 PCB 接线 | 该批设备的中心距离、方向、波特率/站号和有效范围 | 通用 JD 流程 PDF 不能替代具体 WTX-J100 型号数据 |
| 平台规则 | Espressif I2C、GPIO、OTA 文档 | GPIO/I2C 电气约束、OTA 双槽和回滚机制 | 不能替代 PCB 连通性、传感器协议或业务阈值 |
| 通用测量原则 | NIST/EURAMET、ISO/IEC 17025、OIML 参考 | 重复性、迟滞、漂移、残差、可追溯记录和稳定采样的重要性 | 不能直接给出本项目的毫米阈值，也不自动要求本项目成为法定计量器具 |
| 通用 BLE 规则 | Bluetooth SIG GATT、Android BLE 文档 | Notification 无 ATT 层确认，必须由客户端配置并重新订阅 | 不能替代本项目 `STREAM:SET`/`ACK:STREAM` 的应用层合同 |

公开资料支持设计方向，但 SW 的正式字段、状态和兼容语义仍以本仓库合同和两种设备的实测证据为准。

### 11.2 审计总判定

| 审计项 | 判定 | 施工含义 |
|---|---|---|
| CYD3S1612-IIC 地址/寄存器/单位/3.3V/约 1 秒启动 | 已有原厂依据，当前读取代码基本一致 | 可保留当前底层协议；仍要补质量、帧新鲜度和安装条件验证 |
| GPIO15/16、GPIO17/18 的分层和单位 owner | 设计正确，仍受 PCB 实物约束 | ESP32 继续作为唯一换算 owner，烧录前做通断和波形确认 |
| 统一探测的四种结果 | 蓝图正确，当前代码未完全实现 | 必须先修探测，再进入业务迁移；当前 IIC 成功会跳过 485，冲突分支不可达 |
| 探测失败原因和质量门禁 | 不完整 | 不能只看 ACK、串口电平或一次读数；要区分通信、越界、重复帧和质量不足 |
| 距离替代体重作为激光状态输入 | 可行但尚未验收 | 先 shadow，同步保留动作 owner、确认次数和恢复时序；阈值按传感器 profile 冻结 |
| 0kg 定点约束拟合 | 作为本产品物理合同合理 | 不能称为统计学普遍规则；仍须报告残差、方向、输出范围和不确定度，超限就拒绝写入 |
| `CAL:ZERO` 清空模型、再记录 0kg 点 | 需求明确，当前代码未实现 | 要增加 `calibration_ready` 和事务式持久化，不能让默认模型冒充已校准模型 |
| Demo APP distance/weight 解耦 | 协议路径已设计，当前消费层仍整体 valid | 必须先冻结新增字段，再同步固件、Kotlin、导出格式和 golden frame |
| BLE 实时流订阅 | 现有 `STREAM:SET` 方向正确 | 连接后显式订阅，按 `ACK:STREAM`/设备日志判定，不依赖 APP 名称 |
| OTA 统一发布 | 技术上可行但有硬件范围 | “所有 ESP32”应解释为所有受支持的同板型/引脚/分区设备，不能泛化到任意 ESP32 |

因此蓝图总体方向可行，但当前不能宣称“统一探测、距离状态、清模事务和 distance-only APP 已完成”。这些是施工前的硬缺口。

### 11.3 必须先修正的 P0/P1 缺口

#### P0-1：双路探测必须真正独立执行

当前 `src/modules/laser/LaserMeasurementReader.cpp` 的统一路径是：IIC 探测成功后不再调用 485 探测。因此同时安装两种传感器时，不会得到 `SENSOR_CONFLICT`，而会错误选择 IIC。这与 6.1 的正式合同矛盾。

施工要求：

1. 每次启动都分别执行 IIC 和 485 的有限探测；不能用“先成功就短路”的逻辑。
2. 探测结果使用结构化对象，至少包含 `transport_ok`、`sample_valid`、`reason_code`、`attempts`、`elapsed_ms` 和 `last_value`。
3. IIC 读到完整两字节但超出 20～5000mm 时，原因必须是明确的越界/无效数据，而不是 `0x00` 成功码。
4. 485 Modbus 响应成功但解码后越界时，也必须判探测失败并给出解码原因。
5. 两路结果都完成后，才按决策表锁定后端；锁定后运行中不自动串台。

当前代码的 `kUnified485ProbeAttempts` 是 1，而蓝图旧文案曾写成 3。施工前不预先假定 3 次：先测量一次 Modbus 阻塞时长和看门狗余量，再冻结重试上限。蓝图和代码必须使用同一个常量来源。

#### P0-2：校准模型的“未准备”必须是持久化状态

当前 `CalibrationModelStore` 逐项写入 `mdl_t`、`mdl_ref`、`mdl_c0/c1/c2`，没有 `calibration_ready`、版本、代际或提交标记；无模型时还会回退到默认线性模型。这样“参数归零”之后，体重链可能继续输出一个看起来合法的默认结果。

目标存储记录至少包含：

```text
schema_version
generation
calibration_ready
model_type
empty_distance_mm
coefficients
payload_length
crc
commit_marker
```

写入顺序必须是“写新代际 → 校验 → 写提交标记 → 原子切换当前代际”；掉电或写失败时保留上一份完整模型。启动、`CAL:GET_MODEL`、OTA 后和重启后都要验证 `calibration_ready`、代际和 CRC。NVS 跨重启/断电的能力不等于业务模型已经具备事务语义。

#### P0-3：`CAL:ZERO`、模块光学标定和 0kg 体重点必须分开

三件事不能使用同一个含义：

- **模块光学标定**：CYD 说明书中的盖板串扰/Offset 标定，写寄存器 `0x05=0xC1`，约 20 秒，并需按原厂标定板、平行度和环境光条件执行；当前驱动没有实现该命令。
- **参数归零/清模**：APP 对 ESP32 发出的动作，清空当前体重模型、清除 `calibration_ready`，并保留可审计的旧点集副本。
- **0kg 空载定点**：清模后设备稳定空载，APP 输入 `0kg` 并记录该次 `distance_mm`，设置本台设备的 `empty_distance_mm`。

目标交互顺序固定为：

```text
连接并确认实时流
→ 点击“参数归零”
→ ESP32 返回成功/失败，成功后 calibration_ready=0
→ 用户确认空载并等待距离稳定
→ 输入 0kg，点击“记录校准点”
→ ESP32/APP 返回并保存 empty_distance_mm
→ 再记录非零点
→ 一次/二次拟合和门禁
→ CAL:SET_MODEL
→ CAL:GET_MODEL 回读并比对
```

清模不能在设备运行或有人状态下悄悄发生；正式写入和回读必须有命令、ACK/NACK、原因和代际证据。

#### P1-1：传感器质量、帧新鲜度和精度必须进入统一接口

公开的激光传感器资料普遍把“通信成功”和“测量可信”分开；滤波、迟滞、重复性和稳定窗口也会改变可用带宽。`NormalizedDistanceSample` 不能只有一个 `valid`，目标字段至少为：

```text
raw_value
physical_distance_mm
transport_ok
sample_valid
quality_code
invalid_reason
device_timestamp
freshness/sequence
```

CYD 说明书给出约 30 帧/秒、±1cm 或 ≤4% 的精度边界；因此：

- 33ms 只能作为读取节奏初始值，不能保证每次读取都是新帧；要记录时间戳/序号并处理重复帧；
- CYD V1.3 没有公开帧计数/新鲜度寄存器，静止目标下连续相同距离不能单独判为重复帧；只能用主控时间戳、模块约 30fps 能力和通信连续性做 freshness 诊断，严格的新帧判定要等原厂补充或实际状态信号；
- J100 的 ±0.10mm 经验容差不能直接复用到 CYD；CYD 的空载容差、稳定窗口和质量门限必须由同一安装结构的重复采样、2σ/迟滞和有效率数据确定；
- 原厂没有给 CYD 定义 NDS03 的 `65300` 哨兵语义，不能把 NDS03 寄存器规则直接移植到 CYD；CYD 只能按其说明书和实测确认的范围/状态解释。

历史烟测曾出现“总线传输全部成功但有效距离只有约 91.2%”的记录（512 帧中有无效越界值）。这只能证明当时的通信完成率，不能证明测量质量达标；后续报告必须同时给出通信成功率、有效测量率、连续无效最长时长和状态误触发率，并由产品门槛决定是否可用。

状态链应使用 profile 中冻结的 `quality + sample_valid + physical_distance_mm`，而不是仅凭一个可解析数字进入状态机。

统一接口还要避免把两种协议的原始字段混在一起。当前 IIC 路径把无符号毫米值强转到 `signedRaw`，并把 IIC 错误写入 `modbusResult`；例如越界值可能被误显示成负的 485 原始值。目标是：

- `raw_distance_mm` 只表示 IIC 的无符号毫米原始值；
- `signed_raw`、`modbus_result` 只在 485 适配器和诊断层使用；
- 上层统一读取 `physical_distance_mm`、`transport_error`、`sample_valid` 和 `invalid_reason`；
- 短读、NACK、超时、越界和解码失败使用不同原因码，不能用“收到字节数”伪装成 I2C 错误码。

#### P1-2：距离状态阈值必须是 profile 参数，不是全局魔法数

每种后端至少要独立记录：

```text
raw_to_physical_direction
load_direction                 # 受载时距离增大或减小
empty_tolerance_mm
presence_enter/exit_threshold
stable_window_ms
stable_spread_mm
confirm_count
recovery_count
motion/fall_window_ms
max_gap_ms
```

SICK 等成熟传感器资料把窗口、hysteresis、switch-on/off delay、debounce 和 signal quality 分开；这些资料支持参数化方法，但不能直接提供 J100/CYD 的数值。正式阈值必须用两种传感器成对日志 shadow 验证，再写入各自 profile。

#### P1-3：全仓引脚合同和历史资料必须统一

旧版本的 `test_firmware/cyd3s1612_iic_test/main.cpp` 和 `docs/system/esp32_cyd3s1612_iic_dual_firmware_plan.md` 曾打印/记录 `SDA=GPIO18/SCL=GPIO17`，与 active profile 和正式驱动的 `SDA=16/SCL=15` 冲突；烟测提示已在本轮修正，但旧文档和其他入口仍必须做全仓扫描：

- 正式入口、烟测入口和接线提示全部以 `docs/system/esp32s3_pin_profiles.md` 为当前合同；
- 旧计划保留为历史记录时，顶部必须显式标记“已废止，不得按其接线施工”，并链接本蓝图；
- 烟测程序若继续保留，打印的 SDA/SCL 必须与当前 profile 一致，否则不能作为验收入口；
- 任何 `65300` 记录都标成历史观测/越界值，不能写成 CYD 原厂错误码，也不能将 NDS03 的哨兵规则移植到 CYD。

#### P1-4：IIC 总线恢复要有明确边界

`Wire.setTimeOut(50)`只能限制一次事务等待，不能保证 SDA 被拉低或总线锁死后自动恢复。若产品要求运行中恢复，必须基于实际 PCB 确认是否有模块复位/供电控制，然后定义“重新初始化 I2C 控制器、重新 `begin`、必要时受控重启”的单一策略，并记录恢复原因；不能把超时后继续轮询当作总线恢复成功。

#### P1-5：Demo APP 的校准和采样不能依赖普通录制状态

当前 `CalibrationSessionStore` 的可用性还要求普通 `isRecording`，`DemoViewModel.sendCalibrationCapture()` 走 APP 瞬时快照；这会让“记录校准点”被普通 CSV/遥测录制按钮间接控制。目标流程应由实时流订阅和 ESP32 稳定采点合同控制，校准点不依赖普通录制会话。若过渡期保留旧路径，UI 必须明确提示用户先建立录制会话，并在证据中标明 `APP_LIVE_SNAPSHOT`，不能把它冒充 `CAL:CAPTURE`。

同时，当前 `CAL:ZERO` 没有在收到清模 ACK 后归档/清空本地点集，模型写入 ACK 后也没有强制 `CAL:GET_MODEL` 逐项回读；这两项必须列为 APP 施工阻断项。运动采样当前要求距离和体重同时非空，后续应将体重字段改为可空，距离行独立保存，只有相邻体重都有效时才计算体重变化量。

拟合实现还要检查点间距/矩阵秩和病态情况。固定空载点后使用无截距设计矩阵 `[u]` 或 `[u²,u]`，输出条件数或最小距离间隔、MAE、最大误差、残差趋势和方向门禁；不能仅因“有系数”或 R² 较高就允许写入。

### 11.4 校准模型的严谨合同

0kg 定点是本产品的物理约束，用来消除用户观察到的空载截距漂移；它不是公开统计标准要求所有回归都强制过零。NIST 明确建议先看截距意义、残差和不确定度，不能为了过点而接受一条坏曲线。

正式模型仍保留两种：

```text
u = distance_mm - empty_distance_mm
linear:    weight_kg = c1 * u
quadratic: weight_kg = c0 * u^2 + c1 * u
```

`c2` 在两种正式模型中都应为 0；但写入门禁必须同时检查：

1. 0kg 点误差不超过本台设备的空载容差；
2. 有效距离区间内预测值有限、非负且不超过产品上限；
3. 线性斜率或二次导数在预先冻结的受载方向上持续同号，并高于斜率容差；
4. 平均绝对误差、最大绝对误差、重复性、上行/下行迟滞和残差结构满足产品门限；
5. 约束模型与非约束模型都输出诊断结果；约束模型误差超限时拒绝写入，不能只因为“通过空载点”就放行；
6. 线性最少需要空载点加一个非零点，二次最少需要空载点加两个不同非零点；生产验收建议按 OIML/NIST 的成熟做法增加多个递增、递减和重复点，但这不等于本项目自动宣称法定计量合规。

APP 当前以所有点平均距离作为参考距离、线性/二次都允许自由截距，这正是必须修改的地方；`isAvailable` 也必须把 `monotonic == true` 作为可写入条件，而不是只判断该字段非空。固件端还要重复做有限值、方向、输出范围和模型版本校验，不能只信 APP。

### 11.5 485 合同的证据边界

当前代码使用 9600/站号 1 等配置只能称为“当前设备配置假设”，不能写成所有 J100 的原厂固定合同。J100(P)-485 的物理中心、±35mm、原始负值方向和真实距离换算必须由以下三者共同确认：

1. 用户提供的具体型号表；
2. 现场读取到的原始 Modbus 帧和寄存器配置；
3. 同一安装结构下的实际距离对照。

蓝图中应分开写：原始协议有效范围、工程保护范围、物理规格范围。当前代码允许约 ±35.70mm 的保护范围不能自动升级为 ±35mm 厂规，也不能反过来把保护范围当作真实精度。

### 11.6 统一固件和 OTA 的可行性边界

ESP-IDF 官方 OTA 机制支持双 app 槽、OTA data 分区和失败回滚；PlatformIO 可分别构建并监视各环境。2026-09-20 在当前工作区执行的验证结果为：

- `python3 tools/run_evaluator_unit_tests.py`：通过；
- `tools/android_demo` 的 `:sonicwave-protocol:test`：通过；
- `pio run -e esp32s3_unified`：通过；
- `pio run -e esp32s3_laser485`：通过；
- `pio run -e esp32s3_cyd3s1612_iic`：通过。

这些结果只证明当前源码和构建入口可编译、协议单元测试可运行，不证明双路探测、距离状态、校准和真机 OTA 已验收。统一 OTA 还必须单独验证：

```text
统一镜像身份/版本
→ 写入未使用 OTA app 槽
→ 重启并完成新镜像自检
→ 保留 NVS 校准模型和 schema
→ 正常确认或故障回滚
→ 回读模型、后端选择和正式状态
```

OTA 前后、重启和回滚分别取证；不能只看“升级成功”就推断校准参数和状态合同没有变化。

### 11.7 施工前最终门槛和顺序

按以下顺序推进，任何一步失败都停在该层，不扩大改动：

1. **合同层**：冻结双路独立探测、reason 枚举、quality/freshness 字段、`CAL:ZERO` 清模语义、`calibration_ready` 和 APP 新增字段。
2. **驱动层**：修正两路探测短路；补齐探测原因、连续有效读数、重复帧处理和启动时间预算；确认 485 配置和 17/18 接线。
3. **模型存储层**：实现带版本/CRC/提交标记的模型事务，清模后明确无模型状态，写后强制回读。
4. **距离 shadow 层**：保留现有动作 owner，按 profile 计算距离状态并与旧体重状态并行记录；不直接改变摔倒停波。
5. **校准层**：APP 以人工记录的 0kg 点设置 `empty_distance_mm`，一次/二次约束拟合，完成残差、方向、范围和重复性门禁。
6. **APP 四链路层**：校准、距离优先采样、运行状态投影、结构化日志逐条改造，并补 golden frame/导出 schema 测试。
7. **双设备真机层**：同一统一镜像分别接 J100-485 和 CYD3S1612-IIC，采集 ESP32、Demo APP 和操作结果；重点验证空载漂移、开始就绪、离位、律动、摔倒和无效数据隔离。
8. **OTA 层**：在两种后端都通过后做 BLE OTA、重启、断电/回滚和模型回读；最后才考虑删除旧入口。

双设备真机层还必须先完成 CYD 可行性判定：分别采集空载、站人、正常律动、离位和快速下沉数据，计算每类的距离变化量、窗口标准差、最小类间间隔、连续无效时长和误触发率。若占用或动作位移小于约 1～2 个实测噪声标准差，不能宣称 CYD 能可靠承担该状态；PLUS 应按既有激光不可用降级路径处理。这里的“1～2σ”只是工程筛查启发式，不是 NIST/OIML 的硬标准；最终仍以漏检率、误触发率、覆盖率和安全验收冻结。

### 11.8 本轮公开资料索引

以下链接是本轮实际核对的官方或成熟资料。它们用于支持工程原则，不替代项目内传感器 PDF 和真机证据：

- [Espressif Arduino-ESP32 I2C API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html)：SDA/SCL 上拉、主从和 `begin` 参数。
- [Espressif ESP-IDF I2C 指南](https://docs.espressif.com/projects/esp-idf/en/v5.2.2/esp32/api-reference/peripherals/i2c.html)：开漏总线、100/400kHz 平台能力、上拉和线容影响。
- [Espressif ESP32-S3 OTA 指南](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-reference/system/ota.html)：双 OTA 槽、OTA data、首次启动确认和回滚。
- [Bluetooth SIG GATT Core](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host/generic-attribute-profile--gatt-.html)：Notification 无 ATT 确认、CCCD 配置和 Indication 区别。
- [NIST Gauge Study](https://www.itl.nist.gov/div898/handbook/mpc/section4/mpc46.htm)：重复性、再现性、分辨率、非线性、迟滞和漂移的评估思路。
- [NIST 模型残差验证](https://www.itl.nist.gov/div898/handbook/pmd/section4/pmd44.htm)：R² 不能单独证明模型正确，需看残差结构和 lack-of-fit。
- [NIST 校准曲线分析](https://www.itl.nist.gov/div898/handbook/mpc/section3/mpc365.htm)：截距、线性/二次项、残差和异常值的复核。
- [NIST 推荐校准间隔](https://www.nist.gov/calibrations/recommended-calibration-interval)：用控制图和设备自身稳定性确定周期，不能凭一个固定数字推断。
- [EURAMET Calibration Guide No.18](https://www.nist.gov/document/i-cal-gui-018calibrationguideno18webpdf)：无载指示、分辨率、重复性、漂移和迟滞进入不确定度的历史成熟示例；不当作本项目现行强制标准。
- [ISO/IEC 17025 介绍](https://www.iso.org/publication/PUB100424.html)：校准方法、能力和可信结果的通用框架。
- [Unitronics Tare & Zero](https://www.unitronicsplc.com/Download/SoftwareHelp/U90LadderKnowledgebase/Ladder/Functions/Special_functions/Loadcell/Tare___Zero.htm)：稳定空载、零点动作、保存和回读的工程示例。
- [OIML R60](https://www.oiml.org/en/files/pdf_r/r060-e00.pdf)：递增/递减加载、重复性、迟滞和蠕变的成熟验收参考；本项目不是按该文件做 load-cell 型式评价，不据此宣称法定计量合规。
- [SICK 激光三角传感器资料](https://www.sick.com/media/docs/9/39/539/product_information_laser_triangulation_sensors_dt20_hi_od_value_od1000_od_mini_od_max_od_precision_od5000_en_im0053539.pdf)：平均、迟滞和传感器性能参数化的实例。
- [SciPy 有界最小二乘](https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.least_squares.html)：带边界和鲁棒损失的拟合实现参考；不直接规定本项目模型。

以下补充资料用于交叉核对：

- [ST VL53L1X 测量状态指南](https://www.st.com/resource/en/user_manual/um2510-a-guide-to-using-vl53l1x-ultra-lite-driver-stmicroelectronics.pdf)：把 signal/sigma/out-of-bounds 等质量失败与通信状态分开；这里只作为成熟传感器接口设计参考。
- [Android BLE 数据传输指南](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)：服务发现、显式订阅和回调证据；不能替代本项目应用层流合同。
- [Arduino-ESP32 Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html)：NVS 跨重启/掉电保存的基础能力；应用层仍需 schema、CRC 和提交事务。
- [ISO 5725-3 公共预览](https://www.iso.org/obp/ui?_escaped_fragment_=iso%3Astd%3Aiso%3A5725%3A-3%3Aed-2%3Av1%3Aen)：区分重复性、中间精密度和再现性；不直接给出本项目阈值。

本轮审计结论：蓝图的总体架构可以继续使用。E4 记录的是施工前的缺口，不代表 E5 之后的当前代码状态；当前实现和剩余验收门见第 12 节。

## 12. E5 当前施工状态（2026-09-21）

### 12.1 已落地的代码合同

- `esp32s3_unified` 每次启动独立探测 IIC 和 485；只检测到一路就锁定该后端，双路同时成功返回 `SENSOR_CONFLICT`，都失败返回 `SENSOR_UNAVAILABLE`。
- 485 J100 原始相对位移由 ESP32 转为真实物理距离：`distance_mm = 100 - displacement_mm`。例如原始 `-12.25 mm` 对外就是 `112.25 mm`；Demo APP 不再二次换算。
- CYD3S1612-IIC 使用 `0x30`、寄存器 `0x01`、GPIO15/16、100 kHz、20～5000 mm 合同；IIC 传输失败、短读和越界都有独立原因。
- 有激光设备时，距离链独立判断空载、在位、稳定、离位、律动、摔倒候选和 `start_ready`；BASE 无激光路径保持原逻辑，PLUS 激光不可用时沿用降级语义。
- 体重模型只使用持久化正式参考距离计算；运行时零点观察器即使记录诊断值，也不会移动已部署模型的正式零点。
- `CAL:ZERO` 是“参数归零/清模”，清除正式模型、运行时状态和空载引用；它不会把当前距离偷偷写成新零点。清模后，0kg 录点只要求距离稳定，非零录点才要求有人基线稳定。
- NVS 模型具有 schema、generation、CRC、commit marker 和上一份有效模型备份；启动时无有效模型会报告 `ready=0`，不会把默认占位模型当成已校准模型。
- Demo APP 的线性、二次拟合都以 APP 记录的 0kg 距离为必过定点，`c2=0`；写入后自动 `CAL:GET_MODEL` 回读，只有参数一致才显示部署成功。
- `EVT:STREAM` 明确分开 `distance_valid`、`weight_valid`、各自原因和 `backend`；距离有效而体重未校准时，实时距离、采样和日志仍保留。

### 12.2 当前验证结果

- `python3 tools/run_evaluator_unit_tests.py`：通过。
- `./gradlew :sonicwave-protocol:test :app-demo:compileDebugKotlin`：通过。
- `python3 -m platformio run -e esp32s3_unified`：通过。
- `python3 -m platformio run -e esp32s3_laser485 -e esp32s3_cyd3s1612_iic`：通过。
- `git diff --check`：通过。

这些结果证明代码、协议和三套构建入口能工作，不能替代传感器实物和 OTA 的验收证据。

### 12.3 仍未闭合的真机门

1. 用同一份 `esp32s3_unified` 固件分别接 J100-485 和 CYD3S1612-IIC，留存启动探测日志和连续 `EVT:STREAM`；还要用双设备和无设备场景验证冲突/不可用分支。
2. 核对实际 PCB 的 GPIO17/18 485 通断、波特率、站号和原始帧，确认 `-12.25 mm → 112.25 mm` 的端到端显示和记录结果。
3. 按每台设备在 APP 中执行“参数归零 → 记录 0kg → 记录非零点 → 拟合 → 写入 → 回读 → 重启”，确认空载不漂移，且 112.56/112.57 这类微小回弹波动只受设备容差影响。
4. 验证 BASE、PLUS 有激光、PLUS 激光不可用降级三条状态路径，以及 SW APP 只消费正式状态字段。
5. 在两种后端都完成 BLE OTA、重启保留模型、异常升级回滚和回滚后模型回读。

### 12.4 施工后操作顺序

Demo APP 的正式校准路径固定为：连接 BLE → `CAP?` → `STREAM:SET` 并收到 `ACK:STREAM` → 点击“参数归零”并等待 `ACK:CAL_ZERO` → 空载稳定、输入 0kg、记录点 → 记录非零点 → 选择一次或二次拟合 → 检查单调性/范围/误差 → 写入模型 → 自动回读核对 → 重启复核。SW APP 不参与这些校准和距离判断。

注：这是冻结后的目标交互合同，不代表当前页面已经完全实现。E6 审计确认当前可见“参数归零”仍映射到 `SCALE:ZERO`，必须完成第 13.3.1 节修复后，才能把这条路径标记为通过。

## 13. E6/E7 Demo APP 页面审计、资料复核与施工闭环（2026-09-22）

### 13.1 本章目的和证据边界

本章把 Demo APP 当前五个主页面及其共用 `DemoViewModel`、协议解析、会话存储和导出链路纳入施工蓝图。审计结论只依据当前工作区代码、单元测试结果、现有协议/校准文档和已核对的 Android 官方测试资料；没有把“能编译”或“能启动”当成页面验收。

当前页面入口为：

```text
DEVICE       设备型号、设备配置、摔倒/离位保护
CALIBRATION  参数归零、记录校准点、拟合、写入和回读
SAMPLING     律动采样、标注、曲线和 CSV/JSON 导出
RUN          状态、实时曲线、测试记录、开始/停止
LOGS         原始日志、复制和清空
```

统一底部运行控制条在所有页面显示，因此运行控制、连接状态和能力探测属于共享链路，不能只检查单个页面的按钮颜色。

### 13.2 审计总判定

Demo APP 当前不能作为“已完成”交付，原因不是所有页面都已损坏，而是存在以下未闭合的高风险问题：

| 编号 | 页面 | 等级 | 结论 |
|---|---|---:|---|
| DEMO-CAL-P0-001 | 校准 | P0 | 页面显示“参数归零”，实际发送 `SCALE:ZERO`；真正清空模型的 `CAL:ZERO` 只在高级区域。 |
| DEMO-CAL-P1-002 | 校准 | P1 | 已冻结 PRIMARY 使用 `CAL:CAPTURE` 稳定采点；仍需补齐超时、回读绑定和断线保留验收。 |
| DEMO-CAL-P1-003 | 校准 | P1 | `canDeployModel()` 没有调用；手工模型只要能解析成 Float 就可能进入写入路径，缺少空载点、范围、有限值和截距硬门槛。 |
| DEMO-CAL-P1-004 | 校准 | P1 | 校准采集、归零、模型写入和回读缺少完整超时；模型写入按钮未按 pending 状态禁用，存在快速重复写入和回读串线风险。 |
| DEMO-CAL-P1-005 | 校准 | P1 | BLE 断开或重新连接会清空本地校准点、拟合结果和待写模型。 |
| DEMO-SAMPLE-P1-001 | 采样 | P1 | 未连接时仍可开始采样，会创建没有设备数据的 `UNKNOWN` 会话。 |
| DEMO-SAMPLE-P1-002 | 采样 | P1 | 采样中断线后会话被清空；无效距离样本直接丢弃，故障证据无法导出。 |
| DEMO-RUN-P2-001 | 运行 | P2 | 频率输入只检查数字，不检查 5～50 Hz；超范围值先显示可开始，发送时再静默改成边界值。 |
| DEMO-DEVICE-P2-001 | 设备 | P2 | 能力探测尚未完成时，保护开关可能允许发送 PRIMARY 命令。 |
| DEMO-TEST-P1-001 | 全局 | P1 | 没有 `androidTest` 页面测试；历史审计曾有 5 个单元测试失败，虽已修正为 85/85，通过仍不能替代页面回归测试。 |
| DEMO-STATE-P1-002 | 全局 | P1 | 校准点和采样会话主要存于 ViewModel/内存；Android 进程被系统回收后，数据可能无法恢复。 |

### 13.3 校准页施工合同

#### 13.3.1 参数归零必须和设备基准归零分开

正式交互必须固定为：

```text
校准页
→ 点击“参数归零”
→ 确认清空校准参数
→ 发送 CAL:ZERO
→ 等待 ACK:CAL_ZERO
→ APP 清空本地点和拟合结果
```

`SCALE:ZERO` 只允许出现在明确标注“设备基准/比例归零”的工程入口，不能继续使用“参数归零”这个名称。修复必须同时更新：

- `MainScreen.kt` 的回调映射；
- `CalibrationToolsSection.kt` 的按钮和高级区域；
- 中英文确认文案；
- `CAL:ZERO` 成功、失败和超时状态；
- 校准流程文档和页面测试。

验收要求：点击页面上唯一的“参数归零”按钮后，日志必须出现 `CAL:ZERO`，设备成功事件必须清除模型；不得出现 `SCALE:ZERO`。

#### 13.3.2 校准点 owner 必须唯一

在代码施工前冻结以下二选一合同，不能两种路径并存而靠协议模式隐式决定：

```text
方案 A：APP 本地快照
点击记录 → 立即读取当前有效 distance_mm → 本地追加点 → 立即重算拟合

方案 B：ESP32 稳定采集
点击记录 → 发送 CAL:CAPTURE → 等待 EVT:CAL_POINT → 本地追加设备返回点
```

当前正式主路径冻结为方案 B：PRIMARY 统一使用 ESP32 的 `CAL:CAPTURE` 稳定采点；LEGACY 仅保留兼容回退，使用 APP 快照时必须显式标记为 `APP_LIVE_SNAPSHOT`，不得与正式点集混用。施工时必须保留日志中的 `capture_route`。无论哪条路径，0kg 点都必须来自 APP 明确输入的 `reference_weight=0`，并成为本台设备的 `empty_distance_mm`。

#### 13.3.3 模型写入必须统一经过硬门槛

写入按钮和 ViewModel 必须共用一个校验函数，至少检查：

1. BLE 已连接且协议能力为 PRIMARY；
2. 存在有效 0kg 空载点；
3. 所选一次或二次拟合 `isAvailable=true`；
4. `referenceDistance`、`c0`、`c1`、`c2` 全部有限；
5. `c2` 在固件约束容差内为 0；
6. 距离、模型输出、单调性、误差和重复性均通过当前后端门槛；
7. 没有正在进行的 `CAL:SET_MODEL` 或 `CAL:GET_MODEL`；
8. 写入成功后必须自动回读，参数逐项比较通过才显示成功。

`canDeployModel()` 不能继续作为未调用的死代码；要么接入按钮和发送函数，要么删除后将等价规则集中到唯一的模型门禁函数。

#### 13.3.4 校准异步状态必须可恢复

`CAL:CAPTURE`、`CAL:ZERO`、`CAL:SET_MODEL`、`CAL:GET_MODEL` 均需要：

```text
开始请求
→ 按钮进入 pending 并禁用
→ 收到成功/失败事件后清除 pending
→ 超时自动清除 pending 并显示可重试原因
→ BLE 断开时清理请求，但保留本地校准点和采样数据
```

重复点击不能覆盖 `pendingModelReadback`。回读必须绑定写入事务 ID 或至少绑定本次期望模型的完整参数，避免旧事件匹配到新请求。

### 13.4 采样页施工合同

采样页正式操作顺序固定为：

```text
连接 BLE
→ 完成 CAP? 和 STREAM:SET
→ 收到 ACK:STREAM
→ 点击“开始采样”
→ 记录有效距离、体重、有效性、原因、状态和时间戳
→ 点击“停止采样”
→ 选择场景和备注
→ 导出 CSV/JSON
```

施工要求：

- 未连接、未订阅实时流时，“开始采样”必须禁用；
- BLE 短暂断开后保留已采集会话，状态改为“因断线停止”，允许重新连接后导出；
- 无效距离样本不能静默丢弃。至少要以一行 `distance_valid=0`、`reason`、连接状态和设备时间进入故障采样文件；
- `ma12`、稳定体重、速度和导出字段必须检查有限值；
- CSV 和 JSON 导出失败时不能留下“已导出”标记；
- 导出文件名和 JSON schema 版本必须稳定，不能因页面文案变化而改变内部字段名。

### 13.5 运行页和底部控制条施工合同

运行页的状态来源仍然是 ESP32，Demo APP 只显示和发送操作命令。距离和体重显示可以分别有效，但不能让 APP 自行推断正式 `start_ready`、离位、律动或摔倒状态。

运行控制施工要求：

- 频率输入在 5～50 Hz 之外必须直接标为无效，不能静默截断；
- 开始按钮必须同时检查连接、设备能力、正式状态、保护状态、参数范围和 pending 状态；
- Base 无激光路径保持原有降级语义；PLUS 激光不可用时只能按已冻结的 degraded-start 合同显示和操作；
- `UNKNOWN` 能力探测期间不允许发送保护或运行控制命令；
- 断线、异常停止和恢复后的状态必须来自设备事件或正式快照，不从 APP 本地旧状态推断。

### 13.6 设备保护页施工合同

保护开关的正式路径为：

```text
连接
→ CAP? / 能力探测完成
→ 确认当前协议为 PRIMARY 且设备声明支持
→ 用户点击开关
→ 发送命令并进入 pending
→ 收到 ACK 和能力回读
→ 显示设备最终状态
```

任何能力未知、协议未知、设备不支持或断线状态都必须禁用开关。保护事件和命令回读必须清除同步标记；超时必须显示失败并允许重试。

### 13.7 日志、状态和图表页审计结论

- 日志页的 200 行上限、复制和清空逻辑暂时可保留；默认隐藏高频 `EVT:STREAM` 属于降噪策略，但不能影响结构化证据记录。
- 状态卡是显示层，没有发现新的直接写入错误；它依赖中心状态清理，因此必须覆盖断线、重连和能力探测中的状态变化。
- 图表页的距离、体重、稳定体重和移动平均显示逻辑方向正确；缺少有效体重时不得显示旧移动平均值。
- 测试记录页的清空和导出按钮基本有状态门禁，但断线后的会话保留必须和采样页统一。

### 13.8 当前验证证据和施工门槛

截至本章审计时：

```text
:sonicwave-protocol:test                         通过
:app-demo:compileDebugKotlin                     通过
:app-demo:assembleDebug                          通过
:app-demo:testDebugUnitTest                      通过：85/85
app-demo/src/androidTest                         不存在
```

此前 5 个失败测试涉及空载锚点、`distanceValid` 和无效样本移动平均语义，已按本章新合同重写并通过。由于仍然没有 `androidTest`，这些结果不能证明页面交互和 BLE 真机链路已经通过。

Android 官方测试资料明确区分本地单元测试、组件/功能测试和真机应用测试；按钮可用性、页面状态转换、弹窗确认和 BLE 交互不能只靠 JVM 单元测试证明：

- https://developer.android.com/training/testing/fundamentals/strategies
- https://developer.android.com/training/testing/ui-tests
- https://developer.android.com/tools/logcat?hl=zh-CN

### 13.9 本轮施工顺序

1. 先修 `DEMO-CAL-P0-001`，确认“参数归零”唯一发送 `CAL:ZERO`。
2. 固定校准点 owner，删除 PRIMARY/LEGACY 之间未经说明的语义分叉。
3. 接入统一模型门禁、有限值校验、pending 防重入和超时。
4. 增加校准和采样 session 的本地持久化，或明确并实现前台-only 生命周期限制。
5. 修复断线后的校准和采样会话保留。
6. 修复采样启动条件、无效样本记录和导出一致性。
7. 修复运行频率范围和 UNKNOWN 能力期间的控制按钮。
8. 重写 5 个失败单元测试，补充页面级 UI smoke test：连接、参数归零、0kg 记录、第二点记录、拟合、写入回读、采样导出、运行开始/停止、保护开关。
9. 通过自动化测试后，再做手机和 ESP32 真机逐页验收；最后才做 OTA 和旧入口清理。

本章完成后，原有蓝图的统一固件、距离链、体重链、协议和 OTA 目标继续有效；但 Demo APP 在本章列出的缺陷关闭、测试全绿和真机逐页验收前，不得标记为施工完成。

### 13.10 E6 第一施工单元结果（2026-09-22）

- 已修复 `DEMO-CAL-P0-001`：校准页可见“参数归零”现在进入 `CAL:ZERO` 确认流程；`SCALE:ZERO` 不再由该按钮触发。
- 已按 0kg 空载定点和 `distanceValid` 新合同更新校准与显示层单元测试。
- `:sonicwave-protocol:test`：通过。
- `:app-demo:compileDebugKotlin`：通过。
- `:app-demo:testDebugUnitTest`：85/85 通过。
- `:app-demo:assembleDebug`：通过。

剩余最高优先级仍是 `DEMO-CAL-P1-002` 至 `DEMO-CAL-P1-005`：统一校准点 owner、模型写入硬门槛、异步超时/防重入和断线数据保留。完成这些代码修改并重新通过测试后，才进入采样页和真机逐页验收。

### 13.11 E6 第二施工单元结果（2026-09-22）

- 已将模型写入按钮接入统一部署门禁：连接、PRIMARY、0kg 定点、拟合可用、有限值、`c2=0` 和单调性必须同时满足。
- `CAL:SET_MODEL` 和回读 pending 状态下禁止重复写入；ViewModel 发送入口再次执行相同门禁，不能靠 UI 按钮状态作为唯一保护。
- 新增中文和英文失败提示，区分未连接、重复写入和模型尚不可部署。
- `:sonicwave-protocol:test`、`:app-demo:testDebugUnitTest`（85/85）和 `:app-demo:assembleDebug` 均通过。

本单元仍未关闭模型回读超时、校准点 owner 冲突和断线保留问题；这些属于下一施工单元，不得因为模型按钮已经加门禁而提前标记校准链完成。

### 13.12 E7 公开资料复核与蓝图修订（2026-09-22）

本轮复核了 Android 官方应用状态、BLE GATT 通知、Android 12+ 蓝牙权限和 Espressif OTA 文档，并与当前代码和蓝图逐项对照。

#### 13.12.1 Android 状态保存边界

Android 官方说明：`ViewModel` 适合配置变化期间的内存状态，但不能保证系统进程被回收后恢复；需要长期保留的应用数据应进入本地持久化存储。当前 Demo APP 的校准点、拟合结果和采样行主要保存在 `DemoViewModel` 字段中，因此“BLE 断线保留”还不够，必须补充“进程回收保留”边界。

修订后的合同：

```text
校准点/采样行产生
→ 先更新 ViewModel，保证页面立即刷新
→ 异步写入本地 session 文件或 DataStore/数据库
→ BLE 断开只结束设备采集，不删除 session
→ APP 重启后按 sessionId 恢复未导出会话
→ 用户明确点击“清空”后才删除
```

`rememberSaveable` 只用于轻量页面状态，例如当前页签、弹窗和输入框；不能用来保存完整校准点或高频采样数据。若产品明确规定 Demo APP 只在前台运行，则应在页面和验收表中写明“后台持续采样不属于本版本能力”，不能让用户误以为切到后台仍会可靠收数。

#### 13.12.2 BLE 通知证据链

Android 官方 BLE 流程要求连接后完成服务发现，配置特征通知及其 CCCD，并在 `onCharacteristicChanged` 等回调中接收数据。项目应用层的 `STREAM:SET`/`ACK:STREAM` 不能代替底层通知配置证据。

Demo APP 真机验收必须同时留存：

1. GATT 连接成功；
2. UART 服务和 RX/TX 特征发现成功；
3. RX 特征通知/CCCD 配置成功；
4. 发送 `STREAM:SET enabled=1,rate_hz=10`；
5. 收到 `ACK:STREAM enabled=1`；
6. 连续收到带 `distance_mm`、有效性和 `backend` 的 `EVT:STREAM`。

任何一层失败都应显示对应层级原因，不能统一显示成“没有实时数据”。Android 12 及以上的 `BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT` 权限仍需在运行时完成授权；当前 Manifest 和权限代码已覆盖这一点，但仍需把拒绝、撤销后重试纳入真机测试。

#### 13.12.3 OTA 首启动确认

Espressif 官方 OTA 文档要求：启用回滚时，新镜像首次启动必须完成快速自检，成功后标记镜像有效，失败则标记无效并回滚。蓝图原有“升级、重启、回滚”表述过于宽泛，现修订为：

```text
BLE OTA 写入备用 app 槽
→ 重启进入新镜像
→ 首启动自检：启动、NVS 模型读取、传感器探测、BLE、正式状态输出
→ 自检通过：确认新镜像有效
→ 自检失败/超时：标记无效并回滚
→ 回滚后再次回读模型、后端和协议版本
```

OTA 验收不能只看 APP 显示“发送完成”，必须有 ESP32 启动日志和最终镜像状态证据。

#### 13.12.4 复核后的新增施工门

- `DEMO-STATE-P1-002` 加入 P1：校准和采样 session 必须具备本地持久化或明确的前台-only 产品限制；
- BLE 验收表增加 GATT 服务发现、CCCD、应用层 ACK 三层证据；
- OTA 验收表增加首启动自检、确认有效和回滚后的模型回读；
- 蓝图中的“真机通过”定义为链路证据完整，不再只以页面上显示出一个数字作为通过条件。

公开资料边界：Android 和 Espressif 文档只提供通用平台规则，不能替代本项目的 BLE 协议、传感器量程、校准模型和状态合同；项目合同仍以本蓝图、`docs/protocol.md`、`docs/demo_app_protocol.md` 和真机证据为准。

资料索引：

- https://developer.android.com/topic/libraries/architecture/saving-states
- https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data
- https://developer.android.com/develop/connectivity/bluetooth/bt-permissions
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html

### 13.13 E7 第三施工单元结果（2026-09-22）

本单元完成校准异步请求的第一轮收口：

- `CAL:CAPTURE`、`CAL:ZERO`、`CAL:SET_MODEL` 和 `CAL:GET_MODEL` 增加统一 10 秒超时；超时会清除 pending 状态、记录日志并给出可重试提示。
- 发送失败、设备错误、成功事件和 BLE 断开都会取消对应超时任务，避免旧请求在新会话中误报。
- 模型写入仍需先通过统一 `canDeployModel()` 门禁，成功 ACK 后必须继续回读比对；回读超时不会显示“模型已生效”。
- PRIMARY 校准点 owner 已冻结为 ESP32 稳定采点；LEGACY 快照路径只作为明确标注的兼容回退。

验证证据：

```text
./gradlew :app-demo:testDebugUnitTest :app-demo:assembleDebug --no-build-cache
BUILD SUCCESSFUL
app-demo 单元测试：85/85 通过
Debug APK：assembleDebug 通过
```

仍未关闭的施工门：

1. 校准点、拟合结果和采样行的 Android 进程回收持久化；
2. 断线后保留本地会话，但清理 BLE 请求 pending；
3. 采样页连接/实时流门禁、无效距离行保留和频率范围校验；
4. Compose `androidTest`、真机 BLE、ESP32 OTA 首启动回滚证据。

### 13.14 E7 第四施工单元结果（2026-09-22）

已完成传输断开后的本地会话保留：

- 主动断开、被动断开和重新连接不再调用 `CalibrationSessionStore.reset()`，已有校准点、拟合结果和待部署模型继续留在 Demo APP 当前会话中。
- 采样会话不再因 BLE 断开被直接清空；活动采样会先结束为断开状态，数据仍可导出或复核。
- BLE 请求 pending 状态仍会清理，避免旧连接的 `CAL:*` 回应污染新连接。
- “参数归零”和页面上的“清空校准点”仍然是明确的主动清除动作，断线不会替代它们。

本单元验证：

```text
./gradlew :app-demo:testDebugUnitTest --no-build-cache
BUILD SUCCESSFUL
```

本单元尚未声称完成 Android 进程被系统回收后的恢复；该问题仍需用本地持久化存储单独施工和验证。

### 13.15 E7 第五施工单元结果（2026-09-22）

已完成 Android 本地会话持久化和采样入口第一轮门禁：

- 新增 `DemoSessionPersistence`，以版本化内部 JSON 文件保存校准点、当前拟合模型、选中的模型类型和采样会话明细。
- Demo APP 启动时恢复这些用户证据；BLE 连接状态、实时流状态和请求 pending 不从文件恢复，而是重新建立。
- 高频采样采用 250ms 防抖写入，避免每条实时数据都同步写盘。
- 采样开始前必须满足：已连接、PRIMARY 协议、通知已开启且最近收到实时流；否则只显示原因，不创建采样会话。
- 采样行即使距离无效也保留，距离记为无效值并记录 `distanceValid`/原因，避免丢失故障证据。

验证证据：

```text
./gradlew :app-demo:testDebugUnitTest :app-demo:assembleDebug --no-build-cache
BUILD SUCCESSFUL
app-demo 单元测试：85/85 通过
Debug APK：assembleDebug 通过
```

下一步是安装该 APK 到已连接手机，按“连接 → 等待实时流 → 启动采样 → 断开 → 重启 Demo APP → 查看会话恢复”逐步验收；之后再做真实 BLE 通知链和 ESP32 OTA 首启动证据。

### 13.16 真机安装前检查结果（2026-09-22）

已使用外置硬盘 Android SDK 将最新 Debug APK 安装到已连接 Redmi K30 Pro Zoom Edition：

```text
adb devices -l       -> device usb
adb install -r       -> Success
com.sonicwave.demo   -> MainActivity resumed
```

这里只证明 APK 能安装并启动，没有把“没有崩溃”误认为 BLE、持久化恢复或 ESP32 OTA 已验收。下一步需要在手机和 ESP32 同时在线时，按单步交互顺序验证实时流和采样门禁。

### 13.17 真机持久化恢复证据（用户确认，2026-09-22）

用户已在安装后的 Demo APP 中确认：

- 重启 APP 后，已记录的校准点仍然存在；
- 已采集的采样数据仍然存在。

这证明当前版本的基本进程重建恢复链路可用。BLE 断开重连后的保留和旧请求隔离仍待下一步单独验证。

### 13.18 真机 BLE 断开重连证据（用户确认，2026-09-22）

用户已确认断开 ESP32 后重新连接通过：

- 校准点仍然存在；
- 采样数据仍然存在；
- 新连接可以继续操作；
- 未观察到旧校准请求状态污染新连接。

下一项只验证正常实时流下的采样开始、采集和停止。

### 13.19 真机采样主流程证据（用户确认，2026-09-22）

用户已确认正常实时流下：

- 可以开始采样；
- 采样行正常增加；
- 可以停止采样；
- 重启 APP 后采样会话仍然存在。

下一步验证无实时流时的采样门禁。

### 13.20 真机采样门禁证据（用户确认，2026-09-22）

用户已确认无实时流时点击开始采样会被阻止，未创建新的采样会话。采样门禁第一轮验收通过。

下一步核对 BLE 实时通知证据链：连接、服务发现、通知订阅、`STREAM:SET`、`ACK:STREAM` 和持续 `EVT:STREAM`。

### 13.21 BLE 实时通知链代码与日志证据（2026-09-22）

用户只需确认“连接成功且实时距离更新”；内部协议证据由工程侧从手机日志核对。ADB 日志已捕获：

```text
ACK:STREAM enabled=1 supported=1 rate_hz=10
EVT:STREAM seq=258850 ... distance_mm=112.43 ... backend=LASER_485
EVT:STREAM seq=258984 ... distance_mm=112.43 ... backend=LASER_485
```

同时捕获 Android GATT 服务发现和通知注册记录。该证据说明当前 J100-485 链路已经完成实时通知并持续收到统一毫米距离；不要求用户在 APP 界面辨认协议字段。

### 13.22 CYD3S1612-IIC 真机通讯证据（用户确认，2026-09-22）

用户已确认当前接线下 CYD3S1612-IIC 可以正常通讯：

- D → GPIO16（SDA）；
- C → GPIO15（SCL）；
- 统一固件可以收到 IIC 数据。

引脚配置、IIC 驱动和统一固件探测链路通过。下一步单独核对 IIC 输出的真实距离数值。

### 13.23 CYD3S1612-IIC 距离精度首轮结果（用户确认，2026-09-22）

用户实测目标约 100 mm，Demo APP 显示约 110 mm，固定偏差约 +10 mm。

当前判断：

- 该结果落在项目内 CYD3S1612-IIC V1.3 资料记录的约 ±10 mm 或 ≤4% 精度量级内，不能仅凭这一点判定固件换算错误；
- 当前 IIC 驱动读取的是模块输出的绝对毫米值，没有额外加 10 mm 偏移；
- 下一步必须使用钢尺/卡尺从传感器的规定测量基准面量到目标面，固定同一目标和角度，连续记录多个距离点，区分“安装基准偏移”和“传感器精度误差”；
- 在没有完成这组数据前，不能直接在 ESP32 中硬编码 `-10 mm` 修正；校准模型的空载距离应继续由 APP 逐台设备设置。

### 13.24 J100-485 距离状态链代码复核（2026-09-22）

本轮按用户要求复核了现有实现，结论是：J100-485 已经进入距离状态链，不需要再做“体重变量全文替换”。当前正式路径为：

```text
J100-485 原始值
→ ESP32 解码为真实距离
→ DistanceStateChain 判断有人/空载/稳定/基线
→ RhythmStateJudge.useDistance=true
→ 以距离偏差、距离速率和距离持续时间判断异常/危险/摔倒
→ SystemStateMachine 继续作为启停和停波动作 owner
```

复核依据：

- `LaserModule.cpp` 对 `LASER_485` 和 `CYD3S1612_IIC` 均启用 `distanceStateEnabled`；
- `RhythmStateJudgeInput.useDistance` 在激光后端为真，主判断使用距离安全带、危险带和距离变化；
- `StartGateContractEvaluator` 在距离模式下使用 `baselineReadyDistance`，不要求体重达到最小值；
- 体重仍只用于校准计算、显示和诊断，不作为激光设备运行状态的主判断输入。

验证：

```text
python3 tools/run_evaluator_unit_tests.py   -> evaluator unit tests passed
python3 -m platformio run -e esp32s3_unified -> SUCCESS
```

下一步是真机用 J100-485 验证距离状态链的行为：空载、有人站上、稳定、开始就绪、离台和异常距离变化；不能只看体重显示。

### 13.25 J100-485 律动判断复用体重百分比（用户确认，2026-09-23）

本节只定义 J100-485 的目标判断规则。CYD3S1612-IIC 不继承 J100 的距离阈值；更换为其他激光测距传感器时，也应为新传感器单独建立并验证距离判断参数。

用户确认：J100-485 继续沿用体重版主判断的 16% 安全边界和 28% 危险边界。依据是律动仪的弹簧受力形变在工作范围内按胡克定律近似线性，因此体重相对偏差可映射为弹簧形变相对偏差。换算时必须以空载距离为零形变参考，不能把传感器到平台的绝对距离直接作为百分比的分母。

公式中的 J100 距离必须是 ESP32 按 J100 profile 转换后的真实物理距离，不能直接代入 Modbus 原始计数或带符号位移。当前完整换算、APP 输出单位以及手册 ±35 mm 与固件 ±35.7 mm 有效范围的差异，记录在[当前测量语义说明](./laser_measurement_semantics.md)；范围差异在边界数据复核前保持未决。

设：

- `D_e`：该台设备校准得到的 0 kg 空载距离；
- `D_b`：用户站稳、开始律动前记录的距离基线；
- `MA12_D(t)`：律动期间最近最多 12 个有效距离样本的平均值；
- `R_D(t)`：距离版相对形变偏差比例。

```text
R_D(t) = |MA12_D(t) - D_b| / |D_b - D_e|
```

当 J100 距离与弹簧形变在线性工作范围内时，该比例等价于体重版的 `|MA12_W - W_b| / W_b`。因此 J100 沿用相同判定边界和时间规则：

| 条件 | 判断 | 后续处理 |
|---|---|---|
| `R_D <= 0.16` | 正常 | 清除异常/危险计时 |
| `0.16 < R_D <= 0.28` | 可恢复异常 | 允许最多 1500 ms 恢复；仍未恢复则进入危险候选 |
| `R_D > 0.28` | 直接危险 | 危险连续达到 700 ms 后请求停波 |
| 异常超过 1500 ms 后仍未恢复 | 持续异常 | 再持续达到 700 ms 后请求停波 |

实现前须核实 `D_e` 已由逐台设备校准值提供给 ESP32 的距离状态链，并确认 J100 实际输出的距离增减方向、单位和工作范围。胡克定律说明理想弹簧在弹性范围内力与形变成正比；实际整机仍需用 J100 数据确认机械安装和测量链没有显著偏离该线性假设。这项确认用于验证距离换算成立，不用于擅自重设用户已确定的 16%/28% 边界。

当前代码尚未实现上述比例式：`RhythmStateJudge` 的距离分支目前比较绝对距离偏差，J100 默认 `distanceSafeBand=0.01`、`distanceDangerBand=0.03`（内部距离单位）；所以不能把当前运行代码描述为已经复用了 16%/28%。正式实施时，应把该分支改为按 `D_e` 归一化，再复用体重版状态边界与时序；CYD 和其他传感器使用独立参数配置与独立验证结果。

参考： [OpenStax：胡克定律](https://openstax.org/books/university-physics-volume-1/pages/7-1-work)；[NIST：测量系统特性评估](https://www.nist.gov/publications/traceability-considerations-characterization-and-use-measuring-systems-0)。

### 13.26 J100-485 从上电到运行的整体判断流程（用户确认，2026-09-23）

本节把用户认可的流程图正式记录为 **J100-485 的目标业务流程**，供后续实现与真机验证对照。范围只包括 J100-485；CYD3S1612-IIC 和未来的测距传感器要分别设计、采数和验证自己的距离判断参数，不直接套用 J100 参数。BLE 连接和状态订阅继续沿用现有合同，不改变 APP 与 ESP32 的通信职责。

```mermaid
flowchart TD
    P[ESP32 上电] --> INIT[初始化 J100-485 与判断模块<br/>读取已保存的 0 kg 空载距离 D_e]
    INIT --> READ[读取传感器并转换为统一物理距离 D]
    READ --> VALID{本次距离有效吗？}
    VALID -->|否| INVALID[本次样本不进入距离判断<br/>按测量故障状态处理并上报]
    INVALID --> READ
    VALID -->|是| ANCHOR{正式 D_e 有效吗？}
    ANCHOR -->|否| NOANCHOR[继续上报有效真实距离<br/>不把首读数当作 D_e<br/>runtime_ready=0，start_ready=0]
    NOANCHOR --> READ
    ANCHOR -->|是| MODE{律动仪正在运行吗？}

    MODE -->|否| EMPTY[计算 ΔD = |D - D_e|<br/>用进入/退出回差与连续确认判断空载/有人]
    EMPTY -->|空载| NOTREADY[ESP32 上报 start_ready=false<br/>APP 不允许开始或继续]
    EMPTY -->|有人| STABLE[对距离窗口做站稳判断]
    STABLE -->|未站稳| NOTREADY
    STABLE -->|站稳| BASELINE[锁定开始前距离基线 D_b]
    BASELINE --> READY[ESP32 根据有人、站稳和开始门禁结果<br/>上报 start_ready]

    READY --> APP{APP 操作}
    APP -->|开始| START[APP 发 START；APP 开始倒计时]
    APP -->|继续| CONTINUE[APP 发 START；APP 恢复倒计时]
    START --> RUN[ESP32 开始输出，进入运行判断]
    CONTINUE --> RUN
    NOTREADY --> READ

    RUN --> PARALLEL[每个有效采样周期并行更新以下判断]
    PARALLEL --> MAIN[主运行状态：计算 MA12_D 并检查归一化分母]
    PARALLEL --> LEAVE[离台判断：继续比较 D 与 D_e]
    PARALLEL --> FALL[摔倒候选：检查距离变化方向、速度、累计变化和确认条件]

    MAIN --> DENOM{D_b 与 D_e 的距离差达到最小有效跨度 T_span_min_D？}
    DENOM -->|否| BADRATIO[不计算 R_D；记录基准跨度不足<br/>保护处置规则待定]
    DENOM -->|是| RATIO{R_D 落在哪个区间？}
    RATIO -->|R_D ≤ 16%| NORMAL[正常：清除异常与危险计时]
    RATIO -->|16% < R_D ≤ 28%| RECOVER[可恢复异常：最多等待 1500 ms 恢复]
    RECOVER -->|回到安全区| NORMAL
    RECOVER -->|超过恢复时间仍异常| CANDIDATE[进入危险候选，再确认危险持续时间]
    RATIO -->|R_D > 28%| DIRECT[直接进入危险状态，再确认危险持续时间]
    CANDIDATE -->|危险持续达到 700 ms| PROTECT[调用 ESP32 现有保护策略]
    DIRECT -->|危险持续达到 700 ms| PROTECT
    NORMAL --> REPORT[ESP32 上报状态；APP 按订阅内容显示]
    RECOVER --> REPORT
    CANDIDATE --> REPORT
    DIRECT --> REPORT
    PROTECT --> REPORT
    BADRATIO --> REPORT

    LEAVE -->|确认离台| USEROFF[调用 SystemStateMachine::onUserOff<br/>由既有离台保护策略决定后续动作]
    LEAVE -->|仍有人| REPORT
    USEROFF --> REPORT
    FALL -->|确认摔倒| FALLPROTECT[调用现有 FALL_SUSPECTED 保护动作<br/>停波并向 APP 上报]
    FALL -->|未满足| REPORT
    FALLPROTECT --> REPORT

    RUN --> STOPAPP{APP 暂停或停止？}
    STOPAPP -->|暂停| PAUSE[APP 发 STOP；ESP32 停止输出<br/>APP 保留并暂停倒计时]
    STOPAPP -->|停止| STOP[APP 发 STOP；ESP32 停止输出<br/>APP 终止倒计时]
    PAUSE --> MODE
    STOP --> MODE
```

运行期间使用的目标距离比例为：

```text
R_D = |MA12_D - D_b| / |D_b - D_e|
```

其中 `D_e` 是本台设备通过 0 kg 校准点保存的空载距离，`D_b` 是开始律动前的站稳距离基线，`MA12_D` 是最近最多 12 个有效距离样本的平均值；三个距离量必须使用相同单位。计算前除检查 `D_b`、`D_e` 均为有效有限数外，还必须确认跨度 `|D_b - D_e|` 不小于最小有效跨度 `T_span_min_D`。只检查“大于零”不够：分母很小时，传感器量化和重复性误差会被比例放大。`T_span_min_D` 不预设毫米值，须由空载与已知载荷重复数据确定；低于该跨度时不能按比例判为正常，运行时的安全处置要在实施前冻结。16% 与 28% 是用户确认沿用的 J100-485 边界；1500 ms 恢复时间和 700 ms 危险持续确认时间是现有体重版规则中的值，距离版正式接入时应复用并用真机日志确认时序没有改变。[边界与时序配置](../../src/config/RhythmStateConfig.h#L45)

APP 与 ESP32 的操作职责也在此固定：开始和继续都向 ESP32 发开始命令；暂停和停止都向 ESP32 发停止命令。暂停/继续倒计时还是终止倒计时，由 APP 自己区分。运行状态、空载、站稳、离台和危险判断由 ESP32 完成，APP 消费 ESP32 上报的状态；SW APP 继续只使用它现有订阅的状态合同。

离台和摔倒是与 16%/28% 主偏差比例并行的判断支路。确认离台后进入现有 `SystemStateMachine::onUserOff()` 策略，默认停波并上报 `USER_LEFT_PLATFORM`；用户回来并重新满足开始就绪条件后，清除可恢复暂停，由 APP 的“继续”操作重新发送 `START`。确认摔倒后进入现有 `FALL_SUSPECTED` 保护动作，默认停波并上报摔倒原因；阻断状态按现有离台清除条件、冷却和确认时间恢复。两种原因码、停波动作和 APP 事件合同已经存在，不作为新待讨论项。当前 `formalEventCandidate` 只写辅助日志，不能单独触发保护动作；目标实现须将已确认的距离版摔倒判断接入现有 `FALL_SUSPECTED` action owner。主比例危险停波仍按现有入口执行。[离台入口](../../src/core/SystemStateMachine.cpp#L636)；[摔倒候选计算](../../src/modules/laser/RhythmStateJudge.cpp#L270)；[保护动作入口](../../src/modules/laser/LaserModule.cpp#L1743)；[APP 事件合同](../../src/core/ProtocolCodec.h#L456)

#### 本流程是目标，不等于当前固件已经完整实现

| 流程部分 | 当前代码情况 | 后续处理 |
|---|---|---|
| 距离接入、无效样本隔离 | J100-485 已转换为统一距离；无效距离不会进入 `DistanceStateChain` | 按现有测量故障路径验证 |
| 空载锚点 `D_e` | 启动时从当前有效模型的 `referenceDistance` 读取；无模型时当前链会把第一条有效读数临时当空载参考 | 移除临时首读数逻辑。有效距离流继续上报，但无人/站稳/`start_ready` 均不成立；`WAVE:START` 由现有 start gate 拒绝。Demo APP 写入有效 0 kg 定点模型后才启用距离状态链。 |
| 空载与有人 | 当前 J100 使用距离链；默认进入偏移 3 mm、退出偏移 1 mm、连续确认 2 个样本 | 这些只是代码现值，需用 J100 数据验证，不视为已确认参数 |
| 站稳与基线 | 当前距离链看最近 10 个样本的极差和首末漂移，基线取窗口极值中点；开始门禁再消费距离基线 | 与拟定的距离窗口公式/基线统计方式逐项对齐，参数通过采数确定 |
| 律动主判断 | 当前 `RhythmStateJudge` 的 J100 分支比较绝对距离偏差，默认安全/危险带为 0.01/0.03 个内部距离单位（约 1/3 mm）；尚未计算 `R_D` | 按本节公式使用校准 `D_e` 归一化，并复用 16%/28% 边界与计时逻辑 |
| 离台保护 | 距离状态链确认退出后调用 `onUserOff()`，后续由系统离台策略处理 | 真机验证离台、恢复站稳、APP 继续的完整路径 |
| 摔倒保护 | 已有 `FALL_SUSPECTED` 阻断停波、APP 上报和恢复合同；距离版候选当前仅输出辅助日志，尚未接入保护动作 | 将确认后的距离版摔倒判断接入既有保护 action owner；不新增 APP 状态合同 |
| 离台、摔倒后的 APP 状态与恢复 | 已有 `USER_LEFT_PLATFORM` 可恢复暂停、`FALL_SUSPECTED` 阻断停止及相应 APP 语义 | 保持现有合同，真机验证停波、上报、恢复和用户继续顺序；SW APP 不修改 |

距离空载和站稳的待定公式、参数及采数要求按[未律动阶段参数映射及采数方案](./esp32_未律动空载与站稳参数映射及采数方案.md)执行。工程依据方面，胡克定律描述理想弹簧弹性范围内力与形变的正比关系，但这只能说明把受载形变作为归一化量的物理依据；整机是否满足该近似仍需 J100 实测。测量系统的重复性、分辨率、工作条件与不确定性应通过测量数据表征，公开计量资料不替本设备决定毫米阈值。[OpenStax：胡克定律](https://openstax.org/books/university-physics-volume-1/pages/7-1-work)；[NIST：测量系统特性评估](https://www.nist.gov/publications/traceability-considerations-characterization-and-use-measuring-systems-0)。

### 13.27 施工前复核门禁（2026-09-23）

本节记录本轮多来源复核结果。结论：总体方案方向成立，但 J100 距离判断尚未达到可直接改代码并宣称完成验收的状态。公开资料用于约束测量方法和物理假设；设备阈值只能由本机数据确定。NIST 将重复性定义为相同测量条件下连续测量结果的一致程度，并要求用离散程度量化；胡克定律只说明小形变、弹性范围内可能呈比例关系，不能替代对整台律动仪的实测。[NIST 重复性术语](https://www.nist.gov/pml/nist-technical-note-1297/nist-tn-1297-appendix-d1-terminology)；[NIST 测量过程波动](https://www.itl.nist.gov/div898/handbook/mpc/section1/mpc114.htm)；[OpenStax 胡克定律](https://openstax.org/books/college-physics-2e/pages/5-3-elasticity-stress-and-strain)。

| 检查项 | 代码/资料复核结果 | 进入施工前必须完成 |
|---|---|---|
| 真实距离定义 | J100 profile 当前按 `D_mm = 100 - signedRaw × 0.01` 换算；`EVT:STREAM distance_mm` 输出真实毫米。换算细节见[当前测量语义说明](./laser_measurement_semantics.md)。 | 用同步原始寄存器、模块显示和 ESP32 输出留一组可复查实测记录；不能把 signedRaw 直接代入状态比例。 |
| J100 量程 | WTX-J 资料列出 100 mm 中心与 ±35 mm；当前固件接受 ±35.7 mm。两者不一致。 | 留存越界前后原始计数与模块显示，核对厂商书面规格；在此之前不把 ±35.7 mm 称作已确认规格，也不擅自缩改当前门限。 |
| 体重模型坐标兼容 | 当前校准存储有 `mdl_ver=1` 完整模型记录和 CRC/备份；更早的无版本模型或 `zero/factor` 遗留键没有坐标证明。当前启动代码仍会把遗留键算作已保存模型。 | 冻结规则：版本 1 代表当前真实物理距离内部单位（mm/100）；有效版本 1 主/备模型可继续跨重启使用。无版本旧模型与仅有 `zero/factor` 的数据不自动换算、不参与体重/`D_e`、不在启动时删除；隔离保存，等待一次显式新格式校准写入。若 NVS CRC/模型校验失败且无有效版本 1 备份，同样要求重新校准。以后改变距离语义必须升版本。 |
| 空载锚点 `D_e` | 无当前有效模型时，`DistanceStateChain` 会把第一条有效距离临时设成空载参考；这不是 APP 记录的 0 kg 校准点。 | 冻结规则：禁止使用临时首读数。继续上报有效 `distance_mm`；体重标为无效；距离状态链不判有人、站稳、离台或律动；保持 `runtime_ready=0`、`start_ready=0`，现有 `SystemStateMachine::requestStart()` 返回 `NOT_ARMED`。传感器仍健康时，不得把“缺校准锚点”误判为传感器故障或授权降级启动。Demo APP 显式写入当前版本模型后再启用 `D_e`。 |
| 空载与站稳阈值 | 当前 J100 3/1 mm 进入/退出偏移、2 次确认、10 样本站稳窗、极差与首末漂移规则都是代码现值；参数映射文档要求按重复加载和空载恢复数据定值。 | 按采数方案记录重复空载、0/3/5 kg、上台/离台和站稳窗口；不要把当前数值当作已验证阈值。 |
| 16%/28% 主判断 | 用户已确认 J100 目标公式 `R_D = |MA12_D-D_b|/|D_b-D_e|` 和体重版 16%/28% 边界；当前 J100 代码仍用约 1/3 mm 绝对偏差带，尚未实现该比例式。 | 用同一设备的同步体重/距离记录核对线性假设；由重复性与量化数据确定最小有效跨度 `T_span_min_D`，测试分母为零、低于门槛、16%/28% 边界、无效样本和计时器复位。 |
| MA12 的时间含义 | 原始测距轮询约 20 ms，但主状态判断默认每 100 ms 更新（快速站稳构建为 80 ms）；RhythmStateJudge 的 12 点窗口按状态判断调用更新，不是 12 次串口轮询，约覆盖 1.2 s / 0.96 s。 | 自动化测试和日志须按状态判断节奏验证窗口、1500 ms 恢复及 700 ms 危险保持；真机日志应记录实际间隔，避免把 20 ms 当成 MA12 的点间隔。 |
| 离台/摔倒支路 | 离台可复用现有状态机保护入口；摔倒/未离台危险的正式距离阈值尚未从研究阶段验证，当前 `formalEventCandidate` 只作辅助日志，不能据此认定已经接入正式停波。 | 离台按已有合同做真机顺序验收。摔倒支路先完成 J100 正反例采集和离线回放，再把确认结果接到现有 `FALL_SUSPECTED` action owner；不新增 SW APP 状态合同。 |
| 自动化覆盖 | `python3 tools/run_evaluator_unit_tests.py` 通过，但该脚本不覆盖 `DistanceStateChain`、`RhythmStateJudge` 的 J100 新比例公式及完整停波时序。 | 施工时先补对应 host 单元测试，再跑现有 evaluator 测试和统一固件构建。 |
| 可用实测数据 | 仓库现有一份 153 点、约 33 秒的运动采样，包含计算距离/体重/比例；缺少原始寄存器、正式 `D_e`、后端标识和完整空载/离台标签。 | 可用于探索性回放，不能单独证明 J100 16%/28% 换算、空载门槛或摔倒检测可靠。按采数方案补齐有标签的 J100 数据。 |
| 工作区保护 | 当前工作树位于已有功能分支且含多项既存修改与未跟踪文件。 | 开始实现前先保存完整工作区基线并隔离本轮改动；不得用 reset/clean 或切支方式覆盖既有内容。 |

所以施工准备的顺序固定为：① 保存并隔离现有工作区；② 按本节已冻结规则实现旧模型隔离和缺少 `D_e` 时的封闭门禁；③ 补齐 J100 边界及空载/加载/律动带标签的数据；④ 写 J100 公式及 12 点节奏、恢复/停波时序的自动化测试；⑤ 再实现主比例判断和离台接入；⑥ 摔倒支路通过独立回放门禁后才接正式保护动作；⑦ 编译通过后，用现有 BLE 状态合同真机逐项验收。整个过程中 SW APP 的订阅和状态合同不改，Demo APP 负责记录校准/采样证据，不代替 ESP32 判状态。

### 13.28 旧校准模型与缺少 `D_e` 的正式处理规则（2026-09-24）

本节冻结接下来代码施工应实现的行为，不代表当前固件已经如此运行；本节作为本蓝图关于旧模型、缺少 `D_e` 和清模边界的最新规则，优先于前文较早的 E5/E6 审计快照。固件的校准值保存在 ESP32 NVS；Espressif 说明 Preferences/NVS 数据会跨重启和断电保留，因此“启动时读到一个数”不能证明该数仍属于当前测量坐标，应用必须自行校验数据版本和含义。[Arduino-ESP32 Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html)；[ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

代码复核已区分“现状”和“冻结目标”：当前模型存储读取器能识别带 `mdl_ver=1`、提交标记和 CRC 的主/备记录，但 `LaserModule::loadCalibrationModel()` 还把旧 `zero/factor` 键是否存在当成模型已持久化；当前 `DistanceStateChain::update()` 在没有正式锚点时也会把第一条有效读数临时设为空载参考。两条路径都必须修正后，才能满足下表规则。当前 `WAVE:STOP` 只请求停止，不清模型；`CAL:ZERO` 与遗留 `SCALE:ZERO` 命令都会进入 `triggerZero()` 清理路径。归零命令运行中保护尚未实现，属于施工项。

| NVS 内容 | 启动处理 | 是否可作为体重模型 / `D_e` |
|---|---|---|
| 当前模型记录 `mdl_ver=1`，主记录或备份记录 CRC、有限值、空载截距和单调性校验均通过 | 继续加载并保留；`mdl_ver=1` 在本工程中冻结表示 J100 profile 转换后的物理距离内部单位（毫米 / 100）。 | 可以；`referenceDistance` 只能来自 APP 记录的 0 kg 校准点。它应跨断电、重启和普通 OTA 保持不变。 |
| 早期无 `mdl_ver` 的 `mdl_*` 记录，或只有旧 `zero/factor` 键 | 不自动换算、不因键存在就标记“已校准”；先复制到独立隔离记录并读回核对，然后才允许新格式写入覆盖同名活动键。启动本身不删除原始记录。 | 不可以；令模型未就绪、`D_e` 无效。 |
| 当前主记录校验失败但备份记录有效 | 按现有存储恢复路径读取最后一份有效当前模型，并记录恢复来源。 | 可以，前提是备份仍通过当前版本校验。 |
| 主备记录都无效、缺失或版本不符 | 不使用默认占位值或旧键代替；不在启动时清除原始记录。 | 不可以；要求重新校准。 |

旧记录不自动换算，是因为旧数据没有可靠的距离坐标版本；即使物理公式可能是线性变换，也不能从单独的旧零点键证明当时的单位、符号方向、中心距离和拟合参数来源。隔离意味着保留原值供回溯，同时让它退出活动模型；不是把它静默复制到当前 `D_e`。本工程当前模型写入使用主/备记录，而且新旧主模型键名有重叠；所以实现迁移隔离时，须先把无法通过当前版本校验的原始键复制到独立归档记录、逐项读回核对，再允许新模型提交，避免覆盖最后一份旧证据。用户明确执行“参数归零”时，应清除活动模型、主/备记录、兼容键和隔离归档；启动、普通停止或 OTA 不得代替用户执行清除。`WAVE:STOP` 是停波操作，不是清模操作。`CAL:ZERO` 是清模命令；`SCALE:ZERO` 当前也映射到同一清理动作，需在施工中确认兼容入口的处理，但不能把它与 `WAVE:STOP` 混为一谈。

没有有效 `D_e` 时，正式行为固定为：

1. 传感器读数本身有效时，ESP32 仍继续输出真实 `distance_mm`；不能把缺模型误报为测距传感器故障。
2. 不将第一条、当前条或上电时的距离自动设成 `D_e`。距离状态链不得给出“有人、站稳、离台、律动”正式判断；体重字段为无效，不能伪造 `0 kg`。
3. 保持 `runtime_ready=0`、`baseline_ready=0`、`start_ready=0`。现有 `SystemStateMachine::requestStart()` 应返回 `NOT_ARMED`，开始和继续都不能启动波形。
4. 只有 Demo APP 从当前真实距离记录 0 kg 点，并成功提交当前模型后，固件才启用模型参考距离作为 `D_e`；随后仍需通过有人、站稳和开始门禁。
5. 这与“激光传感器缺失或确认故障”的既有 PLUS 降级路径分开处理。传感器健康但尚未校准，不得伪装成无传感器并触发降级启动。
6. `CAL:ZERO`（以及仍保留清模语义的兼容命令）不得在 `RUNNING` 状态下删除活动锚点；运行中收到清除请求时，保持模型不变并沿用现有错误响应路径拒绝本次清除，不新增 SW APP 状态合同。用户先正常停止后才能执行明确的参数归零。当前命令派发没有此运行态保护，施工时必须补上。

实施顺序：先为 NVS 模型装载补“当前格式 / 旧格式 / 无效格式”判定与隔离，不改变 BLE 字段；再让无正式 `D_e` 的距离链保持未就绪，并验证现有 `NOT_ARMED` 启动门；补上运行中清模保护；最后验证“旧参数不被静默改写、有效模型重启不变、普通停止不清模、显式归零确实清除、正常重新校准后才恢复就绪”。这些是静态安全与持久化边界，可先于 J100 采数实现；采数缺口仍阻塞距离阈值定值、16%/28% 映射验证和摔倒阈值验收。

项目内资料：《WTX-J系列说明书 V3.0模拟量切换版本》；《JD系列485通讯操作流程 V1.0》。Modbus 官方说明将功能码 04 定义为读取输入寄存器；这支持通信方式，不定义传感器的物理单位或阈值。[Modbus 官方功能码说明](https://www.modbus.org/introduction-to-modbus)。

### 13.29 传感器身份识别延期与 J100-485 下一步（2026-10-11）

本节记录当前施工取舍。传感器的品牌和具体型号自动识别暂不施工，列为后续事项；本轮只推进已确定的 J100-485 主线。

#### 延期事项：读取传感器品牌与具体型号

- 暂不新增“品牌/型号查询”命令、身份字段或自动识别分支。
- UART、RS-485、IIC 只能说明接口或通信链路，不能单独证明品牌和具体型号。以后只有在选定新传感器、取得其正式手册并确认存在可靠身份查询命令/寄存器后，才重新设计。
- 未来身份信息应分成两层：固件长期依赖的稳定 `sensor_profile_id`，以及仅用于显示和诊断的品牌/型号文本；显示文本不能冒充稳定协议枚举。
- J100-485 当前继续按固定 J100 profile 工作；CYD-IIC 继续保留独立 profile，不因本延期事项改变现有代码路径。

#### J100-485 当前状态

1. 传感器原始值到真实 `distance_mm` 的转换和统一距离接口已有代码；BLE、OTA、SW APP 合同保持冻结。
2. 距离状态链已有空载、有人、站稳和开始门禁入口，但无有效 `D_e` 时仍存在“首个有效读数临时作空载点”的实现缺口。
3. J100 律动主判断的目标公式已经冻结为 `R_D = |MA12_D - D_b| / |D_b - D_e|`，沿用 16%/28%、1500 ms/700 ms；当前代码仍是绝对距离带，不能把它当作目标公式已经落地。
4. 距离版摔倒候选目前只产生辅助证据，尚未接入正式 `FALL_SUSPECTED` 保护动作。

#### 接下来按此顺序施工

1. **先保护工作区**：保存当前基线和本轮证据，所有改动保持在 J100 主题范围内，不使用 `reset/clean` 覆盖已有工作。
2. **先修 J100 安全边界**：隔离无版本或旧坐标模型；禁止无正式 `D_e` 时使用首读数；保留有效 `distance_mm` 上报，但保持体重无效、`runtime_ready/baseline_ready/start_ready` 为未就绪；补上运行中 `CAL:ZERO` 不得清模的门禁。
3. **补 focused tests**：覆盖旧模型隔离、主/备模型跨重启保持、显式归零、无 `D_e` 的 `NOT_ARMED`、运行中清模拒绝、无效距离隔离和 BLE 合同不变。
4. **具备采集条件后再定参数**：采集 J100 原始寄存器、模块显示、ESP32 `distance_mm`、人工确认的 `D_e`/`D_b`，并标记空载、3/5 kg、站稳、正常律动、离台、摔倒正反例；没有这组数据不冻结毫米阈值和最小有效跨度。
5. **数据通过复核后**：把 `R_D`、MA12 窗口、16%/28% 边界和 1500/700 ms 时序接入 J100 主判断，先做离线回放和单元测试，再接入正式离台/摔倒动作。
6. **最后做真机验收**：只验证 J100-485 的上电、连接、空载、站稳、开始/继续、暂停/停止、离台、摔倒和恢复顺序；SW APP 不改，Demo APP 只记录距离、校准和日志证据。

本节新增内容只改变施工顺序和待办记录，不改变当前 BLE 字段、传感器通信命令或 APP 行为合同。Espressif 文档确认 NVS/Preferences 会跨重启和断电保留，因此模型版本、坐标语义和显式清模边界必须在固件中明确校验，不能靠上电时是否读到数值来判断模型有效。[Arduino-ESP32 Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html)

### 13.30 Demo APP 优先施工入口（2026-10-11）

Demo APP 先行施工的页面、数据模型、协议门禁、代码清单和验收顺序，统一见[Demo APP 距离优先与独立空载距离施工蓝图](./demo_app_distance_first_施工蓝图.md)。在该蓝图完成前，不把当前已有的“0 kg 校准点携带 `referenceDistance`”实现宣称为独立空载距离功能。
