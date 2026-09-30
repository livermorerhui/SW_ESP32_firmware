# ESP32 连接与断开提示音施工蓝图

状态：当前施工蓝图（尚未进入代码施工）
文档类型：D3 全面详细 / 高风险冻结
适用范围：`SW_ESP3_Firmware`、Android/iOS 设备连接触发链、PCM5102A 音频输出链
日期：2026-09-30

## 1. 先给结论

这项功能可以放在 ESP32 现有的 `WaveModule → I2S → PCM5102A` 输出链中实现，但必须新增一个独立的 **CUE（提示音）模式**。CUE 只复用 I2S 硬件和唯一音频写入任务，不复用 WAVE 的频率、强度、运行状态或停止语义。

本轮只形成施工蓝图，**不修改 APP、不修改固件、不改 BLE 协议、不烧录**。当前允许继续做的是合同、参数表、测试用例和实现前检查；连接/断开两种提示音参数已经确定，业务代码施工要等 CUE 协议、生命周期消息身份和断连停波门禁冻结后再开始。

当前架构判断：

| 门禁 | 结论 | 依据 |
|---|---|---|
| 通用工程门禁 | `general-partial` | 现有固件已经有单一 I2S 写入 owner 和状态机停波路径，但尚无 CUE 状态、协议入口和一次性去重合同 |
| SW 项目门禁 | `sw-partial / prepared` | 连接真相、WAVE 真相和提示音参数已经明确；协议字段、停波完成交接和跨端能力兼容仍待冻结 |
| 当前可施工内容 | 文档、纯策略测试设计、合同评审 | 不改变运行行为 |
| 当前禁止内容 | 直接改 `WaveModule`、`ProtocolCodec`、Android/iOS 连接 owner 或 BLE 回调 | 防止把提示音混入 WAVE 或在协议未定时产生双 owner |

## 2. 已确认的产品行为

以下内容是本轮施工的固定输入，不再由代码自行推断：

1. APP 与 ESP32 完成协议连接后，在没有律动时，由 **ESP32 通过 PCM5102A 和律动仪喇叭**播放连接成功提示音。
2. APP 与 ESP32 断开时，在没有律动时，由 ESP32 播放断开提示音。
3. 正在律动时发生断开，ESP32 必须先停止 WAVE；确认 WAVE 输出已经停止后，再播放断开提示音。
4. 连接提示音和断开提示音独立于 WAVE。WAVE 仍然是原来的低频正弦律动输出。
5. 提示音的 PCM5102A 输出电压目标与 WAVE 强度 120 使用同一数字满幅口径：名义约 `0.8 Vpk`，约 `0.566 Vrms`、`1.6 Vpp`。这里的 0.8 V 是峰值，不是 RMS，也不是硬件绝对保证值。
6. 手机不直接播放提示音；手机只负责在连接真相成立时发起连接提示音请求。断开提示音由 ESP32 的真实 BLE 断开事件负责触发，因为链路断开后 APP 不能再可靠地发送命令。

## 3. 目标链路、相关链路和真相源

### 3.1 目标链路

```text
APP transport connected
        ↓
APP 完成 CAP/SNAPSHOT 等协议确认，当前连接 epoch 的 protocolReady=true
        ↓
APP 由当前连接 owner 发送一次 AUDIO:CUE id=CONNECT（命令字段仍待协议冻结）
        ↓
ESP32 ProtocolCodec → CommandBus → HubHandler
        ↓
WaveModule 的 CUE 请求入口
        ↓
唯一 I2S_Audio 任务生成短提示音
        ↓
PCM5102A → TPA3255 → 律动仪喇叭
```

断开链路为：

```text
BLE 真实断开
        ↓
BleTransport 控制任务通知 HubHandler / SystemStateMachine
        ↓
保留现有安全停波路径
        ↓
WaveModule.isOutputActive() 变为 false，且 audioTask 发布对应 stop sequence 完成事件
        ↓
WaveModule 请求 DISCONNECT CUE
        ↓
同一个 I2S_Audio 任务播放断开提示音
```

### 3.2 相关链路

