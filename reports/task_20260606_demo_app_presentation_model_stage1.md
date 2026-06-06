# Demo APP Presentation Model Refactor Stage 1

状态：B8 第一阶段实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
对应事项：`FW-OPT-019`

## 1. 本轮实际完成

- 新增 `SectionPresentationModels`，定义 section-specific callback DTO。
- 将 `CalibrationToolsSection` 的长回调参数列表收口为 `CalibrationToolsActions`。
- 将 `MotionSamplingSection` 的动作入口收口为 `MotionSamplingActions`。
- 在 `MainScreen` 使用 `remember(viewModel)` 构建 calibration actions，降低主屏参数噪声和误绑风险。
- 保留业务状态 owner：`UiState`、`DemoViewModel`、已抽出的 stores / reducers 均未改变职责。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/SectionPresentationModels.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/CalibrationToolsSection.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/components/MotionSamplingSection.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/ui/screens/MainScreen.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`

## 3. 抽象点

本包新增的是 UI presentation contract，不是业务 owner：

- `CalibrationInputCallbacks`：校准输入字段变化。
- `CalibrationCommandCallbacks`：校准命令、录制、工程区开关。
- `CalibrationToolsActions`：校准 section 对外动作聚合。
- `MotionSamplingActions`：motion sampling section 对外动作聚合。

这些 DTO 只表达 UI section 能触发哪些动作，不承载设备事实、业务状态或协议语义。

## 4. 参数点

- `CalibrationToolsSection` 对外参数从 `UiState + 多个 callback` 收口为 `UiState + CalibrationToolsActions`。
- `MotionSamplingSection` 对外参数从 `UiState + 多个 callback` 收口为 `UiState + MotionSamplingActions`。
- `MainScreen` 保留 ViewModel 作为 screen-level owner，只在 presentation 层组装 callbacks。
- UI 内 `rememberSaveable` 的导出弹窗、标签选择、MA 点数等 local UI state 继续留在 composable 生命周期内。

## 5. 成熟方案对齐

本包按 Android 官方 Compose state hoisting / state holder 思路执行：

- UI state 应提升到实际需要读写它的最低共同 owner。
- 涉及业务逻辑的 screen state 继续由 ViewModel / store 暴露。
- 简单 UI element state 可以留在 composable 内，复杂 UI 逻辑可用 plain holder 或 presentation DTO 收口。

因此本包没有把 composable local state 强行搬到 ViewModel，也没有把业务状态下沉到 UI。

## 6. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check

cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

Gradle 输出中仍有既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且构建测试通过。

## 7. 不覆盖范围

本包未改：

- UI 视觉和交互。
- `DemoViewModel` 业务方法行为。
- 已抽 stores / reducers 的状态合同。
- BLE command 时序。
- ESP32 BLE wire payload。
- 固件协议、校准算法或 MAX485 参数。
- 正式 SW APP 默认行为。
- 多 ViewModel / navigation 架构。

## 8. 剩余风险与下一步

当前 B8 第一阶段是 presentation 参数治理，单元门禁通过；不要求立即真机验证。

下一步建议：

- 先提交 B8 第一阶段。
- 再做 Demo APP quality baseline audit，复核剩余大参数面、remaining `DemoViewModel` 职责和是否需要统一 smoke / capture。
- B4 第二阶段和 B6 `TestSessionBridge` 仍需先审计控制链，不建议直接自动开。
