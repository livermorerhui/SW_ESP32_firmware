# Demo APP Wave Pending Lifecycle Store

状态：B4 第二阶段完成 / 运行页真机 smoke 已通过
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新时间：2026-06-07

## 1. 本轮判型

本轮属于 Demo APP 整包重构。目标链路是运行页 Start / Stop pending lifecycle，特别是 start request、stop request、stop completion、truth refresh idle 判定和 UI pending flags 的状态归属。

相关链路：

- `WaveControlStateReducer`
- `WaveLifecycleCommandGate`
- `TestSessionBridge`
- Demo APP 运行页 Start -> Stop smoke

真相源：

- B4 第一阶段 reducer 行为。
- `DemoViewModel` 既有 pending request 行为。
- B6 `TestSessionBridge` 的 session start / finish gate。

证据源：

- `WavePendingLifecycleStoreTest`
- `WaveControlStateReducerTest`
- `WaveLifecycleCommandGateTest`
- `TestSessionBridgeTest`
- `:sonicwave-protocol:test :app-demo:testDebugUnitTest`
- `:app-demo:assembleDebug`
- Demo APP quality smoke capture `20260607_152955...demo_app_quality_smoke`

不该动的层：

- ESP32 固件协议和 BLE wire payload。
- `client.send(Command.WaveSet / WaveStart / WaveStop)` 时序。
- `WaveLifecycleCommandGate` token 语义。
- truth refresh coroutine 的发送节奏。
- `TestSessionExporter` 导出格式。
- Demo APP UI 信息架构和文案。

## 2. 改动点

- 新增 `WavePendingLifecycleStore`，集中管理 wave start / stop pending lifecycle。
- 将 `PendingWaveStartRequest`、`PendingWaveStopRequest`、`PendingWaveStopCompletion` 从 `DemoViewModel` 私有类型迁到 store owner。
- `DemoViewModel` 改为通过 store 读取 `hasPendingStart`、`hasPendingStop`、`hasPendingStopCompletion` 和 `hasPendingLifecycle`。
- 新增 `WavePendingLifecycleStoreTest`，覆盖 begin start、begin stop、ensure stop idempotent、consume stop、clear start / stop。

## 3. 抽象点

- `WavePendingLifecycleStore` 是纯状态 owner，只保存 pending request / completion，不发送命令，不读 UI state，不发布日志。
- `DemoViewModel` 继续负责 `client.send`、`viewModelScope`、truth refresh job、系统日志、UI notice 和 test session finish。
- `WaveLifecycleCommandGate` 继续只负责 start token invalidation，不和 pending store 合并，避免扩大控制链行为面。

## 4. 参数点

- `nowProvider` 注入到 store，测试可稳定验证 request timestamp。
- `beginStart(freq, intensity)` 显式保存用户实际发送的 wave 参数。
- `ensureStopRequestIf(condition)` 显式表达“只在正在输出或 session recording 等条件成立时创建 stop request”，并保证幂等。
- `consumeStop()` 一次性返回 request + completion，同时清空 stop lifecycle，避免 ViewModel 分散清理。

## 5. 验证结果

已通过：

```bash
cd tools/android_demo
./gradlew :app-demo:compileDebugKotlin
./gradlew :app-demo:testDebugUnitTest --tests com.sonicwave.demo.WavePendingLifecycleStoreTest
./gradlew :app-demo:testDebugUnitTest --tests com.sonicwave.demo.WaveControlStateReducerTest --tests com.sonicwave.demo.WaveLifecycleCommandGateTest
./gradlew :app-demo:testDebugUnitTest --tests com.sonicwave.demo.TestSessionBridgeTest
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest
./gradlew :app-demo:assembleDebug
git diff --check
```

执行结果均为 `BUILD SUCCESSFUL` 或无输出通过。曾并行运行多个 `:app-demo:testDebugUnitTest` 时出现一次 Gradle `binaryResultsDirectory` 输出目录竞争；顺序重跑后通过，判定为测试调度问题，不是业务测试失败。

既有 Gradle 环境观察项仍存在：

```text
Directory '/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home' ... does not exist
```

该提示不是本包新增失败。

运行页真机 smoke：

- Capture：`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260607_152955__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-quality-smoke__manual__ESP32_RUNTIME__demo_app_quality_smoke`
- 用户 stop summary：`B4 第二阶段 WavePendingLifecycleStore 运行页 Start Stop 体感通过`
- 专项 audit：`PARTIAL_PASS_EVIDENCE_GAP`
- B4 目标证据：通过候选。ESP32 串口采到 `WAVE:START`、`WAVE:STOP`、`STOP_SUMMARY`；Android focus log 采到持续 `SNAPSHOT` 和 `EVT:STREAM`；fatal runtime evidence 为 0。
- audit partial 原因：本次未采到完整 quality baseline 的连接实时可见、信息架构、校准 / motion sampling / device config 工具区人工 marker。它们不是 B4 `WavePendingLifecycleStore` 的运行页 Start -> Stop blocker。
- 后续完整 quality baseline 已通过：`20260607_154532` 覆盖 UI / 信息架构 marker，`20260607_161021` 使用 PlatformIO monitor 补齐运行页 ESP32 Start -> Stop 设备闭环。

## 6. 未覆盖范围

- 未迁移 `client.send`、BLE transport、ESP32 固件协议或 truth refresh coroutine。
- 未迁移完整 `observeClient()` event reducer。
- 未调整运行页 UI 文案、按钮布局或信息架构。
- 未跑完整 Demo APP quality baseline 的所有人工 marker。

## 7. 是否需要真机

已补运行页真机 smoke。B4 目标链路通过；完整 Demo APP quality baseline 已在后续 capture 中补齐，不再需要为了当前 B4 第二阶段重复同一 happy path。

## 8. 下一步建议

推荐提交 B4 第二阶段；不建议在同一包继续迁 command send、truth refresh job、完整 event reducer 或 BLE connection owner。