| 链路 | 现状 | 本功能的边界 |
|---|---|---|
| APP 连接状态 | `transportConnected` 和 `protocolReady` 分开维护 | 只有当前 transport epoch 的 `protocolReady=false → true` 边沿才允许 CONNECT CUE |
| BLE 生命周期 | `BleTransport` 收到连接/断开回调，再交给控制任务 | 回调只投递事件，不能直接写 I2S |
| WAVE 运行状态 | `SystemStateMachine` 是 WAVE 开始/停止的 owner | 不因为提示音改变 WAVE 的业务状态 |
| 音频输出 | `WaveModule::audioTask` 是现有 I2S 写入 owner | WAVE 和 CUE 必须在同一任务内串行输出 |
| 协议 | `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*` 已有正式语义 | 不修改旧线格式，只增加独立的可选 CUE 命令 |
| 输出电压 | `MAX_AMPLITUDE=8826` 对应 PCM5102A 名义约 0.8 Vpk | 实机示波器测量才是硬件验收证据 |

### 3.3 真相源与证据源

| 项目 | 真相源 | 证据源 |
|---|---|---|
| 是否连接成功 | APP 当前 epoch 的正式 ready 状态 | Android/iOS gateway 日志、ESP32 收到的 CUE 命令 |
| 是否真的断开 | ESP32 BLE 断开事件和 transport session | ESP32 生命周期日志、APP 连接状态日志 |
| WAVE 是否停止 | `WaveModule::isOutputActive()` 的运行事实 | `EVT:STOP` / 现有状态日志、I2S 输出测量 |
| CUE 是否播放 | WaveModule 内部 CUE 状态 | 中文运行日志、CUE 完成事件、必要时 debug 计数 |
| 输出电压 | PCM5102A `OUTL/OUTR` 对 `AGND` 的实测 | 示波器记录 `Vpk/Vrms/Vpp` |

## 4. 不应改动的层

以下内容在本功能中冻结：

- `WAVE:SET` 的频率范围 `0..50 Hz`、强度范围 `0..120`、现有参数解析和 ACK/NACK 语义。
- `WAVE:START`、`WAVE:STOP` 和 `SystemStateMachine` 的安全停波规则。
- `EVT:STATE`、`EVT:STOP`、`EVT:SAFETY`、`EVT:FAULT`、`EVT:PARAM`、`EVT:STREAM`、`EVT:BASELINE`、`SNAPSHOT` 等现有 APP 通信格式。
- `WaveModule::isOutputActive()` 的含义：它继续表示 **WAVE 是否正在输出**，不能改成“WAVE 或 CUE 任一有声音”。如需判断提示音，增加独立的 CUE 状态或内部方法。
- BLE 回调线程和协议控制任务的非阻塞要求。任何回调不得等待提示音播放，不得直接调用 `i2s_write`。
- APP 页面 UI、律动参数 Projection 和强度百分比逻辑。本功能不是页面控件，也不应把提示音混入强度百分比链路。

## 5. 建议的 CUE 合同

### 5.1 逻辑接口

固件内部建议使用窄接口，而不是把提示音参数散落在 BLE、状态机和页面中：

```text
requestCue(cueId, transportSessionId, triggerSource) -> accepted / duplicate / rejected / queued
```

- `cueId`：`CONNECT` 或 `DISCONNECT`。
- `transportSessionId`：使用已有 transport session/epoch 身份；不得用本地时间戳临时拼接身份。
- `triggerSource`：`APP_PROTOCOL_READY`、`BLE_DISCONNECTED_AFTER_STOP` 等固定来源。
- 返回值只说明请求是否被接受，不代表声音已经播放完；播放完成由内部状态和日志记录。

### 5.2 建议的协议入口

建议新增一条独立、可选的命令：

```text
AUDIO:CUE id=CONNECT
```

建议返回独立 ACK，例如：

```text
ACK:AUDIO_CUE id=CONNECT result=ACCEPTED
```

命令名和 ACK 的字段在代码施工前作为协议合同复核项。旧 APP 不发送这条命令时，WAVE、连接、停止和保护功能必须完全不受影响；旧固件不认识这条命令时，APP 应把它当成可选能力，而不能把未播放提示音判成连接失败。

断开提示音不依赖 APP 发送 `DISCONNECT` 命令，使用 ESP32 真实断开事件触发，避免链路已经断开后命令丢失。

