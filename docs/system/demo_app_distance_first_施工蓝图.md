# Demo APP 距离优先与独立空载距离施工蓝图

版本：D1（首轮施工实现稿）
日期：2026-10-11
范围：`tools/android_demo/app-demo`、`tools/android_demo/sonicwave-protocol`，以及为完成独立空载距离所需的最小 ESP32 协议配合
不包含：CYD3S1612-IIC 判断算法、传感器品牌/型号自动识别、SW APP 页面改造

## 1. 施工目标

Demo APP 以 ESP32 上报的真实 `distance_mm` 为测量主数据：

- 型号页单独管理空载距离 `D_e`；
- 体重校准页只管理体重模型 `y = ax² + bx + c`；
- 参数归零只清除体重模型 `a/b/c`，不清除 `D_e`；
- 采样页和运行页的曲线、图像、导出文件以距离毫米为主；
- 体重可以显示和保存，但没有体重模型时标记为不可用，不影响距离状态显示；
- 开始就绪、空载、站稳、离台、摔倒和律动状态只显示 ESP32 的正式状态，Demo APP 不自行推断；
- SW APP 的正式状态订阅、控制命令、OTA 和状态字段保持不变。

## 2. 当前代码事实与缺口

| 项目 | 当前状态 | 结论 |
|---|---|---|
| 主页面结构 | `MainScreen` 已有 DEVICE、CALIBRATION、SAMPLING、RUN、LOGS 五个页签 | 保留结构，按页修订 |
| BLE 连接 | 已有 `CAP?`、通知、`STREAM:SET`、`SNAPSHOT?` 初始化链 | 保持合同，不按 APP 名称猜测推流 |
| 实时距离 | 已有 `distance_mm` 解析和显示 | 作为所有测量页面的主值 |
| 校准点/采样持久化 | 已有 `DemoSessionPersistence` | 已补充 schema/version；设备回读是唯一确认源，本地缓存不直接绑定为设备事实 |
| 拟合 | APP 本地保留一次、二次拟合和单调性检查 | 改为使用独立 `D_e` 坐标原点 |
| 型号页 | 已有型号卡片、保护开关卡片 | 增加空载距离卡片 |
| 空载距离 | 已拆出独立 `EmptyDistanceState`，由设备回读确认 | 继续做 J100 真机回读验收 |
| 参数归零 | Demo 使用 `CAL:CLEAR_WEIGHT_MODEL`；旧 `CAL:ZERO` 保留兼容 | 已与 `D_e` 分离 |
| 无 `D_e` 行为 | 固件不再把首个有效读数当正式空载点 | 仍需真机验证未设置时的状态门禁 |
| 距离/体重有效性 | 当前协议历史上存在整体 `valid` 语义 | 需要保留兼容字段并使用 `distance_valid/weight_valid` 分离字段 |

因此，本轮已完成代码层的 APP/协议/固件配合；“独立空载距离已经完成”仍要等 Android Studio Run + J100-485 真机回读和重启证据。

## 3. 固定数据边界

### 3.1 空载距离状态

APP 内部新增独立状态 `EmptyDistanceState`：

```text
status: NOT_SET | PENDING | SET | INVALID | READBACK_MISMATCH
distance_mm: Float?
tolerance_mm: Float?
backend: LASER_485 | CYD3S1612_IIC | UNKNOWN
source: DEVICE_READBACK | USER_SET | LOCAL_ONLY
updated_at: Long?
```

规则：

- `SET` 只能来自 ESP32 成功回读，不能只因为按钮点击就显示已设置；
- `LOCAL_ONLY` 只能作为待提交草稿，不能驱动开始就绪显示；
- 切换传感器后，旧 `D_e` 不自动复用，必须显示未设置或要求重新确认；
- 空载距离为空时，APP 仍显示有效实时距离，但不显示“已就绪”推断。

### 3.2 体重模型状态

APP 内部新增独立状态 `WeightModelState`：

```text
status: NOT_SET | PENDING | SET | INVALID | READBACK_MISMATCH
type: LINEAR | QUADRATIC
a: Float?
b: Float?
c: Float?
updated_at: Long?
```

兼容当前固件时，可暂时保留 `referenceDistance` 作为协议过渡字段，但 UI 和业务语义不得再把它当作体重模型的自由截距。最终模型坐标使用：

```text
u = distance_mm - D_e
y = a*u² + b*u + c
```

没有有效 `a/b/c` 时：

- 不上传或显示伪造体重；
- 不阻断实时距离、采样距离和 ESP32 状态显示；
- 采样导出中保留 `weightValid=false` 和原因码。

