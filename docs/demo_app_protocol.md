## SonicWave Demo APP 协议（与固件对齐）

本文件以 `src/` 当前实现为准，作为 Android demo 联调协议说明。

当前交付边界说明：

- 本文描述的是当前 Demo APP 仍然兼容的主协议面。
- 当前统一固件同时覆盖 `BASE`、`PLUS`、J100(P)-485 和 CYD3S1612-IIC 测量链；真机验收状态见统一施工蓝图第 12 节。
- `SW APP` 继续只消费正式状态，不消费 Demo 的距离/体重校准字段。

### 1. BLE 基本信息

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic（APP -> ESP32）: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`（Write）
- TX Characteristic（ESP32 -> APP）: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`（Notify）
- 设备名: `SonicWave_Hub`

### 2. 主协议（Primary）

#### 2.1 能力探测
- 请求: `CAP?`
- 预期回包:
  - `ACK:CAP fw=SW-HUB-1.0.0 proto=2 platform_model=<BASE|PLUS|PRO|ULTRA> laser_installed=<0|1> ... cue_supported=<0|1>`

#### 2.2 设备配置
- 写入设备画像:
  - `DEVICE:SET_CONFIG platform_model=<BASE|PLUS|PRO|ULTRA>,laser_installed=<0|1>`
- 一致性规则:
  - `BASE` 必须搭配 `laser_installed=0`
  - `PLUS/PRO/ULTRA` 当前要求 `laser_installed=1`
- 固件成功回包:
  - `ACK:DEVICE_CONFIG platform_model=<...> laser_installed=<0|1>`

#### 2.3 运行时快照
- 请求: `SNAPSHOT?`
- 预期回包:
  - `SNAPSHOT: top_state=... runtime_ready=... start_ready=... baseline_ready=... platform_model=... laser_installed=... laser_available=... degraded_start_available=... degraded_start_enabled=...`

控制确认约定：
- `ACK:OK` 只表示 command accepted，不表示启动/停止已经成功。
- `top_state=RUNNING` 只表示状态机进入运行态，不等价于物理输出已生效。
- `wave_output_active=1` 才是 Demo APP 判断“波形输出已真正生效”的正式 truth。

启动按钮约定：
- `BASE`：连接后即可开始。
- `PLUS` 且 `laser_installed=0`：不属于当前正式产品定义，不应作为正常交付路径。
- `PLUS` 且 `laser_installed=1`：以 firmware `start_ready` / `degraded_start_available` 为准。

#### 2.4 波形控制
- 设置参数: `WAVE:SET f=<freq>,i=<intensity>`
  - 兼容输入键：`freq` 和 `amp`
- 启动: `WAVE:START`
- 停止: `WAVE:STOP`

#### 2.5 连接提示音
- APP 在当前连接完成 CAP/SNAPSHOT 协议确认且 `cue_supported=1` 时发送一次 `AUDIO:CUE id=CONNECT`。
- 断开提示音由 ESP32 在真实 BLE 断开后本地播放；APP 不发送 `DISCONNECT` 命令。
- 提示音失败不影响连接、WAVE、停止和 Plus 运行状态判断。

#### 2.5 测重与校准
- 归零: `SCALE:ZERO`
- 校准: `SCALE:CAL z=<zeroDistance>,k=<scaleFactor>`
- 参数归零/清模: `CAL:ZERO`
- 记录校准点: `CAL:CAPTURE w=<referenceWeightKg>`
- 读取模型: `CAL:GET_MODEL`
- 写入模型: `CAL:SET_MODEL type=<LINEAR|QUADRATIC>,ref=<runtimeDistance>,c0=<...>,c1=<...>,c2=<...>`

说明：

- `CAL:ZERO` 只清空 ESP32 已保存模型、正式空载引用和运行时状态，不采样当前距离；空载 0kg 定点必须随后单独用 `CAL:CAPTURE w=0` 记录。
- `CAL:CAPTURE w=0` 在激光后端只要求距离稳定；非零重量还要求有人基线稳定。
- `CAL:SET_MODEL` 成功后 Demo APP 必须继续发送 `CAL:GET_MODEL`，收到 `ACK:CAL_MODEL ... ready=1` 且参数逐项一致后，才显示模型已经部署。

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
- formal measurement carrier:
  - valid sample:
    - `EVT:STREAM seq=<n> ts_ms=<deviceMs> valid=<0|1> distance_valid=1 weight_valid=<0|1> ma12_ready=<0|1> distance_mm=<physicalMillimetres> [weight=<weightKg>] [ma12=<ma12Kg>] backend=<...>`
  - invalid sample:
    - `EVT:STREAM seq=<n> ts_ms=<deviceMs> valid=0 distance_valid=0 weight_valid=0 ma12_ready=0 reason=<READ_FAIL|DISTANCE_OUT_OF_RANGE|...> backend=<...>`
- `SNAPSHOT: ... platform_model=... laser_installed=... laser_available=... degraded_start_available=... degraded_start_enabled=...`

说明：

- `EVT:STREAM` 是 measurement plane 的唯一正式 continuous carrier。
- `EVT:STREAM distance` 保留旧运行时单位以兼容既有协议；`distance_mm` 是 ESP32 输出的统一物理毫米字段。Demo APP 只读取并记录 `distance_mm`，不按传感器类型自行换算；没有该字段时，样本视为不完整。
- `distance_valid`、`weight_valid` 和 `distance_reason`/`weight_reason` 分别表达两条测量链；距离有效但体重未校准时，Demo 仍显示和保存距离，不把缺失体重写成 0 或沿用上一帧。
- `distance / weight / MA12 / valid / reason / seq` 归属于同一 plane、同一 sample 序列。
- `STABLE` 仍只是离散事件，不等于连续 plane。
- Demo APP 在 `PRIMARY/UNKNOWN` 模式下只消费 formal `EVT:STREAM` carrier。
- 当前 `BASE / PLUS degraded-start` 子集不依赖 `EVT:STREAM` 作为 start-path 成立条件。
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
  - 对 `BASE` 与 `PLUS degraded-start` 当前交付子集，测量图表和校准区域可以在主界面让位于 delivery-boundary 提示。
- Layer C: raw console / developer log
  - raw log 使用 capped buffer + throttled publish。
  - verbose stream log 仍可手动开启，但不再默认压垮主 UI。

### 5. Primary / Legacy 判定策略

- Primary: 在超时窗口内收到 `ACK:CAP ...`
- Unknown(协议不匹配): 收到 `NACK/ERR/其他无法识别响应`
- Legacy: 超时窗口内完全无响应（无任何 notify）

### 6. 当前子集最小联调流程（建议）

1. 连接并订阅 notify。
2. `CAP?`（确认 primary）。
3. （可选）`DEVICE:SET_CONFIG platform_model=BASE,laser_installed=0` 或 `platform_model=PLUS,laser_installed=1`。
4. `SNAPSHOT?`（确认 `platform_model` / `laser_installed` / `degraded_start_available` / `start_ready` 相关 truth）。
5. `WAVE:SET f=20,i=80`。
6. `WAVE:START`。
7. `WAVE:STOP`。

断连时固件会安全停机；断连瞬间可能收不到完整 `EVT`，以固件停机行为为准。

### 7. 当前子集下不应作为阻塞项的现象

- `laser_available=0`
- 无持续有效 `EVT:STREAM`
- 图表区没有 measurement samples
- 无法完成真实校准闭环

这些现象在当前 bench 和当前交付边界下不应单独构成 `BASE / PLUS degraded-start` 子集阻塞。