### 5.3 事件身份和去重

每个 transport session 最多产生：

- 一个 `CONNECT` CUE：仅在当前 transport epoch 的 ready 状态从未就绪变为就绪时接受。
- 一个 `DISCONNECT` CUE：仅在该 session 第一次真实断开时接受。

以下情况不得产生提示音：扫描中、仅建立 BLE 链路但 `protocolReady=false`、重复 ACK、重复连接回调、重复断开回调、连接失败、APP 页面重绘、WAVE 步骤切换、强度百分比变化。

当前 `BleTransport` 的 `sessionId` 在新连接时递增、断开时不递增，施工时应把它作为 ESP32 侧断开去重身份。APP 侧使用 Android `transportEpoch` 或 iOS `connectionGeneration`，不能用页面生命周期或本地时间戳代替。

施工前还必须补齐 BLE 控制消息的身份：当前 `ControlMsg` 只有类型和文本，没有 `sessionId/connId`。连接、断开和 RX 命令进入队列后，如果旧消息晚于新连接才被处理，可能把旧断开误用于新连接，或在断开后仍处理排队的 CONNECT CUE。后续控制消息必须携带捕获时的 transport session/connId，控制任务先校验当前 session 和 `deviceConnected`，过期消息直接丢弃。

如果断开事件到达时 CONNECT CUE 还在等待队列中，应丢弃 CONNECT 请求，优先完成停波并播放一次 DISCONNECT CUE。重复断开事件只返回 `duplicate`，不得再次发声。

### 5.4 断连停波合同

产品要求是“律动中断开，必须先停 WAVE，再播放断开音”。因此 CUE 功能不能接受 `WARNING_ONLY` 这种断连后仍让 WAVE 继续运行的配置组合。施工前必须二选一并写入正式配置合同：

- 正式产品构建固定使用当前 `RECOVERABLE_PAUSE` 或 `BLOCKING_FAULT` 停波策略；
- 或由 `SystemStateMachine` 增加独立的“断连提示音前置停波”请求，仍由 FSM 调用唯一的 WAVE stop owner。

CUE 层不能直接调用 `WaveModule::stopSoft()` 越过 FSM，也不能在 WAVE 仍输出时播放断开音。

## 6. WAVE 与 CUE 的音频实现边界

### 6.1 唯一输出 owner

继续由 `WaveModule::audioTask` 独占所有 `i2s_write`。CUE 不新增第二个 I2S task，不在 `BleTransport`、`SystemStateMachine` 或 `HubHandler` 中写 PCM 数据。这样可以避免两个任务同时写 DMA，降低爆音、竞态和 WAVE 被覆盖的风险。

### 6.2 两套互不污染的状态

建议在 WaveModule 内部保留两组状态：

| 状态 | WAVE | CUE |
|---|---|---|
| 频率/相位 | 现有 `target_phase_inc`、WAVE 相位 | 独立 cue 频率和 cue 相位累加器 |
| 强度/幅度 | 现有 `target_intensity`、WAVE ramp | 独立 CUE 峰值和短包络 |
| 运行标记 | `run_requested`、`run_state` | `cue_pending`、`cue_active`、`cue_complete` |
| 对外状态 | `isOutputActive()` | 独立内部状态，不冒充 WAVE 运行 |
| 停止时间 | 现有 500 ms WAVE 软停 | 独立短音效包络，不复用 WAVE 500 ms 软停 |

不能通过 `setFreq(880)` 播放提示音，因为 WAVE 的正式频率上限是 50 Hz，调用它会被夹到 50 Hz。CUE 必须使用自己的频率字段和相位累加器，也不能临时改写 WAVE 的目标参数再恢复。

### 6.3 建议的音频状态机

```text
IDLE_SILENCE
  ├─ WAVE start → WAVE_RAMP_UP → WAVE_RUNNING
  ├─ CONNECT request（仅空闲）→ CUE_PLAYING → IDLE_SILENCE
  └─ DISCONNECT request
       ├─ WAVE_RUNNING → DISCONNECT_STOP_PENDING → WAVE_RAMP_DOWN → CUE_PLAYING → IDLE_SILENCE
       └─ IDLE_SILENCE → CUE_PLAYING → IDLE_SILENCE
```

