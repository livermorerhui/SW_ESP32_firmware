# Demo APP TestSessionBridge Stage 1

状态：第一阶段完成 / B6 运行页轻量 smoke 已通过候选
文档类型：阶段交付报告
适用范围：`tools/android_demo/app-demo`
更新时间：2026-06-07

## 1. 本轮判型

本轮属于 Demo APP 整包重构。目标链路是运行页测试会话 start / append sample / finish / export metadata 与 formal wave truth 的桥接状态。相关链路是 `WaveControlStateReducer`、`WaveLifecycleCommandGate`、measurement display、test session panel 和运行页 Start / Stop 体感。

真相源：

- `DemoViewModel` 中既有 test session 行为。
- `TestSessionManager` 作为 session data owner。
- `SessionLogEvent.TestStart` / `SessionLogEvent.StopSummary` 作为固件日志事件证据。

证据源：

- `TestSessionBridgeTest` focused JVM tests。
- `:app-demo:testDebugUnitTest`。
- `:app-demo:assembleDebug`。
- Demo APP quality smoke capture `20260607_150634...demo_app_quality_smoke`。

不该动的层：

- ESP32 固件协议和 BLE wire payload。
- `client.send(Command.WaveStart / WaveStop)` 时序。
- `WaveLifecycleCommandGate` token 语义。
- `TestSessionExporter` 导出格式。
- Demo APP 信息架构和 UI 视觉。

## 2. 改动点

- 新增 `TestSessionBridge`，承接 test session start、clear、export metadata mark、sample append、finish、`TEST:START` / `STOP_SUMMARY` 接入、formal inactive truth stop plan 和 sample build。
- `DemoViewModel` 改为通过 `TestSessionBridge` 更新 `testSessionStore`，自身继续负责编排协程、发送命令、日志、notice 和 panel publish。
- 新增 `TestSessionBridgeTest`，覆盖 start、append、finish、inactive truth stop plan、start/finish gate 和 clear gate。

## 3. 抽象点

- `TestSessionBridge` 是运行页测试会话与 formal wave truth 之间的窄 owner。
- `TestSessionManager` 继续负责 session data 的创建、finish 和 log event apply。
- `DemoViewModel` 继续是屏幕级 orchestrator，不把 BLE command、truth refresh 或 exporter IO 迁入 bridge。

## 4. 参数点

- `nowProvider` 注入到 `TestSessionBridge`，focused tests 可稳定验证 started / finished timestamp。
- `SessionCaptureSignals` 作为 bridge 的输入参数，避免 bridge 直接读取 ViewModel 字段。
- `fallbackFreq` 显式传入 `resolveFormalSessionFrequency()`，避免 bridge 依赖 UI state。
- `trackTestSessions` 显式传入 `applyStopSummary()` / `shouldStartFromFormalTruth()`，保持是否追踪测试会话的判断在调用侧可见。

## 5. 验证结果

已通过：

```bash
cd tools/android_demo
./gradlew :app-demo:testDebugUnitTest --tests com.sonicwave.demo.TestSessionBridgeTest
./gradlew :app-demo:testDebugUnitTest
./gradlew :app-demo:assembleDebug
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest
git diff --check
```

执行结果均为 `BUILD SUCCESSFUL` 或无输出通过。Gradle 仍提示既有的 JDK 路径不存在：

```text
Directory '/opt/homebrew/Cellar/openjdk@17/17.0.18/libexec/openjdk.jdk/Contents/Home' ... does not exist
```

该提示是环境配置观察项，不是本包新增失败。

轻量真机 smoke：

- Capture：`/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260607_150634__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-quality-smoke__manual__ESP32_RUNTIME__demo_app_quality_smoke`
- 用户 stop summary：`B6 TestSessionBridge 运行页 Start Stop 体感通过`
- 专项 audit：`PARTIAL_PASS_EVIDENCE_GAP`
- B6 目标证据：通过候选。ESP32 串口采到 `WAVE:START`、`WAVE:STOP`、`STOP_SUMMARY`；Android focus log 采到 `SNAPSHOT` 和 `EVT:STREAM`，无 fatal runtime evidence。
- audit partial 原因：本次未采到信息架构、连接后实时可见、校准 / motion sampling / device config 工具区的人工 marker。它们属于完整 quality baseline smoke 的覆盖缺口，不是 B6 `TestSessionBridge` 运行页 Start -> Stop blocker。
- 采集工具修复：`tools/demo_app_quality_smoke_capture.sh stop "SUMMARY"` 现已兼容用户自然写法，等价于 `stop --result pass --summary "SUMMARY"`；`stop --help` 不再误判为 stop 失败。

## 6. 未覆盖范围

- 未做完整 quality baseline smoke 的所有人工 marker。
- 未改 `client.send`、BLE transport、固件行为或 exporter 格式。
- 未把 `SessionCaptureSignals` 自身抽成 owner。
- 未迁移完整 `observeClient()` event reducer。

## 7. 是否需要真机

已补 B6 运行页轻量 smoke。若后续要给完整 Demo APP quality baseline 写“全量通过”，还需另跑或补 marker 覆盖：

- 信息架构：默认型号页、顶部 Tab 固定、内容不重复。
- 工具区：校准入口、motion sampling、可选 device config / protection switches。
- 人工 marker：连接后实时数据可见、Start -> Stop 体感、工具区 smoke。

## 8. 下一步建议

推荐先提交 B6 第一阶段和 capture wrapper 修复。通过后不建议继续扩大 B6，下一包才评估 B4 第二阶段 pending lifecycle / formal session action 是否值得做。
