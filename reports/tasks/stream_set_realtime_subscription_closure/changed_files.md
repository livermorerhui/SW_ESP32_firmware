# STREAM:SET 收口改动文件清单

状态：阶段交付记录
文档类型：历史快照 / 文件清单
适用范围：2026-06-02 `STREAM:SET` 显式实时流订阅修复包
Owner：ESP32 firmware / Demo APP engineering tools
更新日期：2026-06-02
当前替代入口：`docs/system/esp32_realtime_stream_subscription_contract.md`
证据源：merge commit `bc18a74`

## 1. 固件

- `src/core/CommandBus.h`
- `src/core/ProtocolCodec.h`
- `src/HubAckBuilder.h`
- `src/main.cpp`
- `src/modules/laser/LaserModule.h`
- `src/modules/laser/LaserModule.cpp`

## 2. Demo APP / SDK / Protocol

- `tools/android_demo/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/Model.kt`
- `tools/android_demo/sonicwave-protocol/src/main/kotlin/com/sonicwave/protocol/ProtocolCodec.kt`
- `tools/android_demo/sonicwave-protocol/src/test/kotlin/com/sonicwave/protocol/ProtocolCodecTest.kt`
- `tools/android_demo/sonicwave-sdk/src/main/java/com/sonicwave/sdk/SonicWaveClient.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `tools/android_demo/README.md`

## 3. Capture / Audit

- `tools/esp32_demo_telemetry_calibration_capture.sh`
- `tools/esp32_demo_telemetry_calibration_audit.py`

## 4. 协议和长期文档

- `docs/protocol.md`
- `docs/ble-init-contract.md`
- `docs/system/esp32_app_communication_semantics.md`
- `docs/system/esp32_realtime_stream_subscription_contract.md`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `AGENTS.md`

## 5. 本轮报告

- `reports/tasks/stream_set_realtime_subscription_closure/artifact_summary.md`
- `reports/tasks/stream_set_realtime_subscription_closure/retrospective.md`
- `reports/tasks/stream_set_realtime_subscription_closure/changed_files.md`

## 6. 本轮未改

- 未改 `EVT:STREAM` wire payload。
- 未改校准算法。
- 未改 MAX485 / Modbus 参数。
- 未改 safety / stop 语义。
- 未改正式 SW APP 默认行为。
