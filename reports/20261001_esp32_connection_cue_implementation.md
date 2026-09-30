# ESP32 连接与断开提示音施工实现报告

日期：2026-10-01
范围：`SW_ESP3_Firmware`、Android `core-device-sonicwave`、iOS `SonicWaveCore`

## 结论

代码施工已完成，固件和两端 APP 的自动验证通过。当前没有烧录真机，也没有 PCM5102A 示波器读数，因此“实际听感”和“输出端约 0.8 Vpk”仍需硬件验收，不能仅凭编译结果宣称通过。

## 已实现的链路

1. ESP32 `CAP?` 返回 `cue_supported=1`。
2. Android 在当前 transport epoch 同时满足“已连接、协议已 ready、CAP 支持 CUE、该 epoch 尚未发送”时发送一次 `AUDIO:CUE id=CONNECT`。
3. iOS 在当前 connection generation 同时满足“已连接、fresh SNAPSHOT 已到达、CAP 支持 CUE、该 generation 尚未发送”时发送一次同样的命令。
4. ESP32 解析命令后由 `HubHandler → WaveModule::requestCue()` 接收；CUE 与 WAVE 共用现有 `I2S_Audio` 任务和 I2S 写入 owner。
5. ESP32 真实 BLE 断开事件先进入既有 `SystemStateMachine::onBleDisconnected()` 停波路径；同一个 audio task 在 WAVE 静音且 I2S 停止后播放 DISCONNECT CUE。
   APP 不能通过 RX 伪造 DISCONNECT；该命令面返回 `LOCAL_ONLY`。
6. BLE 控制队列消息携带 `sessionId/connId`；旧连接的 RX、连接、断开消息在控制任务中丢弃。重复生命周期事件不重复触发提示音。
7. 新连接在旧断开提示音尚未结束时，会按新 session 优先级抢占旧 CUE，避免快速重连漏播 CONNECT。

## 固件参数

| 项目 | CONNECT | DISCONNECT |
|---|---:|---:|
| 第一音 | 660 Hz / 90 ms | 440 Hz / 100 ms |
| 第二音 | 880 Hz / 140 ms | 330 Hz / 160 ms |
| 音间隔 | 20 ms | 20 ms |
| 包络 | 8 ms 渐入/渐出 | 8 ms 渐入/渐出 |
| 数字峰值 | `MAX_AMPLITUDE=8826` | `MAX_AMPLITUDE=8826` |

WAVE 原有 0～50 Hz、强度 0～120、软停、`isOutputActive()` WAVE-only 语义未改写。CUE 不进入 WAVE 强度百分比链路。

## 主要改动位置

- 固件：`src/core/CommandBus.h`、`src/core/ProtocolCodec.h`、`src/HubAckBuilder.h`、`src/modules/wave/WaveModule.*`、`src/transport/ble/BleTransport.*`、`src/main.cpp`。
- Android：`sonicwave-protocol` 命令模型/编码、`SonicWaveDeviceCommand`、`SonicWaveSdkDeviceGateway`、`ConnectCueTriggerPolicy` 及其测试。
- iOS：`SonicWaveCommand`、协议编码、`DefaultSonicWaveDeviceRepository`、设置日志映射及 focused tests。
- 合同文档：`docs/protocol.md`、`docs/ble-init-contract.md`、`docs/demo_app_protocol.md`、`docs/architecture_runtime.md`、施工蓝图和复核清单。

## 自动验证证据

- `python3 tools/run_evaluator_unit_tests.py`：通过，覆盖 CUE 合法/非法解析、CAP 能力字段和 ACK 文本。
- `python3 tools/test_release_build_contract.py`：通过，4 个测试通过。
- `python3 -m platformio run -e esp32s3`：通过。
- `./gradlew --offline :sonicwave-protocol:test :core-device-sonicwave:test --no-daemon`：通过。
- iOS `DeviceContractsTests` 与 `testConnectCueIsSentOnceAfterFreshSnapshotWhenCapabilityIsSupported`：通过。
- `git diff --check`：通过。

完整 iOS `CoreDeviceSonicWaveTests` 出现 6 个失败，集中在旧连接 late frame 超时和初始事实断言。本次新增 CUE focused test 已单独通过；目前尚未对这 6 个失败做独立基线隔离，不能断言它们与本次改动无关，也不把完整套件写成通过。

`tools/test_bootchain_baseline_build_flags.py` 仍受本机系统 Python 缺少 `SCons` 影响，未安装新依赖；PlatformIO 自带环境的正式固件构建已通过。

## 尚未完成的硬件验收

- 真机连接、空闲断开、律动中断开、快速断连重连的 APP/ESP32 时序日志。
- PCM5102A `OUTL/OUTR` 对 `AGND` 的 CONNECT、DISCONNECT、WAVE 强度 120 测量：`Vpk`、`Vpp`、`Vrms`。
- 实际律动仪喇叭对 330 Hz 断开音的听感确认。

硬件验收完成前，交付结论应写作“代码完成、硬件待验收”。
