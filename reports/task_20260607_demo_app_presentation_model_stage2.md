# Demo APP Presentation Model Refactor Stage 2

状态：B8 第二阶段实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-07
对应事项：`FW-OPT-019`

## 1. 本轮实际完成

- 扩展 `SectionPresentationModels`，新增 `DeviceToolsActions`、`WaveControlActions`、`TestSessionActions`、`RawConsoleActions`。
- 将 `WaveControlBottomBar` 的多个回调参数收口为 `WaveControlActions`。
- 将 `TestSessionSection` 的 clear / export 回调收口为 `TestSessionActions`。
- 将 `RawConsoleSection` 的 clear 回调收口为 `RawConsoleActions`。
- `MainScreen` 统一收集 `measurementDisplayState`、`testSessionPanelState`、`rawConsoleState`，并统一构建 section actions。
- `DeviceToolsContent` 和 `RunDashboardContent` 不再接收 `DemoViewModel`，只接收 UI state / presentation actions。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/SectionPresentationModels.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/WaveControlBottomBar.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/TestSessionSection.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/RawConsoleSection.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/screens/MainScreen.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`

## 3. 抽象点

- 本包继续只抽 presentation wiring，不抽业务 owner。
- `MainScreen` 作为 screen-level composition root，负责把 `DemoViewModel` 方法组装成 section actions。
- `DeviceToolsContent`、`RunDashboardContent`、`WaveControlBottomBar`、`TestSessionSection`、`RawConsoleSection` 只消费明确的 actions DTO，不直接依赖 `DemoViewModel`。
- `TelemetryChartSection` 仍只消费 telemetry points 和 stable-weight truth；measurement display state owner 仍是 `MeasurementDisplayStore`。

## 4. 参数点

- `WaveControlBottomBar` 对外参数从 `UiState + 8 个回调` 收口为 `UiState + WaveControlActions`。
- `TestSessionSection` 对外参数从 `panelState + 2 个回调` 收口为 `panelState + TestSessionActions`。
- `RawConsoleSection` 对外参数从 `rawLogLines + onClear` 收口为 `rawLogLines + RawConsoleActions`。
- `DeviceToolsContent` 对外参数从 `UiState + DemoViewModel` 收口为 `UiState + DeviceToolsActions`。
- `RunDashboardContent` 对外参数从 `UiState + DemoViewModel` 收口为 `UiState + telemetryPoints + testSessionPanelState + TestSessionActions`。

## 5. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:compileDebugKotlin --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
./gradlew :app-demo:assembleDebug --no-daemon --stacktrace
```

Gradle 仍输出既有 JDK path warning；`RawConsoleSection` 仍有既有 `LocalClipboardManager` deprecation warning。本包未新增运行失败。

## 6. 不覆盖范围

本包未改：

- UI 视觉、布局、文案和按钮启用条件。
- `DemoViewModel` 业务方法实现。
- stores / reducers 的状态合同。
- BLE command 时序。
- ESP32 BLE wire payload。
- 固件协议、校准算法或 MAX485 参数。
- 多 ViewModel / navigation 架构。

## 7. 是否需要真机

本包只改 Compose presentation 参数和 actions wiring，且 baseline smoke 刚通过；不需要立即真机。若后续继续触碰按钮启用条件、控制链 command send、capture 文案或导出格式，再进入专项 capture。

## 8. 下一步建议

- 推荐先跑完整 `:sonicwave-protocol:test :app-demo:testDebugUnitTest` 和 `:app-demo:assembleDebug` 后收口。
- 后续如果继续重构，只做小范围 section props 或局部 composable 拆分。
- 继续冻结 BLE client、连接 owner、完整 event reducer 和 `DemoViewModel` 全量拆分。
