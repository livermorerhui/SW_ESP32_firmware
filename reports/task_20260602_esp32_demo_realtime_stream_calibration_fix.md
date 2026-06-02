# ESP32 Demo APP Realtime Stream / Calibration Fix

## 本轮类型

- 类型：Debug 证据采集后的最小修复与验收复核
- 目标链路：ESP32 测量平面 -> BLE `EVT:STREAM` -> Demo APP 遥测曲线 / 实时体重 / 实时距离 / 校准录点
- 相关链路：`SNAPSHOT.measurement_health`、`EVT:BASELINE`、MAX485/RS485 测量诊断
- 真相源：ESP32 固件串口与 Demo APP logcat
- 证据源：专项 capture 与 `tools/esp32_demo_telemetry_calibration_audit.py`
- 不该动的层：Demo APP 重构、校准算法、MAX485/Modbus 参数、`EVT:STREAM` wire format、正式 SW APP 业务语义

## 原问题结论

修复前并不是 Demo APP 单纯画图失败，也不是 RS485 完全无数据。

证据显示：

- ESP32 测量平面有有效读数，`MEASUREMENT_DIAG ok` 持续增长。
- Demo APP 能收到大量 `SNAPSHOT` / `EVT:BASELINE`，说明 BLE 连接与基础解析仍在。
- Demo APP 几乎收不到 `EVT:STREAM`。
- 固件串口有大量 `queue=stream suppressed_for_control`，实时流被 control/status 输出长期压制。

因此 owner 收敛到 BLE TX 调度公平性：control/status frame 优先级正确，但不能把 Demo APP 调试所需的实时 stream 无限期饿死。

## 改动点

- `src/transport/ble/BleTransport.cpp`
  - 去掉 enqueue 阶段直接丢弃 / 压制 stream 的行为。
  - 保留 stream queue 中的 latest sample，让 TX task 负责有界调度。
  - control/status frame 仍优先发送。
  - 当控制帧连续占用过久时，让出一次 `EVT:STREAM`。
- `src/transport/ble/BleTransport.h`
  - 增加 stream defer budget 判断和 control burst 计数状态。
- `tools/esp32_demo_telemetry_calibration_capture.sh`
  - 增加 Demo APP 遥测 / 校准专项采集入口。
- `tools/esp32_demo_telemetry_calibration_audit.py`
  - 增加专项证据复核。
  - 修正验收口径：用户确认通过且 Android 连续收到实时 `EVT:STREAM` 时，缺少 Demo APP 消费层调试日志不再直接判失败；ESP32 串口乱码仍保留为证据缺口。
- `docs/system/esp32_app_communication_semantics.md`
  - 补充 BLE TX stream fairness 合同和后续显式订阅建议。

## 抽象点

本轮没有引入新的跨端协议，也没有按 APP 身份分支。

抽象收敛在 BLE TX 内部调度：

- control/status 是关键 truth frame，保持优先。
- `EVT:STREAM` 是 measurement plane，可覆盖旧样本，但 latest sample 必须有机会发出。
- 公平性由 BLE transport 统一负责，避免业务层、Demo APP 或测量模块各自补 workaround。

## 参数点

- `kStreamMaxDeferMs = 250`
- `kMaxControlBurstBeforeStream = 4`
- `kDeferredStreamRetryDelayTicks = 5ms`
- `kStreamControlHoldoffMs = 35` 保留原有短暂控制帧 holdoff。

这些参数只影响 BLE TX 调度，不改变测量采样、校准算法、Modbus 读取或 BLE 文本协议格式。

## 真机证据

修复前 capture：

`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260602_141255__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-demo-telemetry-calibration__manual__ESP32_RUNTIME__esp32_demo_telemetry_calibration`

关键结论：

- `measurement_diag_ok_total=11794`
- Android 仅收到极少 `EVT:STREAM`
- `stream_suppressed_for_control_last_total=83310`
- 用户现象：曲线无实时数据，校准只能录到滞后数据

修复后 capture：

`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260602_143654__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-plus-demo-telemetry-calibration__manual__ESP32_RUNTIME__esp32_demo_telemetry_calibration`

关键结论：

- 用户结果：`pass`
- 用户摘要：修复后 Demo APP 实时曲线、实时体重、实时距离和校准录点恢复
- Android focus log 中连续收到 `EVT:STREAM`
- 专项 audit verdict：`PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP`
- `android_evt_stream_lines=202`
- `android_evt_stream_chunks=204`

## 验证结果

已执行：

- `python3 tools/run_evaluator_unit_tests.py`：通过
- `python3 -m py_compile tools/esp32_demo_telemetry_calibration_audit.py`：通过
- `bash -n tools/esp32_demo_telemetry_calibration_capture.sh`：通过
- `python3 tools/esp32_demo_telemetry_calibration_audit.py <post-fix-capture>`：输出 `PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP`
- `git diff --check`：通过
- `python3 -m platformio run -e esp32s3`：通过
- `python3 -m platformio run -e esp32s3 -t upload`：通过，已烧录并完成真机复测
- `tools/android_demo ./gradlew :sonicwave-protocol:test`：通过

## 剩余风险

- 修复后 capture 的 ESP32 串口文件存在乱码且行数不足，不能把“ESP32 串口证据完整通过”写成正式结论。
- 本轮通过的是 APP 链路证据与用户体感：Demo APP 已恢复实时 `EVT:STREAM`、曲线、体重、距离和校准录点。
- 后续若要做发布前更硬的设备侧验收，需要先修或切换串口采集方式，再复跑一次同一专项 capture。

## 后续建议

1. 当前包先收口，不继续做 Demo APP 重构。
2. 下一包如要优化 SW APP 与 Demo APP 的数据行为，应新增显式 stream subscription 合同，而不是用 APP 身份猜测：
   - `STREAM:SET enabled=1 rate_hz=10`
   - `ACK:STREAM ...`
   - `ACK:CAP stream_control_supported=1`
3. SW APP 可默认不启用实时 stream；Demo APP 在进入调试 / 校准时启用。
4. 加 subscription 前，本轮 BLE TX fairness 仍应保留，因为开启 stream 后仍必须防止实时流被 control/status 饿死。
