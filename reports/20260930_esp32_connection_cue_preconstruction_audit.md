# ESP32 连接与断开提示音施工前审计

日期：2026-09-30
范围：`SW_ESP3_Firmware`、Android/iOS 连接确认链、PCM5102A 输出链
阶段：施工前审计，不含代码施工和烧录

## 1. 审计结论

| 项目 | 结论 | 说明 |
|---|---|---|
| 通用工程门禁 | `general-partial` | 单一 I2S owner、状态机停波和 BLE 控制任务已经存在；CUE 状态、事件去重、协议入口和测试尚未实现 |
| SW 项目门禁 | `sw-partial / prepared` | APP `protocolReady`、ESP32 断开事件、WAVE 运行事实和幅度公式已查实；APP/固件 CUE 合同仍需施工前冻结 |
| 架构门禁 | `architecture-partial` | 可在现有 WaveModule 内加入独立 CUE 调度；不能由 BLE 回调或 APP 页面直接写 I2S |
| 声音设计 | `pass with implementation check` | 短促上行连接音、短促下行断开音有成熟资料支持；精确频率是工程推荐，不是标准规定 |
| 数字生成可行性 | `pass` | 当前 16 kHz、16 bit、立体声 I2S 足够生成 330–880 Hz；样本数可确定 |
| PCM5102A 电压 | `evidence-missing` | 0.8 Vpk 是按当前数字幅度推导的名义值，工作区当前没有 PCM5102A 实测证据 |
| 机械/听感 | `evidence-missing` | 没有律动仪喇叭/振动平台在 330–880 Hz 的频响数据，必须真机试听 |
| 生命周期消息安全 | `fail until fixed` | 当前 `ControlMsg` 没有 session/connId，旧断开或旧 CONNECT 命令可能在新连接后晚处理；必须先补消息身份和过期丢弃 |

## 2. 资料复核轮次

### 第一轮：系统提示音语义

- Android Earcons 指南要求提示音短、简化、直观，并避免仿造系统音。
- freedesktop Sound Naming 将 `device-added`、`device-removed`、`network-connectivity-established`、`network-connectivity-lost` 分为不同事件。
- Microsoft Mixed Reality 空间音频设计明确建议连接/断开使用短促的音调音，并把上升音调用于正向连接、下降音调用于结束或断开。
- Microsoft Windows UX 声音指南建议界面音短于 1 秒、使用柔和包络，常用界面频段约为 600 Hz–2 kHz。

结论：连接「滴 → 叮」、断开「叮 → 咚」的方向设计正确；330 Hz 断开尾音表达低沉感可以保留为候选，但必须实机听感复核，不应声称是行业标准。

### 第二轮：音频链路和数字参数

- Espressif I2S 标准模式支持立体声双 slot、16 bit 数据和 DMA 输出；现有固件使用 16 kHz、16 bit、LR stereo。
- PCM5102A 官方资料说明其支持 16/24/32 bit PCM、8–384 kHz 和 ground-centered 模拟输出；16 kHz 输入在规格范围内。
- 当前 `WaveModule` 使用 32 位相位累加器和 `SAMPLE_RATE=16000`。提示音频率不能调用 WAVE 的 `setFreq()`，因为该入口正式限制为 0–50 Hz。
- 连接总时长为 `90 + 20 + 140 = 250 ms`，断开总时长为 `100 + 20 + 160 = 280 ms`。
- 16 kHz 下对应样本数分别为 1440/2240、1600/2560，间隔 320 样本，5–10 ms 包络为 80–160 样本。

结论：推荐参数在现有 I2S/DDS 链路中可实现，不需要提高采样率，也不需要新增音频任务。

### 第三轮：本项目源码与协议

已核对：