### 3.3 实时测量样本

统一为：

```text
TelemetrySample {
    timestamp_ms
    distance_mm
    distance_valid
    distance_reason
    weight_kg?
    weight_valid
    weight_reason
    stable_weight_kg?
    esp_state
    baseline_ready
    start_ready
    backend
}
```

APP 不把 `weight_valid=false` 改写成 `distance_valid=false`，也不把缺少体重模型改写成传感器故障。

## 4. 页面施工方案

### 4.1 型号页

对应当前 `DeviceProfileSection.kt`，顺序固定为：

1. 设置型号卡片；
2. 空载距离卡片；
3. 保护开关卡片。

设置型号卡片：

- Base、Plus、Pro、Ultra 同一行显示；
- 继续显示 ESP32 回读的当前型号；
- 不由 APP 根据传感器名称猜测型号或后端。

空载距离卡片：

- 显示“空载距离：112.56 mm”或“未设置”；
- 下方持续显示“实时距离：xxx.xx mm”；
- 提供“设置”和“清除”两个动作；
- 设置前显示当前实时距离，用户确认后发送命令；
- 设置成功必须收到设备 ACK 并完成 GET 回读；
- 清除成功后显示“未设置”，不能继续显示旧值；
- 设备未连接、实时距离无效、运行中时禁用设置/清除；
- 不因本地点击而改变 `start_ready`，只显示 ESP32 回读状态。

保护开关卡片：

- 保持现有摔倒保护、离台保护开关；
- 显示 ACK、回读和未知状态；
- 不把保护开关状态与空载距离或体重模型混在一起。

### 4.2 体重校准页

对应当前 `CalibrationToolsSection.kt` 和 `CalibrationSessionStore.kt`。

页面顶部新增模型摘要：

- 适用设备：`J100-485`；
- 模型状态：未校准 / 已校准 / 回读不一致 / 无效；
- 模型类型：一次 / 二次；
- `a`、`b`、`c` 的具体数值；
- 最后更新时间；
- 不显示“参数为空但体重为 0 kg”这类伪状态。

页面语义：

- 页面名称改为“体重校准”；
- 明确说明当前只适用于 J100-485；
- “参数归零”只清除 `a/b/c`；
- 参数归零不清除型号页的 `D_e`；
- 记录校准点保存参考重量、实时距离和时间戳；
- 0 kg 点可以作为体重拟合样本，但不再承担设置 `D_e` 的职责；
- 线性和二次拟合都以外部已设定的 `D_e` 为横坐标原点；
- 选择模型后必须检查样本数、单调性、距离范围、误差和 `c` 约束；
- 写入后必须执行 GET 回读，只有完全一致才显示“已部署”。

### 4.3 采样页

对应当前 `MotionSamplingSection.kt`、`MotionSamplingSessionStore.kt` 和 `MotionSamplingExporter.kt`。

- 采样主曲线使用 `distance_mm`；
- 横轴使用设备时间或相对时间；
- 每一行保存距离有效性、原因码、后端、ESP32 状态和可选体重；
- 体重模型缺失时仍允许距离采样；
- 标签保留正常律动、离台、危险/摔倒等研究标签；
- 采样数据持久化，重启 APP 后可恢复未导出会话；
- 导出文件明确标记 `distance_mm` 为主测量列，`weight_kg` 为可选参考列；
- 不允许 APP 根据曲线自行生成正式安全状态。

### 4.4 运行页

对应 `TelemetryChartSection.kt`、运行面板和波形控制条。

- 遥测曲线、移动平均、变化量和图像使用距离；
- 显示体重时只作为参考信息，不能覆盖距离主值；
- 开始按钮颜色和可用性只投影 ESP32 的 `start_ready` / `SNAPSHOT` / 正式状态；
- 开始与继续都发送 `START`；暂停与停止都发送 `STOP`；
- APP 只负责暂停/继续倒计时的本地表现，不重算设备状态；
- 离台、摔倒、保护停波和恢复显示 ESP32 正式事件；
- 无空载距离或无实时距离时，显示原因，不自行把按钮判绿。

### 4.5 日志页

对应 `RawConsoleSection.kt`、`RawConsoleStore.kt` 和测试会话记录。

保留三层信息：

1. 原始 BLE TX/RX 分片；
2. 完整协议行；
3. 结构化消费日志。

关键日志至少包括：

