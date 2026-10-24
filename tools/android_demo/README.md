# SonicWave Android Demo

当前维护入口：[ESP32、Demo APP 与工厂交付维护说明](../../docs/system/esp32_demo_release_maintenance.md)。本文负责 Demo APP 的操作和协议入口；历史蓝图、源码事实与验收范围按该维护入口核对。

## Requirements
- Android Studio (latest stable)
- JDK 17
- Android phone with BLE enabled
- Android 12+ devices must grant `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT`
- Android 11 and below must grant Location permission for BLE scan

## Build
```bash
cd tools/android_demo
./gradlew :app-demo:assembleDebug
```

## Protocol Tests
```bash
cd tools/android_demo
./gradlew :sonicwave-protocol:test
```

## Run And Scan/Connect
1. Open `tools/android_demo` in Android Studio and run `app-demo` on device.
2. On the main screen, grant BLE permissions when prompted.
3. Tap `Search & Connect`.
4. In the bottom sheet, pick a device:
- Primary match: advertises service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- Secondary match: device name contains `Sonicwave`/`SonicWave`/`Vibrate`
5. Tap `Connect` on target device.
6. After connected, app auto-enables TX notify (CCCD), requests MTU (target 185), probes `CAP?`, enables Demo realtime stream with `STREAM:SET enabled=1,rate_hz=10` when `stream_control_supported=1`, then refreshes `SNAPSHOT?`.
- If no stream data arrives in 3 seconds, screen shows:
  `未收到测重数据（请确认已开启通知/固件在发送）`
- `设备原始日志` can be expanded to confirm both outgoing and incoming lines.
7. After connected, use:
- Scale: `ZERO`, `CALIBRATE` (`z`, `k` inputs)
- Wave: set `f`, `i`, then `START`, `STOP`
  - `START` sends `WAVE:SET f=<freq> i=<intensity>` then `WAVE:START` back-to-back.
  - `STOP` sends `WAVE:STOP`. If device replies `NACK:UNSUPPORTED`, demo falls back to legacy `E:0`.
8. Tap `Disconnect` to close BLE session.

