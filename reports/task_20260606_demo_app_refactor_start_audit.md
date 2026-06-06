# Demo APP Refactor Start Audit

状态：重构启动审计
文档类型：阶段报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
真相源：`docs/system/esp32_firmware_optimization_priority_table.md`、`reports/tasks/demo_app_observability_quality_audit/`、commit `a7882f9`、当前 Demo APP 代码
证据源：代码审计、现有 JVM tests、真机 capture `20260604_164648...demo_app_wave_stop_race`

## 1. 本轮判型

本轮属于审计 / 高风险冻结前置，不直接做大范围代码拆分。

目标链路：

- Demo APP `EVT:STREAM` 消费。
- Demo APP measurement display state / telemetry points。
- Demo APP 校准录点入口。

相关链路：

- `STREAM:SET` 显式实时流订阅。
- `DemoMeasurementTrace` 结构化日志。
- test session / motion sampling / recording 三条旁路消费。
- `tools/esp32_demo_telemetry_calibration_audit.py` 真机证据判读。

本轮不该动：

- ESP32 BLE wire payload。
- `STREAM:SET` 合同。
- 固件校准算法。
- MAX485 / Modbus 参数。
- 正式 SW APP 默认行为。
- Demo APP 全量连接 / 控制 / 校准 / 导出 / UI 状态一次性大拆。

## 2. 当前结论

可以自动重构，但只能做小包：

- 推荐第一包：`FW-OPT-013 Measurement display owner 抽取`。
- 不推荐第一包：`FW-OPT-014 Calibration session owner 抽取`。
- 明确冻结：`FW-OPT-015 DemoViewModel 大范围重构`。

原因：

- `DemoViewModel.kt` 当前约 4659 行，确实过宽。
- `onStreamSample()` 不是纯显示逻辑，仍牵连 recording、motion sampling、test session、system log 和 capture availability。
- 可以先抽取“纯 measurement display state / telemetry buffer / moving average / carrier accept policy”，让 ViewModel 继续负责副作用。
- 校准录点同时包含 APP live snapshot 和 legacy `ACK:CAL_POINT` 两条路径，且牵连 model compare / prepared model / UI feedback；第一刀自动拆风险高。

## 3. 自动重构建议包

### 包 B1：MeasurementDisplayStore

目标：

- 把 `Event.StreamSample -> MeasurementDisplayUiState / TelemetryPointUi / moving average` 的纯状态计算从 ViewModel 抽出。
- 保留 ViewModel 对副作用的 owner：日志、recording、motion sampling、test session、`_uiState.update`。

建议新增文件：

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/MeasurementDisplayStore.kt`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/MeasurementDisplayStoreTest.kt`

建议 API：

```kotlin
internal class MeasurementDisplayStore(
    private val telemetryWindowMs: Long,
) {
    fun reset()
    fun shouldConsume(protocolMode: ProtocolMode, carrier: MeasurementCarrier): Boolean
    fun applyInvalid(sample: Event.StreamSample): MeasurementDisplaySnapshot
    fun applyValid(
        sample: Event.StreamSample,
        nowMs: Long,
        telemetrySessionStartMs: Long,
        stableWeight: Float?,
        stableWeightActive: Boolean,
    ): MeasurementDisplaySnapshot
}
```

`MeasurementDisplaySnapshot` 应至少包含：

- latest distance / weight / ma12。
- measurement valid。
- last measurement sequence。
- telemetry points。
- latest point for旁路 consumer。

## 4. 必须保留在 ViewModel 的 owner

第一包不得迁移：

- `appendSystemLog()`。
- `DemoMeasurementTrace` summary 输出。
- `recordingSession` / `TelemetryRecorder`。
- `MotionSamplingSessionUi` row 追加。
- `TestSessionUi` sample 追加。
- `_uiState.update` 和 `withCaptureAvailability()`。
- stream watchdog / warning 清理。

这些副作用只消费 `MeasurementDisplayStore` 输出，不进入 store。

## 5. 回归测试矩阵

JVM tests 必须覆盖：

- `PRIMARY` / `UNKNOWN` 只消费 `FORMAL_EVT_STREAM`。
- `LEGACY` 可消费 legacy CSV fallback。
- invalid sample 清理 recent moving average，更新 latest distance / weight / ma12 / sequence，`measurementValid=false`。
- valid sample 生成 `TelemetryPointUi`，包含 distance、weight、ma12、ma3/ma5/ma7。
- telemetry window trim 不丢最新点。
- reset 后 display state 清空。

本地门禁：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check
cd tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.MeasurementDisplayStoreTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

真机验证：

- 如果只做 B1 且测试通过，不要求立即真机。
- 如果 B1 后继续动 capture / 校准 / stream 订阅，必须复用 `tools/esp32_demo_telemetry_calibration_capture.sh` 做真机复核。

## 6. 暂不自动重构的范围

暂不做：

- `CalibrationSessionController`。
- `DemoEventReducer`。
- Compose UI 文件拆分。
- BLE connection owner 拆分。
- Raw console / test session / motion sampling owner 拆分。

这些范围只有在 B1 通过、且后续真实问题或新功能需要时再开独立包。

## 7. 当前启动建议

建议先收口当前未提交的长期总表文档改动，再开 B1 实现包。

如果继续自动实现，建议只执行：

1. 新增 `MeasurementDisplayStore`。
2. 新增 focused JVM tests。
3. 将 ViewModel 中 `telemetryDisplayBuffer / recentWeightBuffer / recentMovingAverage / shouldConsumeMeasurementCarrier / appendTelemetryDisplayPoint / trimTelemetryDisplayBuffer` 的纯状态部分替换为 store 调用。
4. 保持 `onStreamSample()` 的副作用顺序不变。
5. 跑本地门禁。

不建议直接进入 B2 / B3，也不建议把本轮叫做“Demo APP 全量重构”。
