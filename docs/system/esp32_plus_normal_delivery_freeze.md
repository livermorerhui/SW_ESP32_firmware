# ESP32 PLUS Normal Delivery Freeze

最后更新时间：2026-04-28

## 1. 交付判断

当前可以先交付的范围是：

- `ESP32-plus 正常版`
- SW APP 与 ESP32 的 BLE 连接、开始、停止、手动断开重连
- baseline / start gate / reconnect snapshot 补偿
- PLUS measurement / RS485 当前参数下的基础稳定性观察

这个冻结点适合：

- 阶段性 Demo
- 交给后续任务作为稳定基线
- 暂停 ESP32 A 级重构，转去处理其他任务

它不等于：

- 全部 ESP32 变体最终发布完成
- motion safety 新 detector 已接 runtime action
- base / plus degraded 已完成新一轮整机复核
- 长时间 soak / 发布级四端矩阵已全部完成

## 2. 推荐版本组合

代码基线：

- SW 仓：`archive/pre-sessioncoordinator-refactor-20260418`
- SW 仓提交：`a573967 docs: record BLE connect serial smoke evidence`
- ESP32 固件仓：`feature/fw-safety-and-demo-rollup`
- ESP32 固件运行代码提交：`e6364da refactor: add baseline action evidence snapshots`

说明：

- 2026-04-28 之后新增的交付冻结文档不改变运行代码。
- 如后续提交文档，应仍以 `e6364da` 作为本冻结点的固件运行代码基线。

## 3. 已验证链路

### BLE connect stability

已验证：

- Android connect gate 只在 `protocolReady=true` 后视为连接完成。
- `CONNECTED / protocolReady=false` 不再提前取消 reconnect flow。
- `status=62` 如出现，可通过 Android GATT stage 日志定位到 transport 阶段。
- 带 ESP32 串口的补测中未再出现 `status=62`。

关键证据：

- `SW/.artifacts/device-test-captures/20260428_123745__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-normal__manual__ESP32_RUNTIME__ble_connect_stability_smoke`
- `SW/.artifacts/device-test-captures/20260428_132035__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-normal__manual__ESP32_RUNTIME__ble_connect_stability_serial_smoke`

通过点：

- Android：`CONNECT_SUCCESS attempt_id=6/7`
- Android：`AUTO_RECONNECT_CANCELLED reason=protocol_ready`
- Android：无 `CONNECT_FAILURE / CONNECT_CANCELLED / status=62 / StandaloneCoroutine was cancelled / CONNECT_SNAPSHOT_REFRESH_FAILED`
- ESP32：session 14 / 15 connect
- ESP32：MTU applied `185`
- ESP32：`reconnect_snapshot_compensated=2`
- ESP32：无 reset / panic / Brownout / Guru / MEASUREMENT_TRANSIENT / Modbus read fail

### 开始 / 停止 / baseline

已验证：

- SW APP 连接成功后 baseline 可以建立。
- `WAVE:START` 通过固件 start gate。
- `WAVE:STOP` 正常停波。
- 手动断开后重连，snapshot 能恢复真实状态。

关键证据：

- `SW/.artifacts/device-test-captures/20260428_110030__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-normal__manual__ESP32_RUNTIME__baseline_action_evidence_smoke`
- `SW/.artifacts/device-test-captures/20260428_132035__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-normal__manual__ESP32_RUNTIME__ble_connect_stability_serial_smoke`

通过点：

- APP：`DEVICE_SNAPSHOT_SYNCED`
- APP：`start_confirmed_by_device`
- APP：`stop_confirmed_by_device`
- ESP32：`START ALLOW=1`
- ESP32：`STOP_SUMMARY result=NORMAL`
- ESP32：`BASELINE_CONTRACT event=latch / clear / start_ready_writeback`

### Measurement / RS485 transient

已验证：

- Demo APP 只连接不律动约 47 分钟观察未复现 RS485 transient。
- 当前不调整 `20ms` read interval。

关键证据：

- `SW/.artifacts/device-test-captures/20260427_121440__vivo_V2405A__esp32_plus_normal__esp32_plus_normal__manual__ESP32_RUNTIME__plus_measurement_rs485_demo_connected_idle_30m`

通过点：

- `MEASUREMENT_DIAG=1426`
- `ok=71308`
- `fail=0`
- `MEASUREMENT_TRANSIENT=0`
- `max_read_ms=16`

## 4. 冻结边界

当前交付冻结后，除非另起专项包，不应改：

- BLE 外部线格式：
  - `CAP?`
  - `SNAPSHOT?`
  - `WAVE:*`
  - `EVT:*`
  - `ACK:*`
  - `NACK:*`
- Android `SessionCoordinator`
- ESP32 `SystemStateMachine` action timing
- `setStartReadiness / setRuntimeReady / onUserOff`
- `releaseOccupiedCycle / rhythmStateJudge.reset / clearStableContractBridge`
- `MotionSafetyShadowEvaluator` 接 runtime action
- `LASER_MEASUREMENT_READ_INTERVAL_MS=20ms`

## 5. 未覆盖 / 不应过度承诺

当前仍未最终覆盖：

- `ESP32-base` 新一轮整机复核
- `ESP32-plus degraded / 485 或 laser 故障形态` 新一轮整机复核
- motion safety 新 detector 接 runtime action
- 长时间带律动 soak
- 发布级四端完整矩阵
- SafetyAction / StopReason 深层 action owner 拆分

不要对外说：

- “所有 ESP32 形态都已完成”
- “新 detector 已正式替代 runtime 停波逻辑”
- “发布级 soak 已完成”

可以说：

- “ESP32-plus 正常版主链已经有 Android + ESP32 串口证据，可以作为阶段性交付基线。”
- “剩余项是继续优化重构、降级形态复核和发布级加固，不是 PLUS 正常主链 blocker。”

## 6. 后续恢复工作入口

如果暂停后继续，默认从这里恢复：

1. 先读 `SW/docs/system/ESP32与APP联调优化优先级总表.md`。
2. 再读 `SW_ESP3_Firmware/docs/system/esp32_firmware_remaining_work_and_lessons.md`。
3. 优先进入 `SafetyActionContractEvaluator` 最小纯函数抽取。
4. 仍然不直接迁移 `SystemStateMachine` 停波动作时机。

## 7. 最小回归步骤

如果后续只需要确认冻结基线没有坏：

前置条件：

- 不需要重装 APP，除非 Android 代码有新改动。
- 不需要重烧 ESP32，除非固件运行代码有新改动。
- 需要 ESP32 串口接电脑。

命令：

```bash
cd /Users/r.w.hui/Desktop/SW

scripts/esp32_plus_normal_diag_capture.sh start \
  --android-serial ab5e0167 \
  --accessory-model "ESP32-plus-normal" \
  --mode manual \
  --scenario plus_normal_delivery_freeze_smoke \
  --esp32-port /dev/cu.usbmodem5B150892791 \
  --esp32-baud 115200
```

手机操作：

1. SW APP 连接 ESP32-plus。
2. 开始。
3. 停止。
4. 手动断开或退出连接。
5. 重新连接一次。

收尾：

```bash
scripts/device_test_capture.sh mark --note "delivery freeze smoke done"
scripts/esp32_plus_normal_diag_capture.sh stop --result observe --summary "ESP32 PLUS normal delivery freeze smoke"
```
