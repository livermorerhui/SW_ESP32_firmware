## SonicWave Demo APP 协议（与固件对齐）

本文件以 `src/` 当前实现为准，作为 Android demo 联调协议说明。

### 1. BLE 基本信息

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic（APP -> ESP32）: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`（Write）
- TX Characteristic（ESP32 -> APP）: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`（Notify）
- 设备名: `SonicWave_Hub`

### 2. 主协议（Primary）

#### 2.1 能力探测
- 请求: `CAP?`
- 预期回包:
  - `ACK:CAP fw=SW-HUB-1.0.0 proto=2 platform_model=<BASE|PLUS|PRO|ULTRA> laser_installed=<0|1> ...`

#### 2.2 设备配置
- 写入设备画像:
  - `DEVICE:SET_CONFIG platform_model=<BASE|PLUS|PRO|ULTRA>,laser_installed=<0|1>`
- 一致性规则:
  - `BASE` 必须搭配 `laser_installed=0`
  - `PLUS/PRO/ULTRA` 必须搭配 `laser_installed=1`
- 固件成功回包:
  - `ACK:DEVICE_CONFIG platform_model=<...> laser_installed=<0|1>`

#### 2.3 运行时快照
- 请求: `SNAPSHOT?`
- 预期回包:
  - `SNAPSHOT: top_state=... user_present=... runtime_ready=... start_ready=... baseline_ready=... wave_output_active=... current_reason_code=... current_safety_effect=... stable_weight=... current_frequency=... current_intensity=... platform_model=... laser_installed=... laser_available=... protection_degraded=...`

控制确认约定：
- `ACK:OK` 只表示 command accepted，不表示启动/停止已经成功。
- `top_state=RUNNING` 只表示状态机进入运行态，不等价于物理输出已生效。
- `wave_output_active=1` 才是 Demo APP 判断“波形输出已真正生效”的正式 truth。

#### 2.4 波形控制
- 设置参数: `WAVE:SET f=<freq>,i=<intensity>`
  - 兼容输入键：`freq` 和 `amp`
- 启动: `WAVE:START`
- 停止: `WAVE:STOP`

#### 2.5 测重与校准
- 归零: `SCALE:ZERO`
- 校准: `SCALE:CAL z=<zeroDistance>,k=<scaleFactor>`

### 3. Legacy 兼容协议

当 `CAP?` 无响应时，可降级 legacy：

- 波形：`F:<freq>,I:<intensity>`（字段可部分出现）
- 启停：`E:1`（start） / `E:0`（stop）
- 归零：`ZERO`
- 标定：`SET_PS:<zeroDistance>,<scaleFactor>`

说明：若 `CAP?` 收到 `NACK/ERR/不可识别响应`，说明链路可用但协议不匹配，不应静默降级。

### 4. 固件回包与事件

#### 4.1 ACK / NACK / ERR
- `ACK:OK`
- `ACK:CAP fw=... proto=... platform_model=... laser_installed=...`
- `ACK:DEVICE_CONFIG platform_model=... laser_installed=...`
- `NACK:<reason>`（例如 `NACK:INVALID_PARAM` / `NACK:NOT_ARMED` / `NACK:FAULT_LOCKED` / `NACK:UNKNOWN_CMD`）

#### 4.2 事件（固件输出）
- `EVT:STATE IDLE|ARMED|RUNNING|FAULT_STOP`
- `EVT:WAVE_OUTPUT active=<0|1>`
- `EVT:FAULT <code>`（例如 `100` 表示 USER_OFF）
- `EVT:STABLE:<weightKg>`
- `EVT:PARAM:<zeroDistance>,<scaleFactor>`
- `EVT:BASELINE start_ready=<0|1> baseline_ready=<0|1> stable_weight_active=<0|1> stable_weight=<kg> ma7=<kg> deviation=<kg> ratio=<r> main_state=<...> abnormal_duration_ms=<ms> danger_duration_ms=<ms> stop_reason=<...> stop_source=<...>`
- formal measurement carrier:
  - valid sample:
    - `EVT:STREAM seq=<n> ts_ms=<deviceMs> valid=1 ma12_ready=<0|1> distance=<distance> weight=<weightKg> [ma12=<ma12Kg>]`
  - invalid sample:
    - `EVT:STREAM seq=<n> ts_ms=<deviceMs> valid=0 ma12_ready=0 reason=<READ_FAIL|OUT_OF_RANGE_LOW|...>`
- `SNAPSHOT: ... platform_model=... laser_installed=... laser_available=... protection_degraded=...`

说明：

- `EVT:STREAM` 是 measurement plane 的唯一正式 continuous carrier。
- `distance / weight / MA12 / valid / reason / seq` 归属于同一 plane、同一 sample 序列。
- `STABLE` 仍只是离散事件，不等于连续 plane。
- `baseline_ready` 表示本轮 occupied cycle 的 baseline 已建立并保留。
- `stable_weight_active` 表示当前 live stable 窗口仍处于激活态；它可以在 `baseline_ready=1` 时短暂为 `0`。
- `start_ready` 仍是 formal pre-start ready truth，不应由 APP 从 `STABLE/ARMED/baseline_ready` 本地重建。
- Demo APP 在 `PRIMARY/UNKNOWN` 模式下只消费 formal `EVT:STREAM` carrier。
- Demo 仍兼容解析裸 CSV：`<distance>,<weightKg>`，但只作为 `LEGACY` mode fallback，不再视为 formal carrier，也不会在 primary consume 路径中继续驱动 UI / session truth。
- `EVT:WAVE_OUTPUT` 与 `SNAPSHOT.wave_output_active` 共同构成 control confirmation plane：
  - start success: `wave_output_active=1`
  - stop success: `wave_output_active=0`
  - reconnect 恢复以最新 `SNAPSHOT` 为准

### 4.3 Demo APP consume layering

- Layer A: background full-truth ingest
  - formal `EVT:STREAM` 全量进入 Demo ingest。
  - recorder、test session、motion sampling 继续保留全量样本能力。
- Layer B: display snapshot / throttled chart stream
  - 前台 distance / weight / ma12 / chart 改为节流后的 display snapshot。
  - `MainScreen` 不再被每个 sample 的 `uiState` 更新拖动。
- Layer C: raw console / developer log
  - raw log 使用 capped buffer + throttled publish。
  - verbose stream log 仍可手动开启，但不再默认压垮主 UI。

### 5. Primary / Legacy 判定策略

- Primary: 在超时窗口内收到 `ACK:CAP ...`
- Unknown(协议不匹配): 收到 `NACK/ERR/其他无法识别响应`
- Legacy: 超时窗口内完全无响应（无任何 notify）

### 6. 最小联调流程（建议）

1. 连接并订阅 notify。
2. `CAP?`（确认 primary）。
3. （可选）`DEVICE:SET_CONFIG platform_model=PLUS,laser_installed=1`。
4. `SNAPSHOT?`（确认 `laser_available` / `protection_degraded`）。
5. `WAVE:SET f=20,i=80`。
6. `WAVE:START`。
7. `WAVE:STOP`。
8. `SCALE:ZERO`。
9. 观察 `EVT:STREAM` 连续样本，确认 `seq / distance / weight / ma12` 正常增长。

断连时固件会安全停机；断连瞬间可能收不到完整 `EVT`，以固件停机行为为准。