## BLE UUIDs
- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (APP -> FW Write): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (FW -> APP Notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

## Framing Contract (Uplink)
- Firmware TX 每条逻辑消息末尾必须带 `\n`。
- Android transport 按 `\n` 做行重组；只上抛完整行。
- 解析前会剥离行尾 `\r`/`\n`，解析后的行不含换行符。
- MTU 请求失败不影响连接建立；分帧可靠性由 `\n` 重组保证。

## Canonical Incoming Messages
- ACK/NACK:
  - `ACK:CAP fw=SW-HUB-1.0.0 proto=2 platform_model=BASE laser_installed=0`
  - `ACK:OK`
  - `NACK:NOT_ARMED`
- Snapshot:
  - `SNAPSHOT: top_state=IDLE runtime_ready=1 start_ready=1 baseline_ready=0 platform_model=BASE laser_installed=0 laser_available=0 degraded_start_available=0 degraded_start_enabled=0`
- Live stream:
  - `ACK:STREAM enabled=1 supported=1 rate_hz=10`
  - `EVT:STREAM seq=<n> ts_ms=<deviceMs> valid=1 ma12_ready=<0|1> distance_mm=<mm> weight=<kg> ma12=<kg>`
- Stable weight:
  - `EVT:STABLE:<weight>`
- Calibration:
  - `EVT:PARAM:<zero>,<factor>`
- State/Fault:
  - `EVT:STATE ...`
  - `EVT:FAULT ...`
  - `EVT:SAFETY reason=<reason> code=<code> effect=<effect> state=<state> wave=<wave>`

## Raw Console Debugging
- Open `设备原始日志` panel on the main screen.
- Prefix semantics:
  - `[RX-RAW]` notify raw chunk（传输层回调原始分片）
  - `[RX]` delimiter 重组后的完整协议行
  - `[TX]` app 发出的命令行
  - `[SYS]` 连接/MTU/诊断信息
- Use `复制` to export all current lines and `清空` to reset.
- Typical notify/stream verification path:
  1. After connection, check `TX CAP?`.
  2. Confirm `RX ACK:CAP ... platform_model=... laser_installed=...`.
  3. Confirm `TX SNAPSHOT?` follows and `RX SNAPSHOT: ...` returns runtime truth.
  4. If `ACK:CAP` contains `stream_control_supported=1`, confirm `TX STREAM:SET enabled=1,rate_hz=10` and `RX ACK:STREAM enabled=1 ...`.
  5. Confirm subsequent `RX-RAW` chunks and `RX` complete lines continue normally.
  6. Confirm stream lines (`EVT:STREAM`) appear continuously when measurement plane is active.

## Init Contract
- `ACK:CAP` is bootstrap truth only. It identifies the device.
- `SNAPSHOT` is runtime truth. The connect-time path is intentionally trimmed to
  the start-gate subset so it remains single-frame on the common MTU path.
- Do not keep adding runtime or peripheral-health fields into `ACK:CAP`.
- Connection bootstrap order is fixed:
  1. connect
  2. enable notify
  3. `CAP?`
  4. `STREAM:SET enabled=1,rate_hz=10` when the firmware advertises `stream_control_supported=1`
  5. `SNAPSHOT?`
- For the refactored profile protocol, a SET ACK is not the final configuration truth. After `DEVICE:SET_CONFIG`, confirm the matching `DEVICE:GET_CONFIG` result first, then refresh `CAP?` / `SNAPSHOT?` and restore `STREAM` on the same BLE connection. Legacy firmware has a separate compatibility boundary; see below.

## Model Settings And Start Readiness Contract

用户操作顺序：

1. 连接后，APP 读取当前连接的设备配置和 `SNAPSHOT`。选择 Base/Plus 或激光型号只改变待设置值，不写 ESP32。
2. 点击“设置”后，APP 写入配置并等待设备回读确认。目标合同是：确认 Base 时隐藏 Plus 的 3 张卡片；确认 Plus 时显示激光型号、空载距离、保护开关。已选型号与设备已确认型号相同时，型号设置按钮不可用。
3. Plus 的激光型号单独设置并回读确认后，才能按有效距离设置空载距离；空载距离也需要独立回读。保护开关不以空载距离设置动作作为操作前提，两个开关分别设置。
4. 激光未设置或异常：保护开关灰色不可点。连接到故障 Plus 或 Base 设置为故障 Plus 后，显示一次激光故障通知；只有“知道了”，关闭通知不发送授权、不改变 ready。Plus 设置为 Base 后不应沿用旧故障通知。
5. ESP32 独立判断 `start_ready`，Base 和 Plus 均使用同一正式字段。APP 不根据型号、激光状态、`baseline_ready` 或 `degraded_start_*` 自行推算就绪，也不发送 `DEGRADED_START_SET`。
6. ESP32 的目标规则：Base 配置完成后可开始；Plus 未设置激光或激光故障时自动降级；激光正常且两个保护都关闭时不要求站稳，任意保护开启时必须站稳。硬阻断、断连和配置切换不能被降级绕过。

APP 的断连、开始/停止命令发送中、配置刷新中和非法输入仍可禁用操作；这些是 APP 操作状态，不是修改 ESP32 的 `start_ready`。本轮不修改 SW APP，也不经过服务器。

**已知源码差异与证据边界（2026-10-25，审计基线 `b2ce08e`）：**

- 当前 SET 等待阶段把 `deviceConfigVerified` 清为 false，3 张卡片会暂时隐藏；不能宣称整个写入等待过程都保持原卡片。目标合同未因此改动，详见[统一蓝图第 15 节](../../docs/system/esp32_demo_start_ready_independent_judgment施工蓝图.md#15-2026-10-25-复盘状态与源码差异登记)。
- CYD 可以读距离和设置空载距离，但当前固件尚未开放其健康激光、保护开启时的启动策略。不能把 J100 的保护验收扩展到 CYD。
- 代码保留 legacy 设置分支，不等于旧固件全链路兼容已验证；分支前置的身份、generation、配置确认条件仍需专项证据。
- 历史用户反馈和业务观测有通过记录，但采集完整性为 `INCOMPLETE`；不代表当前 1.1.3 包或所有传感器场景已完整验收。本次文档复盘未重新运行 APP、烧录或真机测试。

详细规则见 [Start Readiness Contract](../../docs/start-readiness-contract.md)。

## Main Screen Notes
- Connection status is always one of `Disconnected`, `Scanning`, `Connecting`, `Connected`.
- Status area also shows `Notify` enable state and capability probe result.
- System status now shows engineering-facing safety visibility for:
  - safety reason
  - effect
  - runtime state
  - wave state
  - transport-derived BLE disconnect reminder when `EVT:SAFETY` is not the first observable signal
- Scale area shows live `distance`, `weight`, `stable weight`, and calibration state.
- Wave area highlights latest ACK/NACK/ERR with friendly Chinese hints for:
  - `NOT_ARMED`
  - `FAULT_LOCKED`
  - `UNSUPPORTED`