CUE 播放期间不改变 `SystemStateMachine` 的 WAVE 状态。提示音结束后，I2S 先写入静音帧，再回到既有的空闲处理。

如果 CONNECT CUE 播放期间收到 `WAVE:START`，不能让 WAVE 与 CUE 同时写 DMA。建议按“断连停波 > 已接受的 CUE > WAVE 开始”的顺序排队：先记住合法的 WAVE start 请求，等 CUE 完成后再进入原有 WAVE ramp；如果产品决定拒绝，则返回明确的 `NACK:BUSY_CUE`，不能静默丢弃。

断开停波必须是非阻塞交接：`SystemStateMachine` 继续调用现有 `stopSoft()`，由 `audioTask` 在软停完成、写入静音帧并停止 I2S 后发布 stop sequence/完成事件；控制任务看到完成事件后再发起 DISCONNECT CUE。BLE 回调和控制任务都不能忙等 500 ms，也不能直接写 I2S。

### 6.4 幅度和参数

当前正式数字幅度为：

```text
MAX_AMPLITUDE = 8826
digital_ratio = 8826 / 32767
Vpk(I) ≈ 0.79995 × I / 120 V
Vrms(I) ≈ 0.56565 × I / 120 V
Vpp(I) ≈ 1.59990 × I / 120 V
```

CUE 的峰值目标使用 `MAX_AMPLITUDE=8826`，所以名义峰值与 WAVE 强度 120 相同。由于提示音有渐入、渐出和不同波形占空时间，整段音效的 RMS 会低于峰值；实际模块增益、负载和供电变化必须用示波器确认，不能把公式当成硬件绝对值。

用户已确认先采用下面这两个提示音作为本轮产品参数。它们只属于 CUE，不会进入 WAVE 的频率或强度范围；施工后只验证实际听感、时长和电压，不再重新选择声音方向：

| 音效 | 听感目标 | 推荐参数 |
|---|---|---|
| CONNECT | 明亮、上行、短促 | 660 Hz 约 90 ms → 880 Hz 约 140 ms，音符间隔约 20 ms，5–10 ms 渐入/渐出 |
| DISCONNECT | 低沉、下行、结束感 | 440 Hz 约 100 ms → 330 Hz 约 160 ms，音符间隔约 20 ms，5–10 ms 渐入/渐出 |

这不是报警声，也不是手机系统音效的复制；只是让用户听出“已经连上”和“已经断开”。具体值是基于通用听觉设计原则的工程推荐，不是行业强制标准。Microsoft 的通用界面声参考主要集中在 600 Hz–2 kHz，因此 330 Hz 需要在实际喇叭/振动平台上确认是否够清楚；若太闷，备选为 440→350 Hz 或 500→350 Hz。

## 7. 施工分包和文件 owner

### 包 0：合同和测试夹具（本轮）

- 固化连接真相、断开真相、停波后播放顺序、事件去重和幅度口径。
- 形成 CUE 参数表、协议草案、状态转移表和回滚点。
- 不改业务代码。

### 包 1：WaveModule CUE 调度器（固件）

涉及：`src/modules/wave/WaveModule.h/.cpp`、必要的纯策略文件和 focused tests。

- 增加 CUE 请求、独立相位/包络和播放完成状态。
- 保持一个 `audioTask` 和一个 I2S writer。
- 不改变现有 WAVE 参数、WAVE ramp、`isOutputActive()` 和对外事件。
- CUE 请求必须非阻塞；队列满、正在播放或状态不允许时返回明确结果。

### 包 2：协议入口和 APP 连接触发

涉及：`src/core/ProtocolCodec.h/.cpp`、`HubHandler`、Android/iOS 设备 gateway。

