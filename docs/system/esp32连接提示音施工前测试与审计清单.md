# ESP32 连接提示音施工前测试与审计清单

状态：施工后自动验证清单（真机/示波器项目待执行）
适用功能：连接「滴——叮」、断开「叮——咚」
前置文档：[ESP32 连接与断开提示音施工蓝图](esp32连接提示音施工蓝图.md)
原则：先证明状态和协议，再证明声音；先做自动检查，再做真机和示波器验收。

## 1. 施工前门禁（已完成）

代码施工开始前必须全部满足：

- [ ] 已冻结 `AUDIO:CUE` 命令、ACK 字段、能力兼容方式和 OTA 期间行为。
- [ ] 已冻结本轮声音参数：CONNECT=`660 Hz → 880 Hz`，DISCONNECT=`440 Hz → 330 Hz`，两段间隔约 20 ms，包络 5–10 ms；后续只验听感、时长和电压。
- [ ] 已冻结 ESP32 的连接音边沿：有效 `BLE_CONNECTED` 生命周期事件经控制任务校验后，只触发一次 CONNECT CUE。
- [ ] 已确认 Android/iOS 不再发送 CONNECT CUE；旧 APP 的 `AUDIO:CUE id=CONNECT` 仅作为兼容入口，并与本地连接事件按 firmware `sessionId` 去重。
- [ ] 已定义 ESP32 的 `stopSoft` 完成通知或序列号；由 `audioTask` 发布，控制任务只能异步等待，不得阻塞 BLE 回调。
- [ ] 已定义 ESP32 transport session 去重身份。当前 `BleTransport::sessionId` 在新连接时递增，断开时不递增，施工应按该 session 做一次性去重。
- [ ] 已明确产品构建不能启用 `WARNING_ONLY` 断连策略；若必须保留该配置，先实现由 `SystemStateMachine` 发起的独立停波请求。
- [ ] 已确认 CUE 只由 `WaveModule::audioTask` 写 I2S，`isOutputActive()` 继续表示 WAVE-only。
- [ ] 已保留旧 `CAP? / SNAPSHOT? / WAVE:* / EVT:* / ACK:* / NACK:*` 的 golden fixture 和回归结果。
- [ ] 已确认 Android 的 Home 点击 `SoundPool`（默认关闭）和 iOS 的功能音乐 `MediaPlayer` 不接入连接提示音 owner。
- [ ] 已确认真实 PCM5102A、示波器和律动仪硬件可用；没有硬件时只能完成逻辑/编译测试。

## 2. 固件纯策略测试

这些测试不依赖 ESP32 硬件，优先放进 host-side focused test 或纯 C++ policy seam。

| 编号 | 测试 | 预期 |
|---|---|---|
| FW-CUE-001 | 有效 BLE_CONNECTED 生命周期事件 | 由 `HubHandler::onBleConnected()` 接受一次 CONNECT 播放计划 |
| FW-CUE-002 | 同一 session 首次 CONNECT（本地事件或旧 APP 兼容命令） | 接受一次 |
| FW-CUE-003 | 同一 session 重复 CONNECT | `DUPLICATE`，不重复播放 |
| FW-CUE-004 | 新 session BLE_CONNECTED | 允许一次本地 CONNECT |
| FW-CUE-005 | 空闲时 DISCONNECT | 接受一次 |
| FW-CUE-006 | 同一 `sessionId` 重复断开 | `DUPLICATE`，不重复播放 |
| FW-CUE-007 | pending CONNECT 后收到 DISCONNECT | 丢弃 CONNECT，DISCONNECT 优先 |
| FW-CUE-008 | WAVE running 后 DISCONNECT | 先产生 stop handoff，再产生 DISCONNECT CUE |
| FW-CUE-009 | stop 尚未完成 | 不提前写 CUE 样本 |
| FW-CUE-010 | stop 完成后 | 只播放 DISCONNECT CUE |
| FW-CUE-011 | CUE 失败/队列满/I2S 错误 | 返回明确结果并保持 WAVE/安全主链可用 |
| FW-CUE-012 | CUE 播放期间读取 WAVE 参数 | 频率、强度、相位目标、`run_state` 不被修改 |
| FW-CUE-013 | 旧 BLE_DISCONNECTED 事件延迟到新连接之后 | 按 session/connId 丢弃旧事件，不误停新连接、不播断开音 |
| FW-CUE-014 | CONNECT RX 已入队后发生断开，旧命令晚处理 | 按 session/connId 和 `deviceConnected` 丢弃，不播放连接音 |
| FW-CUE-015 | CONNECT CUE 播放中收到 `WAVE:START` | 按冻结优先级延迟或明确拒绝，不与 CUE 同时写 DMA |
| FW-CUE-016 | `WARNING_ONLY` 断连策略组合 | 产品构建拒绝该组合，或先由 FSM 完成独立停波；绝不在 WAVE 输出时播断开音 |
| FW-CUE-017 | BLE 生命周期事件队列满/入队失败 | 不猜测状态、不播错误音，走安全降级并留下中文异常日志 |

## 3. 数字音频测试

### 3.1 频率与样本数