- `CAP?`、`STREAM:SET`、`ACK:STREAM`、`SNAPSHOT?`；
- 空载距离设置/清除请求、ACK、回读和失败原因；
- 体重模型归零、写入、回读和一致性结果；
- 采样开始/停止、导出和恢复；
- ESP32 状态变化、保护原因和来源。

高频 `EVT:STREAM` 默认节流显示，导出时保留完整记录，避免日志页面反过来影响 BLE。

## 5. 协议施工门禁

独立 `D_e` 不能由 APP 本地模拟。正式实现前必须由固件和 Demo APP 共同冻结以下合同（命令名为提案，施工前须以协议评审结果为准）：

```text
EMPTY:GET
EMPTY:SET distance_mm=<finite>
EMPTY:CLEAR
ACK:EMPTY ...
NACK:EMPTY ...
```

体重模型建议使用独立清除语义：

```text
CAL:GET_MODEL
CAL:SET_MODEL ...
CAL:CLEAR_WEIGHT_MODEL
```

在合同冻结前：

- 不把现有 `CAL:ZERO` 静默改成新含义；
- 不让 APP 把本地缓存当作 ESP32 已设置；
- 不修改 SW APP 已正式依赖的字段和状态；
- 保留旧协议解析和兼容输出。

固件必须同时完成：

- 无 `D_e` 时不使用首个读数；
- `D_e` 跨重启、普通 OTA 保持；
- 运行中禁止清除 `D_e` 或模型；
- `distance_valid` 与 `weight_valid` 可分离表达；
- 设置/清除命令支持 ACK、NACK、GET 回读和版本校验。

## 6. 代码修改清单

### Demo APP

- `app-demo/.../UiModels.kt`：新增独立空载距离、体重模型和测量样本状态；
- `DemoViewModel.kt`：连接初始化、空载设置/清除、模型归零、回读和门禁；
- `DemoSessionPersistence.kt`：增加 schema/version，并在重新连接后要求设备回读确认；本地缓存只作草稿/历史展示，不冒充当前设备事实；
- `CalibrationSessionStore.kt`：把 `D_e` 从拟合模型状态中拆出；
- `DeviceProfileSection.kt`：加入空载距离卡片；
- `CalibrationToolsSection.kt`：改为体重校准页和顶部模型摘要；
- `MotionSamplingSection.kt` / `MotionSamplingSessionStore.kt`：距离主列和持久化；
- `TelemetryChartSection.kt`：距离主曲线和单位文案；
- `SystemStatusSection.kt` / `WaveControlBottomBar.kt`：只投影 ESP32 正式状态；
- `RawConsoleSection.kt` / `RawConsoleStore.kt`：补结构化消费日志；
- `MainScreen.kt`：保持五页结构，只调整页面职责和顺序；
- `values-zh-rCN/strings.xml`：统一“空载距离、体重校准、未设置、回读失败”等文案。

### 协议模块

- `sonicwave-protocol/ProtocolCodec.kt`：增加新命令和 ACK/NACK 解析；
- `Model.kt`：增加空载距离与体重模型独立类型；
- `CalibrationComparison.kt`：以外部 `D_e` 做拟合坐标平移；
- golden frames：覆盖缺失、无效、超范围、回读不一致和旧字段兼容。

### 固件配合

固件只做协议和持久化配合，不在本轮改 CYD 判断算法：

- 独立存储和回读 `D_e`；
- 体重模型清除与 `D_e` 清除分离；
- 缺 `D_e` 时禁止正式状态链就绪；
- 保持 BLE、OTA、SW APP 正式合同兼容。

## 7. 分阶段施工顺序

### 阶段 A：冻结与数据模型

- 以本文件和实时流合同为施工入口；
- 先补 UI 状态、领域模型和本地持久化测试；
- 不改变 BLE 字段，不碰 CYD 算法；
- 先让 APP 能正确表示“距离有效、体重未校准、空载未设置”。

### 阶段 B：型号页和体重校准页

- 加入空载距离卡片和回读状态；
- 体重校准页只处理 `a/b/c`；
- 解决参数归零、模型回读和页面状态机；
- 已落地独立空载按钮、ACK/GET 回读状态；仍禁止仅凭本地点击显示已设置。

### 阶段 C：采样、运行和日志

- 距离优先曲线、图像、表格和导出；
- 体重作为可选参考；
- 完善结构化日志和会话恢复；
- 保持开始按钮由 ESP32 状态驱动。

### 阶段 D：协议桥接与固件回读

- 冻结并实现独立 `D_e` 命令；
- 完成主/备存储、回读、版本和错误码；
- 联调 Demo APP 与 J100-485；
- SW APP 只做回归，不改页面和订阅合同。