- 增加独立 `AUDIO:CUE` 解析、ACK 和能力兼容处理；建议在能力返回中增加 `cue_supported=1`，或明确使用 best-effort/NACK 兼容策略。
- Android 在当前 `transportEpoch` 的 `protocolReady` 首次变为 true 的 owner 中发一次 CONNECT 请求；`markProtocolReady()`、刷新、重试和页面重建都必须先检查当前连接仍有效且 epoch 未变化，不能让旧 ACK/旧 refresh 把新 epoch 误标为 ready，也不能在 transport connected 或页面重绘时发声。
- iOS 没有同名 `protocolReady` 字段，应以 `connected + hasCurrentConnectionSnapshot + fresh` 的正式事实作为 ready 边沿；不能把 CoreBluetooth connected 当成协议完成。
- Android 需要同步 `sonicwave-protocol` 命令模型/编码、core-device command 映射和测试；iOS 需要同步 `SonicWaveCommand`、`SonicWaveProtocolCodec`、repository send owner 和 Swift tests。
- Android 现有 `SoundPool` 只用于 Home 页点击声且默认关闭，iOS 现有 `MediaPlayer` 只用于功能音乐；两者都不是连接提示音 owner，不能把它们接入本功能。
- 不在页面 `onResume`、按钮点击、连接 UI 重绘或任意重试回调中直接发声。

### 包 3：断开停波后提示音

涉及：`BleTransport`、`SystemStateMachine`、`HubHandler` 与 WaveModule 的窄接口。

- BLE 回调只投递断开事件。
- 复用现有安全停波路径。
- 等 `isOutputActive()` 变为 false，并收到由 `audioTask` 发布的 stop sequence 完成交接事件后再接受 DISCONNECT CUE；控制任务只事件驱动等待，不忙等。
- 断开事件按 transport session 去重，连接音未播放完成时按优先级丢弃连接音。
- 如果生命周期事件入队失败或队列已满，不能猜测连接状态，也不能播放错误提示音；必须保留安全停波降级和中文异常日志。
- 明确 OTA 期间 `AUDIO:CUE` 是拒绝、静默忽略还是允许；不得绕过现有 OTA business block。

### 包 4：验证、中文日志和长期文档

- 添加 parser/状态机/去重/顺序 focused tests。
- 更新 `docs/protocol.md`、`docs/architecture_runtime.md` 和幅度设计文档；只有代码和实测通过后才写入正式合同。
- 形成 `reports/` 真机记录，记录 APP 日志、ESP32 日志、示波器读数和失败情况。

### 包 5：真机和硬件验收

- 使用现有 PlatformIO 入口编译、烧录和串口监视。
- 在 PCM5102A `OUTL/OUTR` 对 `AGND` 测量连接音、断开音和 WAVE 强度 120 的 `Vpk/Vrms/Vpp`。
- 验收通过后再分类 commit；功能 commit 与文档/复盘 commit 分开。

## 8. 验证矩阵

### 8.1 WAVE 回归

| 场景 | 预期 |
|---|---|
| `WAVE:SET` 频率 0、50 Hz | 行为与当前一致 |
| 强度 0、1、120 | 0 仍表示静音，120 的 WAVE 输出不变 |
| WAVE 开始/停止 | 原有 ramp、状态和事件不变 |
| WAVE 参数在 CUE 前后 | 频率、强度、相位目标不被改写 |
| WAVE 步骤切换/百分比变化 | 不触发任何 CUE |

### 8.2 连接/断开时序

| 场景 | 预期 |
|---|---|
| BLE 已连但 `protocolReady=false` | 不播放连接音 |
| 当前 `transportEpoch` 的 `protocolReady` 首次变为 true | 只播放一次连接音 |
| 重复 ready、重发 ACK、页面重建 | 不重复播放 |
| 空闲时真实断开 | 只播放一次断开音 |
| 律动中真实断开 | 先停 WAVE，确认静音/停止，再播放断开音 |
| 连接音尚在等待时断开 | 丢弃连接音，完成停波后只播放断开音 |
| 扫描、广告、连接失败、重连尝试 | 不播放提示音 |
| 断开后重新连接 | 每个新 transport session 各播放一次连接音 |

### 8.3 音频和稳定性

- CUE 不新建第二个 I2S 写入任务，不阻塞 BLE 回调，不触发看门狗。
- I2S 短写、写入错误、队列满时进入静音安全路径，不改变 WAVE 和安全停波行为。
- CUE 播放期间不产生 `EVT:STATE RUNNING`，也不把提示音伪装成 WAVE 输出。
- 示波器测得 CUE 峰值接近 WAVE 强度 120 的名义 `0.8 Vpk`；记录真实误差，不以“公式相等”代替测量。