当前采样率为 16 kHz：

| 音段 | 频率 | 时长 | 样本数 | 预期 |
|---|---:|---:|---:|---|
| 连接第一音 | 660 Hz | 90 ms | 1440 | DDS 频率接近目标 |
| 连接第二音 | 880 Hz | 140 ms | 2240 | DDS 频率接近目标 |
| 连接间隔 | 静音 | 20 ms | 320 | 不残留上一音 |
| 断开第一音 | 440 Hz | 100 ms | 1600 | DDS 频率接近目标 |
| 断开第二音 | 330 Hz | 160 ms | 2560 | 真机确认不闷、不像故障声 |

频率必须使用 CUE 独立相位累加器，不能调用 WAVE 的 0–50 Hz `setFreq()`。

### 3.2 包络和幅度

- [ ] 渐入/渐出 5–10 ms，样本数为 80–160。
- [ ] 样本峰值不超过 `MAX_AMPLITUDE=8826`。
- [ ] 连接音和断开音中间稳定段峰值目标与 WAVE 强度 120 相同。
- [ ] 不因 CUE 修改 WAVE intensity、WAVE ramp 或 `EVT:WAVE_OUTPUT`。
- [ ] 立体声左右声道样本一致，或在合同中明确差异。

## 4. 协议和兼容测试

| 编号 | 测试 | 预期 |
|---|---|---|
| PROTO-CUE-001 | 解析 `AUDIO:CUE id=CONNECT` | 得到明确 CUE 类型 |
| PROTO-CUE-002 | 缺少 `id` | `NACK:INVALID_PARAM` |
| PROTO-CUE-003 | 未知 `id` | `NACK:INVALID_PARAM` 或明确 `NACK:UNSUPPORTED_CUE` |
| PROTO-CUE-004 | CUE ACK accepted/duplicate/rejected | 字段稳定、单帧不超 MTU |
| PROTO-CUE-005 | 旧 CAP/SNAPSHOT/WAVE golden frames | 与施工前完全一致 |
| PROTO-CUE-006 | OTA 期间 CUE | 按冻结策略返回可解释结果，不绕过 OTA business block |
| PROTO-CUE-007 | 旧 APP 连接旧固件 | 不因没有 CUE 能力而判连接失败 |
| PROTO-CUE-008 | 新 APP 连接旧固件 | CUE 为 best-effort，WAVE/停止/保护仍可用 |

建议将 `cue_supported=1` 纳入能力合同；如果本轮不增加 CAP 字段，也必须在 APP 侧把 CUE ACK/NACK 当作可选能力处理，并记录一次能力缺失。

## 5. Android 测试

连接音真相位于 ESP32 BLE 生命周期和 `WaveModule`，不在 Android gateway/repository 或页面：

| 编号 | 场景 | 预期 |
|---|---|---|
| AND-CUE-001 | BLE transport connected，CAP/SNAPSHOT 未完成 | ESP32 已在有效 BLE_CONNECTED 生命周期中本地触发；APP 不发送 CONNECT CUE |
| AND-CUE-002 | 当前 firmware `sessionId` 首次有效 BLE_CONNECTED | ESP32 发送一次 CONNECT CUE 到唯一 audioTask |
| AND-CUE-003 | 重复连接回调、重复 ACK、页面重建或旧 APP 重复命令 | firmware session 去重，不重复发声 |
| AND-CUE-004 | firmware `sessionId` 改变后再次连接 | 新 session 允许一次本地 CONNECT CUE |
| AND-CUE-005 | 旧 APP `AUDIO:CUE` 到达本地连接音之后 | 返回 `DUPLICATE`，连接仍保持成功 |
| AND-CUE-006 | 用户主动断开 | 不发送断开命令，等待 ESP32 本地断开音 |
| AND-CUE-007 | 旧 epoch ACK/refresh 在新 epoch 到达；`sendCommand()` 时 transport 已断开 | 丢弃旧结果/不发送 CUE，不把旧结果当作新连接 ready |

应保留旧 APP 兼容命令的 parser/去重回归；正式 APP 的连接、协议 ready 和页面生命周期测试不得再把 CONNECT CUE 发送次数当成连接成功条件。

## 6. iOS 测试

iOS 当前没有名为 `protocolReady` 的单一字段。CoreBluetooth 已 connected 只说明 BLE 链路阶段完成，正式 ready 需要当前连接的正式 SNAPSHOT，并且数据属于当前 `connectionGeneration`。

| 编号 | 场景 | 预期 |
|---|---|---|
| IOS-CUE-001 | CoreBluetooth connected，但没有 SNAPSHOT | 不影响 ESP32 已按 BLE_CONNECTED 生命周期本地触发的连接音 |
| IOS-CUE-002 | 当前 generation 收到 fresh SNAPSHOT | 只刷新原有连接/设备状态，不发送 CONNECT CUE |
| IOS-CUE-003 | 重复 SNAPSHOT/重复 identity readback | 不发送 CONNECT CUE、不重复发声 |
| IOS-CUE-004 | 旧 generation 的 SNAPSHOT 到达 | 丢弃旧状态，不影响 firmware session 音效 |
| IOS-CUE-005 | 新 generation 再次连接 | firmware 新 session 允许一次本地 CONNECT CUE |
| IOS-CUE-006 | 主动/异常断开 | 不发送断开命令，由 ESP32 本地事件负责 |
| IOS-CUE-007 | 新固件没有 CUE 能力 | 连接状态仍按正式 snapshot 投影，CUE 作为可选能力缺失 |

