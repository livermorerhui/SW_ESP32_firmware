# SonicWave Android Demo

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
6. After connected, app auto-enables TX notify (CCCD), requests MTU (target 185), probes `CAP?`, then refreshes `SNAPSHOT?`.
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
  - `EVT:STREAM:<dist>,<weight>`
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
  4. Confirm subsequent `RX-RAW` chunks and `RX` complete lines continue normally.
  5. Confirm stream lines (`EVT:STREAM`) appear continuously when measurement plane is active.

## Init Contract
- `ACK:CAP` is bootstrap truth only. It identifies the device.
- `SNAPSHOT` is runtime truth. The connect-time path is intentionally trimmed to
  the start-gate subset so it remains single-frame on the common MTU path.
- Do not keep adding runtime or peripheral-health fields into `ACK:CAP`.
- Connection bootstrap order is fixed:
  1. connect
  2. enable notify
  3. `CAP?`
  4. `SNAPSHOT?`
- After every successful `DEVICE:SET_CONFIG`, the app should refresh both:
  1. `CAP?`
  2. `SNAPSHOT?`

## Start Readiness Contract
- `BASE` is treated as a laserless start profile. After connect/reconnect,
  `Start` should be ready without waiting for stable-weight runtime truth.
- `PLUS` with `laser_installed=0` is also treated as a laserless start profile.
  The app should show an informational prompt, but `Start` should remain
  available.
- `PLUS` with `laser_installed=1` relies on firmware runtime truth:
  `start_ready` for the healthy path, `degraded_start_available` plus
  confirmation for the degraded path.

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