### 8.4 日志

日常日志保持中文、短句、可读，例如：

```text
[音效] 连接提示音：已接受（本次连接）
[音效] 连接提示音：播放完成
[断开处理] 已停止律动，准备播放断开提示音
[音效] 断开提示音：已开始
[音效] 断开提示音：播放完成
```

重复事件、命令不支持、队列满、I2S 错误才打印异常信息。相位、DMA、每帧采样等开发细节默认关闭，只在显式 debug 开关打开时打印。

## 9. 失败处理和回滚

1. CUE 任何失败都不能阻塞连接、断开、安全停波或 WAVE 控制主链。
2. 连接音命令不被旧固件识别时，APP 保持连接成功状态，提示音作为可选能力缺失；不能把它升级为协议连接失败。
3. 断开提示音播放失败时，设备仍保持已经完成的停波和断开状态。
4. 如果实测幅度超过目标，先降低 CUE 专用数字峰值并记录校准值；不得修改 WAVE 强度 120 的现有合同来掩盖 CUE 问题。
5. 如果 CUE 引入 WAVE 抖动、状态污染或重复触发，先关闭 CUE 入口并恢复静音，再回滚 CUE 包；现有 WAVE 文件、协议和状态机保持可独立运行。
6. 施工前建立干净 checkpoint。后续提交建议分为：`cue scheduler`、`cue protocol/app trigger`、`disconnect sequencing`、`tests/docs`，不把四类变化混成一个提交。

## 10. 示波器验收口径（人话）

“示波器验收口径”不是让用户选择另一种音效，而是施工完成后检查 **ESP32 实际送到 PCM5102A 输出端的电压**。代码中的数字计算只能说明理论值，示波器才能确认硬件实际有没有明显偏差。

| 要检查什么 | 人话说明 |
|---|---|
| 测量位置 | 示波器探头接 PCM5102A 的 `OUTL` 或 `OUTR`，地夹接同一块板的 `AGND`。不接 TPA3255 功放后的喇叭端子。 |
| 主要看什么 | 看 `Vpk`（从中间基线到最高峰）。目标约 `0.8 Vpk`，与 WAVE 强度 120 的名义峰值一致。 |
| 同时记录什么 | `Vpp`（最高峰到最低峰）约 `1.6 Vpp`；纯正弦稳定段的 `Vrms` 约 `0.566 Vrms`。 |
| 提示音怎么测 | 提示音有渐入和渐出，要看中间已经达到稳定音量的几个周期，不能只看开头或结尾。 |
| 为什么不只写“0.8 V” | `0.8 V` 不说明是峰值、峰峰值还是 RMS，三者数值不同。本项目把 `0.8 Vpk` 作为主要口径。 |
| 测量结果怎么用 | 记录连接音、断开音和 WAVE 强度 120 的三个读数，确认它们的峰值目标一致；真实误差写进硬件测试记录。 |

因此，这项验收不是新的产品决策，后续由开发/硬件测试人员按表测量即可。当前施工推荐的声音参数已经明确为上面的连接上行音和断开下行音；剩下需要冻结的是 `AUDIO:CUE` 协议字段和实现前代码复核。

## 11. 资料和项目证据

### SW 项目证据

- [WaveModule.h](../../src/modules/wave/WaveModule.h)：WAVE 频率/强度接口、I2S owner 的声明。
- [WaveModule.cpp](../../src/modules/wave/WaveModule.cpp)：16 kHz/16-bit I2S、唯一 `audioTask`、WAVE ramp 和 `i2s_write` 路径。
- [ProtocolCodec.h](../../src/core/ProtocolCodec.h)：现有 WAVE 命令和 `0..50 Hz`、`0..120` 解析限制。
- [BleTransport.cpp](../../src/transport/ble/BleTransport.cpp)：BLE 连接/断开事件投递路径。
- [SystemStateMachine.cpp](../../src/core/SystemStateMachine.cpp)：WAVE 开始/停止和断开后的安全停波 owner。
- [GlobalConfig.h](../../src/config/GlobalConfig.h)：I2S 端口、采样率、缓冲区和 WAVE 幅度限制。
- [architecture_runtime.md](../architecture_runtime.md)：`APP → BLE → 状态机 → WaveModule → I2S → PCM5102A` 运行链。
- [esp32_plus_normal_delivery_freeze.md](esp32_plus_normal_delivery_freeze.md)：APP 只有 `protocolReady=true` 才算连接完成。
- [audio_output_amplitude_design.md](../audio_output_amplitude_design.md)：`MAX_AMPLITUDE=8826` 和 0.8 Vpk 名义计算。
- Android `SonicWaveSdkDeviceGateway.kt`：APP 侧 capability/snapshot 完成后才发布 `protocolReady`（位于同一工作区 `core-device-sonicwave`）。

