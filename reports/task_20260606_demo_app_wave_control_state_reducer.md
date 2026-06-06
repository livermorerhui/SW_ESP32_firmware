# Demo APP Wave Control State Reducer Refactor

状态：B4 第一阶段实现完成
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新日期：2026-06-06
对应事项：`FW-OPT-017`

## 1. 本轮实际完成

- 已提交前置 B2/B3：`d3fd7df refactor(demo): extract console and calibration stores`。
- 新增 `WaveControlStateReducer`，承接 Demo APP wave control 的第一批纯状态规则。
- 新增 `WaveControlStateReducerTest`，覆盖 pending flags、availability、runtime clock 和 formal wave truth sync。
- 改造 `DemoViewModel`，把 `syncFormalWaveTruth()`、`syncWaveControlFlags()`、`applyWaveOutputTransition()` 改为薄 wrapper，实际规则交给 reducer。
- 将原先在 `DemoViewModel` 文件底部的 wave control resolver 迁出：`SnapshotStartReadyMergeContext`、`resolveAuthoritativeWaveOutput()`、`shouldTreatSafetyAsWaveStopped()`、`resolveSnapshotStartReady()`、`mergeProtocolMode()`、`shouldAttemptPrimarySnapshotRefresh()`、`resolveOptimisticStopState()`。

## 2. 改动点

- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/WaveControlStateReducer.kt`
- `tools/android_demo/app-demo/src/test/java/com/sonicwave/demo/WaveControlStateReducerTest.kt`
- `tools/android_demo/app-demo/src/main/java/com/sonicwave/demo/DemoViewModel.kt`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`

## 3. 抽象点

`WaveControlStateReducer` 是本包新增的 pure reducer：

- `waveOutputActive` runtime transition。
- `waveRuntimeStartMs / waveRuntimeElapsedMs` 更新。
- formal safety mirror 中 runtime / wave code 同步。
- `isWaveStartPending / isWaveStopPending` 计算。
- snapshot `start_ready` merge 策略。
- authoritative wave output 解析。
- safety event 是否代表 wave stopped。
- optimistic stop target state。

## 4. 参数点

- `timeProvider` 由 ViewModel 注入，生产使用 `System.currentTimeMillis()`，测试使用可控时间。
- pending lifecycle 没有迁入 reducer；ViewModel 仍显式传入 `hasPendingStart / hasPendingStop`。
- reducer 不持有 `client`、coroutine scope、resource text、test session store 或 command token。

## 5. 验证结果

已通过：

```bash
cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware
git diff --check

cd /Users/r.w.hui/Desktop/SW_ESP3_Firmware/tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.WaveControlStateReducerTest" --tests "com.sonicwave.demo.WaveLifecycleCommandGateTest" --no-daemon --stacktrace
./gradlew :app-demo:testDebugUnitTest --tests "com.sonicwave.demo.DemoStartReadyRegressionTest" --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

说明：

- 曾并行运行 `DemoStartReadyRegressionTest` 与全量 `testDebugUnitTest`，Gradle 同时访问同一 binary test result 输出目录，出现 `output.bin.idx` 缺失；清理测试输出目录后单独重跑全量门禁已通过。
- Gradle 输出中仍有既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且构建测试通过。

## 6. 不覆盖范围

本包未改：

- `client.send(Command.WaveSet / WaveStart / WaveStop)`。
- `viewModelScope` command job。
- truth refresh scheduling / cancellation。
- `WaveLifecycleCommandGate` token 调用。
- `PendingWaveStartRequest / PendingWaveStopRequest / PendingWaveStopCompletion` 生命周期。
- test session start / finish / export 副作用。
- BLE command 时序。
- ESP32 BLE wire payload。
- capture 日志文案和 audit 语义。
- 固件 `SystemStateMachine` 或 wave output 行为。

## 7. 剩余风险与下一步

当前 B4 第一阶段只抽 pure reducer，单元门禁通过；由于控制链被触碰，若要作为真机验收基线，建议复用 `demo_app_wave_stop_race` capture 做最小回归。

下一步建议：

- 先提交 B4 第一阶段，保持可回滚边界。
- B4 第二阶段如果继续，只审 pending lifecycle / formal session action 是否可抽；不要迁 `client.send`、truth refresh job 或 BLE command 时序。
- 如果要继续开新包，优先考虑 B5 `MotionSamplingSessionStore`，因为它比继续深拆控制链更低风险。