APP 侧保留旧命令解码兼容即可；正式连接链路不再需要 `SonicWaveCommand`、`SonicWaveProtocolCodec` 或 repository 主动发送 CONNECT CUE。

## 7. 真机顺序测试

每条用例都要同时保留 APP 日志、ESP32 串口日志和时间顺序：

1. 空闲连接：扫描 → BLE connected → 协议确认 → 只出现一次「滴——叮」。
2. BLE connected 但协议确认失败：保持无声，不误播连接音。
3. 空闲断开：真实断开 → 只出现一次「叮——咚」。
4. 律动中断开：WAVE 仍在输出 → 断开 → WAVE 软停 → `isOutputActive=false` → 断开音。
5. 快速断连重连：第一 session 断开音只一次，第二 session 连接音只一次。
6. 重复连接/断开回调：不出现第二个同类提示音。
7. WAVE 回归：提示音前后的频率、强度、开始、停止和保护行为与施工前一致。
8. 日志回归：日常只打印中文摘要；调试模式关闭时不打印每帧采样信息。

### 7.1 生命周期竞态专测

- 断开回调产生 `session=N` 的消息后，立即建立 `session=N+1`，确认旧断开消息不能影响新连接。
- 在 `session=N` 的 CONNECT 命令已进入控制队列后断开，确认旧命令不会在断开后播放连接音。
- 在高频 RX/WAVE 控制压力下人为填满生命周期队列，确认失败路径不会把“未确认的连接/断开”当成事实。

## 8. PCM5102A 电压验收

施工后且硬件接好才执行：

- 测量点：PCM5102A `OUTL` 或 `OUTR` 对 `AGND`。
- 主指标：稳定音段 `Vpk` 约 `0.8 Vpk`。
- 辅助指标：`Vpp` 约 `1.6 Vpp`；稳定正弦段 `Vrms` 约 `0.566 Vrms`。
- 连接音、断开音、WAVE 强度 120 各测一次，记录频率、波形、Vpk、Vpp、Vrms 和负载条件。
- 不把 TPA3255 功放后的喇叭端电压当作 PCM5102A 电压。
- 没有实测记录时，结论只能写“理论目标”，不能写“0.8 Vpk 已验收”。

## 9. 听感验收

- [ ] 连接音确实听起来像「滴——叮」，明亮但不刺耳。
- [ ] 断开音确实听起来像「叮——咚」，低沉但不闷、不像故障报警。
- [ ] 330 Hz 在实际喇叭/振动平台上可辨识；如果不可辨识，试听备选 `440→350 Hz` 或 `500→350 Hz`。
- [ ] 两个音效在律动停止后可清楚听到，且不会让用户误以为 WAVE 又重新开始。
- [ ] 音效在重复连接/断开时不烦扰，不连续重复。

## 10. 基线和交付记录

施工前基线已验证：

- `python3 tools/run_evaluator_unit_tests.py`：通过。
- `python3 tools/test_release_build_contract.py`：通过，4 个测试通过。
- `python3 -m platformio run -e esp32s3`：通过。
- `python3 tools/test_bootchain_baseline_build_flags.py`：当前环境缺少 `SCons`，未运行成功。
- `git diff --check`：通过。

本次施工后已完成：

- `python3 tools/run_evaluator_unit_tests.py`：通过，包含 `AUDIO:CUE` 解析、非法参数和 CAP 能力字段。
- `python3 tools/test_release_build_contract.py`：通过，4 个测试通过。
- `python3 -m platformio run -e esp32s3`：通过。
- Android `:sonicwave-protocol:test`、`:core-device-sonicwave:test`：通过。
- iOS `DeviceContractsTests`：30 项通过；`DefaultSonicWaveDeviceRepositoryTests` 运行 68 项、7 项失败，完整 `CoreDeviceSonicWaveTests` 运行 218 项、8 项失败。失败集中在既有设备事实/重连测试，尚未完成独立基线隔离，因此本轮只认定协议 focused 测试通过，不把 Repository 全量结果认定为本次改动通过。
- `git diff --check`：通过。

仍待执行：

- 真机 APP + ESP32 时序日志。
- PCM5102A 示波器记录。
- 330 Hz 听感结论。

## 11. 失败处理

- 任何 CUE 失败都不能让连接、WAVE、保护或停波失败。
- stop handoff 超时则保持静音，不提前播放断开音；记录 `CUE_REJECTED reason=STOP_NOT_CONFIRMED`。
- I2S 写入失败则写静音并结束 CUE，不能继续输出旧 WAVE 样本。
- 旧固件不支持 CUE 时，APP 只记录能力缺失，不把它升级为连接失败。
- 330 Hz 不可听时只替换断开音参数，不改连接真相和停波时序。