### 公开资料（通用规则，不替代 SW 合同）

- [TI PCM5102A 产品页](https://www.ti.com/product/PCM5102A) 与 [数据手册](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf)：说明器件的音频输入能力和典型模拟输出口径；不能替代本机模块的绝对电压测量。
- [Espressif I2S 官方文档](https://docs.espressif.com/projects/esp-idf/en/stable/api-reference/peripherals/i2s.html)：说明 I2S DMA 写入接口和写入边界；不能替代 SW 的单一音频 owner 合同。
- [ESP-ADF I2S stream writer 示例](https://github.com/espressif/esp-adf/blob/release/v2.x/examples/get-started/play_mp3_control/main/play_mp3_control_example.c)：提供成熟的单一 I2S 输出链参考；不要求本项目引入 ADF。
- [Android Earcons 指南](https://developer.android.com/design/ui/ai-glasses/guides/interaction/earcons?hl=en)：支持提示音短、直观、易区分的通用设计原则；不定义本项目的频率和协议。
- [freedesktop Sound Naming Specification](https://whot.pages.freedesktop.org/xdg-specs/sound-naming-spec/latest/ar01s04.html)：将设备加入和移除视为不同声音事件；这里只作为事件分类参考。
- [KDE Ocean Sound Theme](https://github.com/KDE/ocean-sound-theme/blob/master/README.md)：提供设备加入/移除音效分离和包络处理的开源参考；不替代本项目产品听感验收。
- [Human factors study](https://pubmed.ncbi.nlm.nih.gov/16884050/)：支持稳定、清晰的听觉语义有助于学习和识别；不能直接决定本项目参数。
- [Microsoft Mixed Reality Spatial Sound Design](https://learn.microsoft.com/en-us/windows/mixed-reality/design/spatial-sound-design)：连接/断开使用短促音调、上行正向和下行结束的成熟产品参考。
- [Microsoft Windows UX Sound](https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-sound)：短于 1 秒、柔和包络和常用界面声频段参考；不规定本项目四个频率。
- [Apple CoreBluetooth didConnect](https://developer.apple.com/documentation/corebluetooth/cbcentralmanagerdelegate/centralmanager%28_%3Adidconnect%3A%29) 与 [didDisconnectPeripheral](https://developer.apple.com/documentation/corebluetooth/cbcentralmanagerdelegate/centralmanager%28_%3Adiddisconnectperipheral%3Aerror%3A%29)：BLE 已连接和已断开是 transport 阶段事实，不能替代协议 ready，也不能在断开后再写命令。
- [Android BLE 数据传输](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)：`STATE_CONNECTED` 后仍需服务发现/通知配置，支持连接音绑定协议确认边沿。

## 12. 本蓝图的完成标准

本蓝图完成后，任何实施者都应能回答：

- 什么时候播放连接音，什么时候绝不播放；
- 断开时为什么必须先停 WAVE，以及由谁确认已经停完；
- 为什么 CUE 不会改变 WAVE 的频率、强度、状态和 APP 协议；
- 为什么所有 I2S 写入仍由一个任务完成；
- 0.8 V 的单位、公式和实测点是什么；
- 如何防止重复连接、重复断开和链路抖动造成重复发声；
- 如果提示音失败，如何保证律动和安全主链仍然可用。

在这份蓝图评审通过、`AUDIO:CUE` 协议字段冻结并完成实现前代码复核前，不进入代码施工。
