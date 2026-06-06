# Demo APP Measurement Display Store Refactor

状态：第一包实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
对应事项：`FW-OPT-013`

## 1. 本轮实际完成

- 新增 `MeasurementDisplayStore`，承接 Demo APP measurement display 的纯状态计算。
- 新增 `MeasurementDisplayStoreTest`，覆盖 carrier policy、valid sample、invalid sample、moving average reset、telemetry window trim 和 reset。
- 改造 `DemoViewModel.onStreamSample()`，让它调用 store 获取 `MeasurementDisplayUiState` 和最新 `TelemetryPointUi`。
- 保留 `DemoViewModel` 对副作用的 owner：`DemoMeasurementTrace`、system log、recording、motion sampling、test session、`_uiState.update`、stream watchdog。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/MeasurementDisplayStore.kt`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/MeasurementDisplayStoreTest.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`

## 3. 抽象点

`MeasurementDisplayStore` 是本包新增的单一 owner：

- `ProtocolMode + MeasurementCarrier` 是否消费。
- invalid sample 如何更新 display state 并清空 recent moving average。
- valid sample 如何生成 `TelemetryPointUi`。
- `ma3 / ma5 / ma7` moving average。
- telemetry display buffer 的窗口裁剪。
- reset 后的空 display state。

## 4. 参数点

- `telemetryWindowMs` 由 `DemoViewModel` 继续传入，当前仍使用原 `TELEMETRY_WINDOW_MS = 20_000L`。
- recent moving average 最大窗口保持 7，不改变原 `ma3 / ma5 / ma7` 行为。
- `stableWeightActive` 继续由 `DemoViewModel` 的 UI state 判定，store 只负责映射到 telemetry point。

## 5. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check

cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.MeasurementDisplayStoreTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

Gradle 输出中仍有既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且构建测试通过。

## 6. 不覆盖范围

本包未改：

- ESP32 BLE wire payload。
- `STREAM:SET` 合同。
- 固件校准算法。
- MAX485 / Modbus 参数。
- 正式 SW APP 默认行为。
- Demo APP calibration session owner。
- BLE connection owner。
- recording / motion sampling / test session owner。
- Compose UI 拆分。

## 7. 剩余风险与下一步

当前 B1 已完成，未要求立即真机验证；因为本包只抽纯状态计算且单元门禁通过。

后续建议：

- 若继续 Demo APP 重构，下一包仍不建议直接做全量 `DemoViewModel` 拆分。
- 只有当校准录点归因、legacy `ACK:CAL_POINT` 合并或模型比较 rebuild 再次成为真实问题时，才启动 `FW-OPT-014 Calibration session owner 抽取`。
- 若只是验证现场行为，可复用既有 `tools/esp32_demo_telemetry_calibration_capture.sh` 复核 Demo APP telemetry / calibration 证据链。