- `WaveModule::audioTask()` 是当前唯一 `i2s_write` owner。
- `SystemStateMachine::setState()` 是 WAVE start/stop owner；断开安全策略最终会走 `wave->stopSoft()`。
- `BleTransport` 回调只向控制任务投递 `BLE_CONNECTED/BLE_DISCONNECTED`，不应在回调中写 I2S。
- APP Android gateway 在 capability/snapshot 确认后才将 `protocolReady=true`，并记录 `CONNECT_SUCCESS`；但当前 `markProtocolReady()` 和通用 `sendCommand()` 仍需要显式绑定当前 `transportEpoch`，否则旧 ACK/旧 refresh 可能在新连接中误触发 ready 或 CUE。
- iOS `DefaultSonicWaveDeviceRepository` 使用 `connectionGeneration` / `protocolReady`，但当前 `SonicWaveProtocolCodec` 只有 CAP、SNAPSHOT、WAVE 和保护命令，没有 CUE 命令。
- Android 现有 `SoundPool` 只用于 Home 页点击声且默认关闭；iOS 现有 `MediaPlayer` 只用于功能音乐；两者都不是连接提示音 owner。
- 当前 `CmdType`、`Command`、`HubHandler` 和 Android/iOS 命令枚举均没有 CUE 入口。
- 当前正式 BLE 线格式 `CAP? / SNAPSHOT? / WAVE:* / EVT:* / ACK:* / NACK:*` 没有提示音语义。
- 当前 `BleTransport::ControlMsg` 只有类型和文本，生命周期/RX 消息没有捕获 session/connId；断开消息入队延迟、随后新连接建立时，旧消息可能误影响新 session。
- 当前断开事件入 `controlQueue` 失败时只记计数/日志；CUE 不能建立第二条不可靠断开链，必须定义队列满时的安全降级。
- 当前 BLE 断连策略允许 `WARNING_ONLY`，该模式可能保持 WAVE 输出；它与“律动中断开先停波再播放断开音”的产品合同冲突，必须禁止组合或由 FSM 增加独立停波请求。

结论：现有主链适合增加独立可选 CUE 能力；必须同时补固件协议 owner、Android owner、iOS owner、去重身份和 focused tests，不能只改固件。

## 3. 固定产品决定

1. 连接成功音：上行「滴——叮」，本轮先用 660 Hz → 880 Hz。
2. 断开音：下行「叮——咚」，本轮先用 440 Hz → 330 Hz。
3. 提示音由 ESP32 经 PCM5102A 和律动仪喇叭输出，手机不直接播放。
4. 律动中断开时，先完成既有 WAVE 软停并确认 `isOutputActive()==false`，再播断开音。
5. 提示音不改变 WAVE 参数、WAVE 状态、WAVE 事件和强度百分比逻辑。
6. 数字峰值沿用 `MAX_AMPLITUDE=8826`，PCM5102A 名义目标约为 `0.8 Vpk`。

## 4. 施工前必须冻结的接口

### 4.1 建议内部接口

```text
requestCue(cueId, connectionEpoch, triggerSource)
    -> ACCEPTED / DUPLICATE / QUEUED / REJECTED
```

该接口只能把请求送进 WaveModule 的音频调度，不允许携带任意 WAVE 频率或强度，不允许从页面直接传 PCM 样本。

### 4.2 建议协议接口

```text
AUDIO:CUE id=CONNECT
ACK:AUDIO_CUE id=CONNECT result=ACCEPTED
```

断开音由 ESP32 本地 BLE 断开事件触发，不依赖 APP 发送断开命令。命令名称、ACK 字段、旧固件兼容策略和能力探测方式需要在代码施工前写入正式协议文档及 golden fixture。

## 5. 关键风险和处理

| 风险 | 现状 | 施工要求 |
|---|---|---|
| 连接音重复 | APP 有重试、重连、页面重建路径 | 只认当前 `transportEpoch`/`connectionGeneration` 内 ready 首次变为 true；`markProtocolReady()` 和 `sendCommand()` 都要校验当前连接仍有效 |
| 断开音重复 | BLE 回调可能重复或经过恢复路径 | 使用 ESP32 transport session 去重 |
| 连接音与断开音交错 | 连接音等待时可能马上断开 | 断开优先，丢弃未播放连接音 |
| WAVE 被覆盖 | 当前 WAVE 参数是唯一运行参数 | CUE 独立相位、频率、包络和状态，不改 WAVE 字段 |
| 两个任务写 I2S | 若新增独立音频 task 会产生竞态 | 继续由 `audioTask` 独占 I2S |
| 330 Hz 太闷 | 没有喇叭/平台频响证据 | 施工后试听；必要时备选 440→350 或 500→350 |
| 0.8 V 偏差 | 当前只有数字推导 | 在 PCM5102A OUTL/OUTR 对 AGND 实测 Vpk/Vpp/Vrms |
| 旧 APP/旧固件兼容 | 当前没有 CUE 命令 | CUE 为可选能力，不能阻塞连接、WAVE 或安全主链 |
| 生命周期乱序 | 控制消息没有 session/connId | 施工前给消息绑定捕获身份，控制任务丢弃过期连接、断开和 RX 命令 |
| 断连配置冲突 | `WARNING_ONLY` 可能不停止 WAVE | 正式产品禁止该组合，或由 FSM 先完成独立停波 |

