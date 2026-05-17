# ESP32 Measurement Availability Probe Policy

日期：2026-05-17

## 1. 背景

本包承接 `task_20260430_degraded_measurement_circuit_breaker_audit.md` 的审计结论。

目标是在 `PLUS + laser_installed=1 + measurement unavailable` 且 measurement fault 已确认后，避免 RUNNING 阶段持续进行无意义的 2 秒级 Modbus timeout 探测。

本包只优化 ESP32 内部 measurement probe 策略，不改变跨端合同。

## 2. 边界

保持不变：

- 不改 `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*`。
- 不改 Android APP 消费逻辑。
- 不改 `SystemStateMachine` start / stop / safety action timing。
- 不改 degraded-start 授权语义。
- 不改 `WaveModule` ramp / I2S 输出策略。
- `MEASUREMENT_UNAVAILABLE` 仍默认 `WARNING_ONLY`。

本包新增的 `MEASUREMENT_PROBE` 只作为 ESP32 串口 evidence，不进入 BLE 正式合同。

## 3. 实现

新增文件：

- `src/modules/laser/MeasurementAvailabilityProbePolicy.h`
- `src/modules/laser/MeasurementAvailabilityProbePolicy.cpp`

修改文件：

- `src/modules/laser/LaserModule.h`
- `src/modules/laser/LaserModule.cpp`
- `tools/run_evaluator_unit_tests.py`

### 3.1 纯策略 owner

新增 `MeasurementAvailabilityProbePolicy`，只负责内部探测策略：

- 状态：
  - `CLOSED`
  - `OPEN_UNAVAILABLE`
- 默认参数：
  - `timeoutCode = 0xE2`
  - `openAfterConsecutiveTimeouts = 2`
  - `probeIntervalMs = 5000`
  - `skipLogIntervalMs = 5000`

启用条件：

- `laserInstalled == true`
- `measurementFaultConfirmed == true`

非启用条件下保持 `CLOSED`，并清空连续 timeout 状态。

### 3.2 LaserModule 接入

`LaserModule::taskLoop()` read 前调用 policy：

- `CLOSED`：保持现有 20ms 健康读取节奏。
- `OPEN_UNAVAILABLE` 且 probe 未到期：跳过真实 Modbus read。
- probe 到期：执行一次真实 read。

read 后将结果回写 policy：

- 连续 `0xE2` 且 fault 已确认后进入 `OPEN_UNAVAILABLE`。
- probe 成功后恢复 `CLOSED`。
- probe 失败后继续低频 probe。
- open 后即使出现非 `0xE2` transport failure，也保持低频 probe，避免退回高频失败探测。

跳过 read 时不发布新的 `EVT:STREAM valid=0 reason=READ_FAIL`。只有真实 read 失败时才沿用原有 invalid sample 发布链。

### 3.3 串口 evidence

`LaserModule` 负责把 policy event 转成串口日志：

- `[MEASUREMENT_PROBE] event=open ...`
- `[MEASUREMENT_PROBE] event=skip ...`
- `[MEASUREMENT_PROBE] event=probe ...`
- `[MEASUREMENT_PROBE] event=still_unavailable ...`
- `[MEASUREMENT_PROBE] event=recovered ...`
- `[MEASUREMENT_PROBE] event=reset ...`

这些日志只服务固件 / capture 复核，不是 APP 业务 truth source。

## 4. 自动化验证

已执行：

```bash
git diff --check
python3 tools/run_evaluator_unit_tests.py
python3 -m platformio run -e esp32s3
python3 -m platformio run -e esp32_plus_laser_sim
```

结果：

- `git diff --check` 通过。
- evaluator host 单测通过。
- `esp32s3` 构建通过。
- `esp32_plus_laser_sim` 构建通过。

新增 focused policy tests 覆盖：

- healthy / not fault-confirmed 时不启用 low-frequency probe。
- 连续 `0xE2` 且 fault confirmed 后进入 open。
- open 未到期时跳过 read。
- probe 到期失败后继续 open。
- probe 成功后恢复 closed。
- closed 状态下非 `0xE2` 错误不触发 circuit-breaker。
- open 状态下非 `0xE2` transport failure 仍保持低频 probe。
- laser not installed / config reset 清空 policy 状态。

## 5. 后续真机验证

