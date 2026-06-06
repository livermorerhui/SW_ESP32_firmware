# Demo APP Calibration Session Store Refactor

状态：第三包实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
对应事项：`FW-OPT-014`

## 1. 本轮实际完成

- 新增 `CalibrationSessionStore`，承接 Demo APP 校准会话的纯状态计算。
- 新增 `CalibrationSessionStoreTest`，覆盖 capture availability、手动模型解析、点集 fit、模型切换、无效点过滤和 reset。
- 改造 `DemoViewModel`，把校准 model input、model type selection、APP live snapshot 点集 append、legacy calibration point append、comparison rebuild、reset 等 pure state 操作交给 store。
- 保留 `DemoViewModel` 对副作用和文案的 owner：采集按钮前置失败分支、`client.send`、ACK/NACK/Error 接入顺序、resources `text(...)`、capture 结构化日志文案均未迁移。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/CalibrationSessionStore.kt`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/CalibrationSessionStoreTest.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`

## 3. 抽象点

`CalibrationSessionStore` 是本包新增的单一 owner：

- 校准录点可用性计算：connected / recording / reference / live distance。
- APP live snapshot 与 legacy calibration point 的点集 append 合同。
- valid calibration point 到 `CalibrationComparisonEngine.compare()` 的 fit dataset 过滤。
- `comparisonResult` rebuild。
- `preparedModel` 自动 fit 选择与手动输入解析。
- `modelOptions` selected / available / prepared 同步。
- 校准 session reset。

## 4. 参数点

- 校准点构造中的 `timestampMs`、`distanceMm`、reference weight、stable flag 仍由 `DemoViewModel` 传入，store 不读取系统时间。
- capture feedback 与中文提示文本仍由 `DemoViewModel` 传入，store 不依赖 Android resources。
- `CalibrationComparisonEngine` 仍来自 `sonicwave-protocol`，store 不修改拟合算法。
- `modelType` / `selectedComparisonModel` 仍使用现有 `CalibrationModelType`。

## 5. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check

cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.CalibrationSessionStoreTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

Gradle 输出中仍有既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且构建测试通过。

## 6. 不覆盖范围

本包未改：

- `sendCalibrationCapture()` 的失败分支和中文提示文案。
- `client.send(Command.CalibrationSetModel)`。
- ACK/NACK/Error 到 write/capture feedback 的接入顺序。
- `CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT / CAL_UI` 日志文案。
- ESP32 BLE wire payload。
- `STREAM:SET` 合同。
- 固件校准算法。
- MAX485 / Modbus 参数。
- 正式 SW APP 默认行为。

## 7. 剩余风险与下一步

当前 B3 已完成，未要求立即真机验证；因为本包只抽校准 pure state owner，且 focused / full unit gate 已通过。

下一包建议：

- 进入 B4 `WaveControlStateReducer` 前先做更细审计；B4 涉及控制状态和 start / stop 体感，风险高于 B2/B3。
- B4 只能抽 pure reducer，不迁移 `client.send`、truth refresh job、BLE command 时序或 `WaveLifecycleCommandGate` token 调用。
- 如果 B4 落代码，建议复用 `demo_app_wave_stop_race` capture 做最小真机回归。
