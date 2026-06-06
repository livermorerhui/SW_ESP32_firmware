# Demo APP Raw Console Store Refactor

状态：第二包实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
对应事项：`FW-OPT-016`

## 1. 本轮实际完成

- 新增 `RawConsoleStore`，承接 Demo APP raw console 的纯状态计算。
- 新增 `RawConsoleStoreTest`，覆盖 raw log 追加、max line trim、`EVT:STREAM` 过滤、legacy CSV fallback 过滤、verbose 保留、高优先级 publish 和 reset。
- 改造 `DemoViewModel`，让 `appendRawLog()` 调用 store，`publishRawConsole()` 读取 store state。
- 保留 `DemoViewModel` 对业务日志调用点的 owner：`appendSystemLog()` 调用位置、capture 结构化日志文案、raw flow collect 均未迁移。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/RawConsoleStore.kt`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/RawConsoleStoreTest.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`

## 3. 抽象点

`RawConsoleStore` 是本包新增的单一 owner：

- raw log ring buffer。
- raw log timestamp / direction 格式化。
- `RawConsoleUiState` 生成。
- `EVT:STREAM` 默认隐藏。
- legacy CSV fallback 默认隐藏。
- verbose mode 下 raw stream 保留。
- high-priority log 强制 publish 判定。

## 4. 参数点

- `maxLines` 由 `DemoViewModel` 继续传入，当前仍使用原 `MAX_RAW_LOG_LINES = 200`。
- time provider 支持测试注入，生产默认 `LocalTime.now()`。
- test session 日志是否 high-priority 仍由 `DemoViewModel` 通过 `shouldTrackTestSessionAutomation(_uiState.value)` 传入。

## 5. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check

cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.RawConsoleStoreTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

Gradle 输出中仍有既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且构建测试通过。

## 6. 不覆盖范围

本包未改：

- capture 结构化日志文案。
- `appendSystemLog()` 的业务调用点。
- BLE raw flow collect 顺序。
- ESP32 BLE wire payload。
- `STREAM:SET` 合同。
- 固件校准算法。
- MAX485 / Modbus 参数。
- 正式 SW APP 默认行为。

## 7. 剩余风险与下一步

当前 B2 已完成，未要求立即真机验证；因为本包只抽 raw console 纯状态计算且单元门禁通过。

下一包建议：

- 按总计划进入 B3 `CalibrationSessionStore`，但只抽 pure store，不迁移 `client.send`、ACK/NACK/Error 接入顺序或 capture 日志文案。
- 如果 B3 触碰 `CAL_CAPTURE_ATTEMPT / CAL_CAPTURE_RESULT / CAL_UI_BIND` 语义，需要复用 `tools/esp32_demo_telemetry_calibration_capture.sh` 真机复核。
