# ESP32 PLUS Laser Simulator Firmware

Date: 2026-05-13

## Purpose

`esp32_plus_laser_sim` is a standalone PlatformIO environment for a spare ESP32 module. It simulates an ESP32 PLUS device with controllable protection states for Android APP regression.

It is a development and Android APP regression tool. It is not the production firmware and does not validate the real MAX485, laser sensor, or PCM5102A chain.

User-facing serial monitor commands and prompts are Chinese-first for bench operation. BLE protocol frames remain English-code based because the Android APP parser depends on the formal protocol contract.

## Scope

Truth source:

- Existing SonicWave BLE UART UUIDs
- Existing `CAP?`, `SNAPSHOT?`, `WAVE:*`, `DEBUG:DEGRADED_START`, `SAFETY:LEAVE_PROTECTION`, and `DEBUG:FALL_STOP` command families
- Serial-only `SIM:*` bench commands for manual state injection
- Chinese serial bench commands for manual operation, mapped to the same simulator state changes

Out of scope:

- Real Modbus / MAX485 reads
- Real laser distance values
- Real I2S output
- Motion safety detector validation

## Wiring

The simulator uses `GPIO4` as the laser-state input:

```text
GPIO4 open / pull-up -> laser READY
GPIO4 connected GND  -> laser FAULT / no signal
```

If `GPIO4` is needed later for another bench use, update `kLaserStatePin` in `test_firmware/esp32_plus_laser_sim/main.cpp`. The production firmware does not depend on this simulator pin.

## Build And Upload

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
python3 -m platformio run -e esp32_plus_laser_sim
python3 -m platformio run -e esp32_plus_laser_sim -t upload
python3 -m platformio device monitor -e esp32_plus_laser_sim -b 115200
```

If upload waits for download mode, hold `BOOT`, tap `RESET` or reconnect USB, then release `BOOT` after upload starts.

## Expected App Semantics

Healthy state:

```text
SNAPSHOT:top_state=IDLE start_ready=1 laser_available=1 measurement_health=READY degraded_start_available=0 leave_stop_enabled=1
```

Fault state:

```text
SNAPSHOT:top_state=IDLE start_ready=0 laser_available=0 measurement_health=FAULT degraded_start_available=1 leave_stop_enabled=1
EVT:SAFETY reason=MEASUREMENT_UNAVAILABLE code=0 effect=WARNING_ONLY state=IDLE
```

After degraded-start confirmation:

```text
ACK:DEGRADED_START enabled=1 available=1
SNAPSHOT:top_state=IDLE start_ready=1 laser_available=0 measurement_health=FAULT degraded_start_available=1 degraded_start_enabled=1 leave_stop_enabled=1
```

## Regression Boundary

This firmware can support Android APP checks for:

- BLE discovery and connection
- top-bar ESP32 route visibility
- laser fault reminder
- degraded-start confirmation
- start/stop button state under simulated READY / FAULT

It cannot replace real-device validation for:

- MAX485 electrical reliability
- laser sensor readings
- PCM5102A output
- real safety detector behavior

## Simulator Controls

The simulator supports two control paths:

1. Physical GPIO for laser health.
2. Serial monitor commands for APP state-chain regression.

Serial commands are local bench controls. They are not production BLE protocol.

### Serial Commands

Open monitor:

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
python3 -m platformio device monitor -e esp32_plus_laser_sim -b 115200
```

Then type one command and press Enter:

Chinese commands are preferred for manual testing:

```text
帮助
状态
激光正常
激光故障
站稳
未站稳
离开
摔倒
清除
快照
```

The older English simulator commands remain supported:

```text
SIM:STATUS
SIM:LASER ready
SIM:LASER fault
SIM:STAND 1
SIM:STAND 0
SIM:LEFT
SIM:FALL
SIM:CLEAR
SIM:SNAPSHOT
```

Meaning:

- `SIM:LASER ready` / `SIM:LASER fault`
  - Chinese equivalent: `激光正常` / `激光故障`
  - Simulates protection module READY / FAULT.
  - Equivalent to GPIO4 open / GPIO4 to GND.
- `SIM:STAND 1`
  - Chinese equivalent: `站稳`
  - Simulates user standing stable on platform.
  - Emits `EVT:BASELINE start_ready=1 baseline_ready=1 stable_weight=60.00` and snapshot.
- `SIM:STAND 0`
  - Chinese equivalent: `未站稳`
  - Simulates user not standing stable.
  - Emits `EVT:BASELINE start_ready=0 baseline_ready=0 stable_weight=0.00` and snapshot.
