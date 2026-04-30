# ESP32-plus Degraded Measurement Runtime Policy + Wave Output Startup Observability

日期：2026-04-30

## 1. 背景

SW APP 真机 smoke `esp32_plus_degraded_start_smoke` 已证明 degraded-start 合同链路可用：

- `SNAPSHOT` 正确暴露 `degraded_start_available=1`。
- APP 确认后固件返回 degraded-start ACK。
- `WAVE:START` 进入 `START ALLOW`。
- `WaveModule::start`、`i2s_start=0`、`ramp complete` 均可见。

但同轮用户反馈首次起震弱、断断续续，随后恢复。ESP32 串口同时出现：

- `MEASUREMENT_TRANSIENT_COUNT=12`
- `MODBUS_READ_FAIL_COUNT=12`
- RUNNING 阶段持续 `0xE2 read_ms=2009`

因此本包目标不是改启动策略，而是先补只读诊断，区分以下几类可能：

- 正常 `800ms` startup ramp 的体感。
- I2S start / first emit / ramp complete 时序异常。
- `i2s_write` 错误、短写或慢写导致输出不连续。
- degraded measurement 故障链路在 RUNNING 阶段连续 2 秒 timeout，对输出任务造成间接影响。

## 2. 边界

本包只改内部串口诊断：

- 不改 BLE 线格式。
- 不改 `CAP? / SNAPSHOT? / WAVE:* / EVT:* / ACK:* / NACK:*`。
- 不改 `SystemStateMachine` start/stop action timing。
- 不改 `WaveModule` ramp 参数。
- 不改 `LaserModule` measurement 读取间隔或 backoff 策略。
- 不把串口诊断升级为 APP 正式 truth source。

## 3. 实现

文件：

- `src/modules/wave/WaveModule.h`
- `src/modules/wave/WaveModule.cpp`

新增只读诊断：

### 3.1 `WAVE_OUTPUT_STARTUP`

新增 startup 序列号和 start request 时间戳，用于串起一次输出启动：

- `event=request`
  - 记录 `setEnable(true)` 到 audio task 看到请求的耗时。
- `event=i2s_start`
  - 记录 start request 到 `i2s_start` 的耗时。
  - 记录 `i2s_zero_dma_buffer` 与 `i2s_start` 返回值。
- `event=first_emit`
  - 记录 start request 到首次非零样本写入的耗时。
  - 记录 i2s start 到首次非零样本写入的耗时。
  - 记录当前 amplitude / phase_hz / write_ms。
- `event=ramp_complete`
  - 记录 start request 到 ramp complete 的耗时。
  - 记录 first emit 到 ramp complete 的耗时。
- `event=cancel`
  - 如果启动窗口未完成就被 stop，记录是否已经 first emit / ramp complete。

### 3.2 `WAVE_OUTPUT_WRITE`

新增 I2S 写入异常诊断：

- `event=error`
  - `i2s_write` 返回非 `ESP_OK`。
- `event=short`
  - 写入字节数小于期望长度。
- `event=slow`
  - 正常 emit 写入耗时超过 `25ms`。

这些日志只在异常或关键边沿出现，不会在正常输出循环里每帧刷屏。

## 4. 自动化验证

已执行：

```bash
git diff --check
python3 -m platformio run -e esp32s3
```

结果：

- `git diff --check` 通过。
- `python3 -m platformio run -e esp32s3` 通过。
- 构建目标：`sonicwave_esp32s3_n16r8`。

## 5. 后续真机复核看点

下一轮真机 smoke 需要重新烧录 ESP32 固件，因为本包改了固件串口诊断。

重点不是再证明 degraded-start 合同，而是看首次输出异常时是否有证据：

- `WAVE_OUTPUT_STARTUP event=request`
- `WAVE_OUTPUT_STARTUP event=i2s_start`
- `WAVE_OUTPUT_STARTUP event=first_emit`
- `WAVE_OUTPUT_STARTUP event=ramp_complete`
- 是否存在 `WAVE_OUTPUT_WRITE event=error/short/slow`
- `MEASUREMENT_TRANSIENT` 是否仍在 RUNNING 阶段持续 `0xE2 read_ms=2009`

判断规则：

- 如果 startup 时间线正常、无 write error/short/slow，而用户仍觉得前 800ms 弱，优先判断为正常 ramp 体感，需要讨论产品体验是否要调 ramp。
- 如果 startup 时间线正常，但 RUNNING 阶段持续 2 秒 measurement timeout 与断续体感重合，下一包再审 degraded measurement circuit-breaker / low-frequency probe。
- 如果出现 `WAVE_OUTPUT_WRITE error/short/slow`，优先审 I2S/DMA/任务调度/电源输出侧。

## 6. 真机 smoke 复核

capture：

`20260430_211325__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_fault__esp32_plus_fault__manual__ESP32_RUNTIME__esp32_plus_degraded_wave_output_observability`

用户体感：

- 使用体感正常。

APP / BLE 证据：

- `CONNECT_SUCCESS=1`
- `CONNECT_SNAPSHOT_REFRESH_FAILED=0`
- `DEVICE_SNAPSHOT_SYNCED=8`
- `DEVICE_WAVE_OUTPUT_CONFIRMATION active=true`
- `start_confirmed_by_device`
- `DEVICE_WAVE_OUTPUT_CONFIRMATION active=false`
- `stop_confirmed_by_device`
- `DEVICE_EVENT_SAFETY=0`
- `DEVICE_EVENT_FAULT=0`
- `warnings.log` 为空

ESP32 输出侧证据：

- `WAVE:START=1`
- `START_ALLOW=1`
- `WAVE_OUTPUT_STARTUP_COUNT=4`
- `event=request req_to_loop_ms=6`
- `event=i2s_start req_to_i2s_ms=21 zero_dma=0 start=0`
- `event=first_emit req_to_emit_ms=41 i2s_to_emit_ms=20 amplitude=0.025 phase_hz=20.00 write_ms=0`
- `event=ramp_complete req_to_complete_ms=804 emit_to_complete_ms=763 target_amp=1.000 phase_hz=20.00`
- `WAVE_OUTPUT_WRITE_COUNT=0`
- `WAVE_OUTPUT_WRITE_ERROR_COUNT=0`
- `WAVE_OUTPUT_WRITE_SHORT_COUNT=0`
- `WAVE_OUTPUT_WRITE_SLOW_COUNT=0`
- `WAVE:STOP=1`
- `GURU=0 / PANIC=0 / BROWNOUT=0`

测量链观察项：

- `MEASUREMENT_TRANSIENT_COUNT=9`
- `MODBUS_READ_FAIL_COUNT=9`
- RUNNING 阶段仍可见 `0xE2 read_ms=2009`。

判断：

- 本包目标是解释首次输出异常是否来自输出启动链或 I2S 写入异常。真机证据显示输出启动时间线完整，I2S start 成功，首次非零输出和 ramp complete 正常，无 write error / short / slow。
- 本轮体感正常，因此 `ESP32-plus degraded wave output startup observability` 可判定通过。
- `0xE2` 连续读失败仍是 PLUS 故障形态的 measurement unavailable 证据；它没有在本轮造成体感异常，但后续如要优化 degraded 运行期效率，可单独做 `degraded measurement circuit-breaker / low-frequency probe` 审计包。
