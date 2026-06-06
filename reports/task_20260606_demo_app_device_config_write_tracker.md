# Demo APP Device Config Write Tracker Refactor

状态：B7 实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
对应事项：`FW-OPT-020`

## 1. 本轮实际完成

- 新增 `DeviceConfigWriteTracker`，承接 Demo APP device config 写入反馈的纯状态追踪。
- 新增 `DeviceConfigWriteTrackerTest`，覆盖 pending request、observed truth confirmation、ACK fallback、NACK/Error、timeout 和 confirmation refresh gate。
- 改造 `DemoViewModel`，让 `sendDeviceConfig()`、`ACK:DEVICE_CONFIG`、`ACK:OK` fallback、`NACK`、`ERROR`、watchdog timeout 调用 tracker。
- 保留 `DemoViewModel` 对副作用和文案的 owner：`client.send(Command.DeviceSetConfig)`、watchdog coroutine、snapshot/capability refresh、中文状态文案和 system log 调用点均未迁移。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DeviceConfigWriteTracker.kt`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/DeviceConfigWriteTrackerTest.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`

## 3. 抽象点

`DeviceConfigWriteTracker` 是本包新增的单一 owner：

- device config pending request。
- observed `platform_model / laser_installed` 与 request 的匹配确认。
- mismatch / missing field keep pending。
- generic ACK fallback clear pending。
- NACK / Error clear pending。
- timeout clear pending。
- confirmation refresh 是否需要触发。

## 4. 参数点

- `requestedAtMs` 由 ViewModel 传入，tracker 不读取系统时间。
- tracker 不持有 `client`、coroutine scope、Android resources、watchdog job、snapshot refresh 或 system log。
- `DEVICE_CONFIG_WRITE_CONFIRM_REFRESH_DELAY_MS` 和 `DEVICE_CONFIG_WRITE_TIMEOUT_MS` 继续留在 ViewModel，未新增配置参数。
- 设备真相字段仍为正式协议字段 `platform_model / laser_installed`。

## 5. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check

cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.DeviceConfigWriteTrackerTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

Gradle 输出中仍有既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且构建测试通过。

## 6. 不覆盖范围

本包未改：

- `client.send(Command.DeviceSetConfig)`。
- `DEVICE:SET_CONFIG` payload。
- `ACK:DEVICE_CONFIG` 解析合同。
- snapshot / capability refresh 命令。
- watchdog coroutine 生命周期。
- UI 视觉和交互。
- 中文状态文案。
- system log 文案。
- ESP32 固件配置写入行为。
- 正式 SW APP 默认行为。

## 7. 剩余风险与下一步

当前 B7 是 pure tracker 抽取，focused 单元门禁已通过；不要求立即真机验证。若后续要把这批 Demo APP 重构作为发布或真机验收基线，建议统一跑一次 Demo APP smoke / capture，而不是单独为 B7 跑专项。

下一包建议：

- B8 Presentation model / Compose 参数收口可作为低风险 UI 参数治理包，但价值低于当前 owner 抽取包。
- B4 第二阶段和 B6 `TestSessionBridge` 暂不建议直接做；它们会进入控制链 / session bridge 高风险区，应先单独审 pending lifecycle 和 formal session action。