- `SIM:LEFT`
  - Chinese equivalent: `离开` / `律动离开`
  - Simulates `USER_LEFT_PLATFORM`.
  - If leave protection is enabled and wave is running, emits recoverable pause frames:
    - `EVT:WAVE_OUTPUT active=0`
    - `EVT:STOP stop_reason=USER_LEFT_PLATFORM ... effect=RECOVERABLE_PAUSE`
    - `EVT:FAULT 100 reason=USER_LEFT_PLATFORM`
    - `EVT:SAFETY reason=USER_LEFT_PLATFORM ... effect=RECOVERABLE_PAUSE`
  - If leave protection is disabled, emits warning-only safety frame.
- `SIM:FALL`
  - Chinese equivalent: `摔倒` / `摔倒异常`
  - Simulates `FALL_SUSPECTED`.
  - If fall stop is enabled and wave is running, emits abnormal stop frames.
  - If fall stop is disabled, emits warning-only safety frame.
- `SIM:CLEAR`
  - Chinese equivalent: `清除`
  - Emits clear frames:
    - `EVT:FAULT 0`
    - `EVT:SAFETY reason=NONE effect=NONE`
- `SIM:SNAPSHOT`
  - Chinese equivalent: `快照`
  - Emits baseline, state, and snapshot with current simulator state.

## BLE Connect-Disconnect Note

If the APP connects to an older simulator build and then immediately disconnects, first update and reflash this simulator firmware. The simulator must notify BLE lines with a trailing newline, split long notify payloads by the negotiated UART-safe payload budget, and should not push a snapshot inside `onConnect` before Android has enabled notifications.

The formal Android APP also performs `CAP? -> SNAPSHOT?` protocol confirmation immediately after BLE transport connects. The simulator therefore keeps `CAP?` scoped to `ACK:CAP` only; runtime truth is returned by an explicit `SNAPSHOT?`.

Since `SW-HUB-LASER-SIM-1.0.3`, BLE RX/TX work is queued out of BLE callbacks:

- RX callback only records the command.
- The main loop handles commands.
- TX notify is sent from the main loop.
- The simulator no longer sends periodic snapshots immediately after connection; formal APP should request `SNAPSHOT?`.

This mirrors the production firmware pattern more closely and avoids notifying from inside the write callback.

This is a simulator-specific compatibility issue. The production firmware uses `BleTransport`, which frames outgoing BLE lines with `\n` and fragments long payloads according to MTU, so the old simulator connect-time disconnect does not by itself indicate a production firmware regression.

## APP Regression Scenarios

### Settings Switches

1. Connect APP to `SonicWave_SIM_PLUS`.
2. Open top-bar settings.
3. `摔倒保护` and `律动离开` should be visible and clickable.
4. Toggle `律动离开`; simulator replies with `ACK:LEAVE_PROTECTION`.
5. Toggle `摔倒保护`; simulator replies with `ACK:FALL_STOP`.

### Laser Fault / Recovery

1. Ensure simulator is healthy:

   ```text
   SIM:LASER ready
   SIM:STAND 1
   ```

2. In APP, confirm settings gear is available.
3. Trigger fault:

   ```text
   SIM:LASER fault
   ```

4. Expected APP behavior:
   - Shows one `律动保护失效`.
   - Settings gear becomes unavailable.
   - Start button becomes available through degraded-start path.

5. Recover:

   ```text
   SIM:LASER ready
   SIM:STAND 1
   ```

6. Expected APP behavior:
   - Gear recovers.
   - Start button returns to normal PLUS logic.
   - No old fault dialog is replayed.

### Leave Platform

1. Prepare:

   ```text
   SIM:LASER ready
   SIM:STAND 1
   ```

2. Start playback in APP.
3. Trigger leave:

   ```text
   SIM:LEFT
   ```

4. Expected APP behavior:
   - Shows leave-platform recoverable pause dialog.
   - Does not show `律动保护失效`.
   - Does not disable settings gear.

5. Simulate standing stable again:

   ```text
   SIM:STAND 1
   ```

6. Expected APP behavior:
   - Leave dialog auto-dismisses after baseline recovery.

### Fall Suspected

1. Prepare:

   ```text
   SIM:LASER ready
   SIM:STAND 1
   ```

2. Start playback in APP.
3. Trigger fall:

   ```text
   SIM:FALL
   ```

4. Expected APP behavior:
   - Shows abnormal stop / fall protection dialog.
   - Does not change `律动离开` switch state.
   - Does not show `律动保护失效`.