本包不在中途拆开要求真机测试。按当前协作计划，等 SW APP 与 ESP32 本轮都完成后统一跑 capture。

真机复核重点：

- PLUS degraded 连续 `0xE2` 后出现 `MEASUREMENT_PROBE event=open`。
- RUNNING 阶段不再持续每次 2 秒 timeout 连环探测。
- open 未到期时没有新的 `EVT:STREAM valid=0 reason=READ_FAIL` 刷新。
- `WAVE:START / START ALLOW / WAVE_OUTPUT_STARTUP / WAVE:STOP` 正常。
- `CAP? / SNAPSHOT / EVT:* / ACK:* / NACK:*` 解析不变。
- Android `CONNECT_SNAPSHOT_REFRESH_FAILED=0`。

## 6. 当前状态

本包代码实现和本地验证已完成。

尚未完成：

- 用户确认后分类 commit / push。

## 7. 真机 capture 复核

capture：

`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260517_205808__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_fault__esp32_plus_fault__manual__ESP32_RUNTIME__esp32_plus_measurement_probe_after_app_resource_governance`

场景：

- SW APP 资源治理后 smoke。
- ESP32 PLUS fault / measurement unavailable。
- 手动模式 start / running / stop。

用户结果：

- `RESULT=pass`
- summary：`APP resource governance smoke passed; ESP32 plus measurement probe observed during degraded running`

证据完整性：

- `session_meta.env` 完整。
- `notes.md` 包含 capture started、APP navigation / ESP32 connected、running 30s、manual stop、capture stopped。
- `warnings.log` 只有 `full_logcat_skipped`，本轮 focus log 足够，不构成 blocker。
- Android focus log、runtime events、ESP32 serial log 均可读。

APP / BLE 证据：

- `CONNECT_SUCCESS_COUNT=1`
- `CONNECT_SNAPSHOT_REFRESH_FAILED_COUNT=0`
- `DEVICE_SNAPSHOT_SYNCED_COUNT=6`
- `DEVICE_DEGRADED_START_SYNCED_COUNT=1`
- `DEVICE_DEGRADED_START_ACK_COUNT=2`
- `SESSION_LIFECYCLE_RESULT ... result=confirmed_by_device`
- `stop_confirmed_by_device`
- `DEVICE_EVENT_SAFETY_COUNT=0`
- `DEVICE_EVENT_FAULT_COUNT=0`

ESP32 输出链证据：

- `WAVE_START_COUNT=1`
- `START_ALLOW_COUNT=1`
- `WAVE_STOP_COUNT=1`
- `WAVE_OUTPUT_STARTUP_COUNT=4`
- `WAVE_OUTPUT_WRITE_ERROR_COUNT=0`
- `WAVE_OUTPUT_WRITE_SHORT_COUNT=0`
- `WAVE_OUTPUT_WRITE_SLOW_COUNT=0`
- `GURU_COUNT=0 / PANIC_COUNT=0 / BROWNOUT_COUNT=0`

measurement probe 证据：

- 串口捕获到 `MEASUREMENT_PROBE event=probe state=open`。
- 串口捕获到 `MEASUREMENT_PROBE event=skip state=open next_probe_in_ms=...`。
- 串口捕获到 `MEASUREMENT_PROBE event=still_unavailable code=0xE2 ... next_probe_in_ms=5000`。
- RUNNING 阶段同样可见 `event=skip / event=probe / event=still_unavailable`，说明 open 后不再每轮都做真实 Modbus read，而是按 5 秒 probe 窗口低频尝试。

证据限制：

- 本轮 capture 开始时 policy 已经处于 `state=open`，首条 ESP32 串口为 `event=probe state=open outage_ms=104211`，因此没有捕获到 open transition 本身的 `event=open`。
- 这不影响确认低频 probe 已生效，但如果后续需要证明“从连续 0xE2 到 event=open”的完整瞬间，需要重启 ESP32 后立即开始 capture，或在 capture 开始前清空故障历史再复现。

结论：

- SW APP 资源治理后的设备入口、连接、degraded-start、手动 start/stop 链路通过。
- ESP32 Measurement Availability Probe 低频探测行为通过。
- 本包真机验收结论为：通过，附带 `event=open` transition 未落入本轮 capture 窗口的证据限制。