### 阶段 E：验证与交付

自动验证：

```text
git diff --check
./gradlew :sonicwave-protocol:test
./gradlew :app-demo:testDebugUnitTest
./gradlew :app-demo:assembleDebug
```

真机验证顺序：

1. Android Studio Run 安装 `app-demo`，不清除 APP 数据；
2. 烧录统一 ESP32 固件并重启；
3. 连接 J100-485，确认 `CAP?`、`STREAM:SET`、`ACK:STREAM`、`SNAPSHOT`；
4. 设置空载距离，确认 ACK 和 GET 回读；
5. 清除空载距离，确认状态回到未设置；
6. 归零体重模型，确认 `D_e` 仍保留；
7. 记录体重点、拟合、写入并回读 `a/b/c`；
8. 采样页、运行页、日志页逐项检查距离主数据；
9. 重启 APP、重启 ESP32，再确认空载距离和模型状态；
10. 最后验证开始/暂停/继续/停止和 ESP32 状态投影。

真机结论必须同时有 Android 日志、ESP32 串口日志和界面结果；只有体感或页面显示，不能写成完整通过。

## 8. 明确暂不处理

- CYD3S1612-IIC 的空载/站稳/律动阈值；
- 传感器品牌和具体型号自动识别；
- SW APP 页面或其正式状态订阅；
- J100-485 `R_D`、16%/28% 和摔倒阈值的固件实现；
- 删除旧协议兼容字段。

## 8.1 D1 已实施内容与剩余门禁（2026-10-11）

已实施：

- Demo APP 型号页增加独立空载距离卡片，显示设备确认值和实时距离；设置/清除均走 ACK 后 GET 回读。
- 体重校准页增加模型状态和 `a/b/c` 摘要；参数归零只清体重模型，保留 `D_e`。
- 采样页、运行页和测试会话以距离为主；没有体重模型时仍保存距离，体重列为空并带 `weight_valid=false`。
- Android 本地持久化保存空载距离、体重模型、校准点和采样会话；重新连接时本地缓存必须重新得到设备确认。
- 协议模块增加 `EMPTY:*` 和 `CAL:CLEAR_WEIGHT_MODEL`，固件增加独立 NVS 存储、回读和清除路径。
- 固件没有 `D_e` 时不使用首个读数作为正式空载参考；SW APP 的正式字段、状态订阅和 OTA 未修改。

尚待验证：

- J100-485 真机：设置、GET 回读、清除、重启/OTA 保留、运行中拒绝清除。
- 无体重模型时 `EVT:STREAM distance_valid=1,weight_valid=0` 的 Demo 显示、采样、运行曲线和导出。
- 旧固件不支持新命令时，Demo 的明确降级提示；不能把不支持伪装成成功。
- CYD3S1612-IIC 的阈值、容差和状态算法仍未施工。

施工前置风险：当前本地校准点、拟合结果和采样数据仍按单一工作区保存，没有按 BLE identity（地址/稳定设备 ID）分桶。连接另一台 J100 前，不能把本地旧模型当作当前设备模型；本轮先要求用户以设备回读为准，后续再单独施工多设备身份隔离。

## 9. 施工完成判定

只有同时满足以下条件，才称为 Demo APP 本轮完成：

- 空载距离与体重模型在 UI、协议、固件存储和回读上完全分离；
- 无体重模型时距离仍能显示、采样和绘图；
- `参数归零` 不改变 `D_e`；
- 设置/清除空载距离不改变 `a/b/c`；
- APP 不自行判断 `start_ready`、离台或摔倒；
- 采样、运行、日志和持久化均能恢复且字段含义一致；
- Android 单元测试、协议测试、Debug 构建和 J100 真机证据全部齐全。

## 10. 依据

- [ESP32 统一固件施工蓝图](./esp32_iic_preserve_all_function施工蓝图.md)
- [实时流订阅合同](./esp32_realtime_stream_subscription_contract.md)
- [校准录点 ownership 审计](./calibration_recording_ownership.md)
- [Demo APP 校准使用说明](./demo_app_calibration_guide_zh.md)
- [Demo APP 校准模型比较说明](./demo_app_calibration_model_comparison_zh.md)
- [Android Compose 状态提升](https://developer.android.com/develop/ui/compose/state-hoisting)
- [Android Preferences DataStore](https://developer.android.com/codelabs/android-preferences-datastore)
- [Android 12 Bluetooth 权限](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions)