## 6. 施工前基线证据

本轮未改运行代码，当前固件基线验证结果：

| 检查 | 结果 |
|---|---|
| `python3 tools/run_evaluator_unit_tests.py` | 通过 |
| `python3 tools/test_release_build_contract.py` | 通过，4 个测试通过 |
| `python3 -m platformio run -e esp32s3` | 通过，40.14 秒 |
| `python3 tools/test_bootchain_baseline_build_flags.py` | 未运行成功，当前 Python 缺少 `SCons`；未下载依赖 |
| `git diff --check` | 通过 |

PlatformIO 构建存在既有编译警告，但本轮没有新增代码，构建成功且没有链接错误。

## 7. 审计结论

可以进入下一阶段的 **CUE 调度器实现设计**，但不能跳过以下门禁：

1. 先冻结 `AUDIO:CUE` 命令、ACK、能力兼容和 Android/iOS 调用 owner，并把本轮两种提示音参数写入合同。
2. 先修复生命周期消息的 session/connId 绑定和过期丢弃，再接入 CUE。
3. 先建立纯策略测试，覆盖事件资格、连接 epoch 去重、断开优先级、WAVE/CUE 隔离。
4. 明确 `WARNING_ONLY` 与本功能的禁止组合，保证律动中断连一定先停波。
5. 当前按 330 Hz 施工；只有真机听感证明低音不清楚时，才在不改变时序和语义的前提下替换为备选低音。
6. 没有 PCM5102A 实测设备前，不得写“0.8 Vpk 已验收”；只能写“理论目标”。
7. CUE 代码完成后必须重新跑现有 evaluator、PlatformIO build、BLE 回归和真实声音/电压验收。

## 8. 资料来源

- [Microsoft Mixed Reality Spatial Sound Design](https://learn.microsoft.com/en-us/windows/mixed-reality/design/spatial-sound-design)：连接/断开短音、上行正向、下行结束的成熟产品设计参考。
- [Microsoft Windows UX Sound](https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-sound)：短于 1 秒、柔和包络和常用界面声频段参考。
- [Android Earcons](https://developer.android.com/design/ui/ai-glasses/guides/interaction/earcons?hl=en)：短、简化、直观，避免仿系统声。
- [Android BLE 数据传输](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)：BLE 已连接后仍需服务发现、特征确认和通知链路，支持把 CONNECT CUE 绑定在协议 ready 边沿。
- [Apple CoreBluetooth didConnect](https://developer.apple.com/documentation/corebluetooth/cbcentralmanagerdelegate/centralmanager%28_%3Adidconnect%3A%29) 与 [didDisconnect](https://developer.apple.com/documentation/corebluetooth/cbcentralmanagerdelegate/centralmanager%28_%3Adiddisconnectperipheral%3Aerror%3A%29)：连接/断开回调是传输层事实，不能替代当前协议快照 ready，也不能在断开后继续发送 CUE 命令。
- [freedesktop Sound Naming Specification](https://agx.pages.freedesktop.org/xdg-specs/sound-naming-spec/sound-naming-spec-latest.html)：设备加入/移除和连接建立/丢失事件分类。
- [TI PCM5102A 产品页](https://www.ti.com/product/PCM5102A) 与 [数据手册](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf)：PCM 输入范围和 ground-centered 输出资料。
- [Espressif I2S 官方文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html)：16 bit stereo slot、DMA 和 I2S 写入边界。
- [Auditory icons review](https://pubmed.ncbi.nlm.nih.gov/31046954/)：建议明确记录提示音的强度、时长和频率，便于复现和比较。

公开资料只支撑通用工程原则；SW 的协议、WAVE 状态和设备安全语义仍以项目源码和正式合同为准。
