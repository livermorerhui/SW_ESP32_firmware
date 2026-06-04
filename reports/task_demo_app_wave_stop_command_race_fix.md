# Demo APP Wave Stop Command Race Fix

状态：本轮交付记录
文档类型：阶段报告
适用范围：ESP32 固件仓 `tools/android_demo` 手动 `Start -> Stop` 控制链
Owner：Demo APP control lifecycle
更新日期：2026-06-03
真相源：`docs/protocol.md`、`src/core/SystemStateMachine.cpp`、`src/modules/wave/WaveModule.cpp`
证据源：代码审计、`tools/android_demo` focused unit test

## 1. 目标链路

Demo APP 用户点击 `Start` 后，立即点击 `Stop`，`WAVE:STOP` 必须能作为后发控制意图生效，不能被旧的 in-flight `Start` 协程继续发送 `WAVE:START` 覆盖。

## 2. 相关链路

- Demo APP `sendWaveStart()`：发送 `WAVE:SET -> WAVE:START`。
- Demo APP `sendWaveStop()`：取消 live 参数发送，发送 `WAVE:STOP`。
- BLE transport：`BluetoothGattTransport.writeLine()` 通过 `writeMutex` 串行写入。
- ESP32 固件：`WAVE:STOP` 进入 `SystemStateMachine::requestStop()`，再由 `WaveModule::stopSoft()` 执行软停止。

## 3. 真相源 / 证据源

- 真相源：固件协议规定 `WAVE:STOP` 是停止输出请求，响应 `ACK:OK`。
- 证据源：Demo APP TX 日志、ESP32 `[CMD] WAVE_STOP received`、`[LAYER:COMMAND_DISPATCH] cmd=WAVE:STOP`、`[LAYER:OUTPUT_DRIVER] action=i2s_stop`。

## 4. 审计结论

固件侧没有发现“启动后禁止停止”的状态门控。`WAVE:STOP` 不走 start gate，不会因为 `NOT_ARMED` 或 `FAULT_LOCKED` 拒绝停止。固件输出仍保留 `RAMP_STOP_TIME_MS=500ms` 软停止，因此 0.5 秒级尾振属于设计内。

Demo APP 存在真实代码风险：`sendWaveStart()` 会在独立协程里连续发送 `WAVE:SET` 和 `WAVE:START`。用户快速点击 `Stop` 时，`pendingWaveStartRequest` 会被清空，但已经启动的 Start 协程不会被取消，仍可能在 Stop 后继续发送旧 `WAVE:START`。这会表现为第一次 Stop 后输出又被旧 Start 拉起，需要再次点击 Stop。

## 5. 改动点

- 新增 `WaveLifecycleCommandGate`，为 Start/Stop 控制意图建立单调 token。
- `sendWaveStart()` 在发送 `WAVE:SET` 后、发送 `WAVE:START` 前二次检查 token。
- 如果 Stop 已经作废旧 Start，则旧 Start 不再继续发送 `WAVE:START`。
- 旧 Start 的失败回调如果发生在 Stop 之后，不再取消当前 Stop pending。
- `sendWaveStop()` 先发送 `WAVE:STOP`，不再在 Stop 发送前调度 `SNAPSHOT` truth refresh，避免查询命令排在停止命令前面。

## 6. 抽象点

`WaveLifecycleCommandGate` 只表达控制意图新旧关系，不依赖 UI 展示字段、设备显示名、时间戳或随机 ID。它属于 Demo APP 命令生命周期 owner，不改变 BLE wire format 和固件状态机。

## 7. 参数点

本轮未新增可配置参数。固件 `RAMP_STOP_TIME_MS=500ms` 保持不变。

## 8. 验证结果

已执行：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:testDebugUnitTest
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest
```

结果：通过。

补充检查：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check
```

结果：通过。

## 9. 剩余风险

本轮代码修复了“旧 Start 覆盖后发 Stop”的明确竞争风险，但下午现场现象仍需真机证据确认：

- 如果 APP TX 日志没有 `WAVE:STOP`，问题在 Demo APP UI/发送链。
- 如果 APP TX 有 `WAVE:STOP` 但 ESP32 串口没有 `[CMD] WAVE_STOP received`，问题在 BLE 传输/连接窗口。
- 如果 ESP32 收到 `WAVE:STOP` 但没有 `stopSoft / i2s_stop`，才回到固件执行链审计。
- 如果 ESP32 正常 `i2s_stop` 但体感仍不停止，需检查外设输出或体感判断窗口。

## 10. 后续验证建议

下一步需要安装新 Demo APP 后真机复测快速 `Start -> Stop`。复测时至少观察 APP raw TX 和 ESP32 串口，确认第一次 Stop 是否发出、是否到达固件、是否触发 `i2s_stop`。

## 11. 真机复核记录

时间：2026-06-04 16:46-16:48

capture：

`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260604_164648__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-wave-stop-race__manual__ESP32_RUNTIME__demo_app_wave_stop_race`

用户体感：通过。快速 `Start -> Stop` 复测中，第一次 Stop 均能停止。

证据结论：

- `session_meta.env` 记录 `RESULT=pass`，summary 为“Demo APP 快速 Start -> Stop 复测通过，第一次 Stop 均能停止”。
- Android focus log 可读，连续出现 `SNAPSHOT top_state=RUNNING -> EVT:STOP stop_reason=MANUAL_STOP ... state=ARMED -> SNAPSHOT top_state=ARMED`。
- `capture_evidence_summary.txt` 记录 Android `TRANSPORT_EVT_STOP_COUNT=36`，ESP32 `WAVE_START_COUNT=18`、`WAVE_STOP_COUNT=18`。
- ESP32 串口可读，每轮停止均有 `WAVE_STOP received`、`cmd=WAVE:STOP`、`STOP REQUEST`、`RUNNING->ARMED action=wave.stopSoft`、`action=i2s_stop ... reason=req_run_cleared`。
- 未见 `GURU / PANIC / BROWNOUT`。

非 blocker 说明：

- `warnings.log` 仅有 `backend_capture_skipped mode=skip`，本场景不涉及 backend，符合预期。
- `runtime_events_session.jsonl` 为空；本轮使用固件仓 Demo APP，不是 SW 正式 APP runtime event 链，不作为 blocker。
- `measurement_health=FAULT / MEASUREMENT_UNAVAILABLE / Modbus read fail` 属于本轮 ESP32-plus degraded/测量健康背景噪声，不影响 `Start -> Stop` 控制竞态判断。

正式判断：通过。本轮修复已解除“旧 Start 覆盖后发 Stop”这一明确竞态风险；后续若再次复现，应按 APP raw TX、ESP32 串口接收、固件输出停止三段证据重新定位，不直接扩大固件协议或状态机修改范围。
