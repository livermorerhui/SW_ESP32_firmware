# Demo APP Quality Baseline Smoke Prep

状态：真机 smoke 已复核通过
文档类型：阶段交付报告
适用范围：`tools/android_demo`
更新日期：2026-06-07
对应事项：`FW-OPT-021`

## 1. 本轮实际完成

- 完成 Demo APP 多包重构后的 quality baseline audit。
- 新增专项 capture 入口：`tools/demo_app_quality_smoke_capture.sh`。
- 新增专项 audit：`tools/demo_app_quality_smoke_audit.py`。
- 将 smoke 范围固定为连接、实时流、Start -> Stop、校准工具入口、motion sampling 启停、可选 device config 写入。
- 已完成真实手机 / ESP32 smoke 采集、补采和 AI 复核。
- 已将专项 capture wrapper 的 ESP32 串口采集默认入口从 `auto` 调整为 `platformio`，避免 macOS raw `stty` 采集乱码。

## 2. 改动点

- `tools/demo_app_quality_smoke_capture.sh`
- `tools/demo_app_quality_smoke_audit.py`
- `docs/system/esp32_firmware_optimization_priority_table.md`
- `reports/task_20260606_demo_app_refactor_master_plan.md`
- `reports/task_20260607_demo_app_quality_baseline_smoke_prep.md`

## 3. 抽象点

本轮新增的是测试证据 owner，不是业务 owner：

- `demo_app_quality_smoke_capture.sh` 负责固定 scenario、profile、用户步骤和 capture 生命周期。
- `demo_app_quality_smoke_audit.py` 负责复核 marker、Android BLE transport、SNAPSHOT / STREAM、ESP32 start / stop 串口证据和异常日志。

这些脚本不改 Demo APP ViewModel、BLE wire payload、ESP32 固件行为或正式 SW APP。

## 4. 参数点

- 默认 capture profile：`esp32_plus_normal`。
- 可通过 `--capture-profile esp32_base` 切换到 BASE bench。
- 可通过 `--android-serial` 指定手机。
- 可通过 `--esp32-port` 指定 ESP32 串口。
- 可通过 `--esp32-mode` 覆盖默认串口采集方式；当前默认使用 PlatformIO monitor。
- `device config` 写入是可选步骤，只在现场确认安全时执行。

## 5. 成熟方案对齐

本轮按 Android 官方真机调试 / logcat 思路和项目生产证据链执行：

- Android 真机测试通过 ADB 连接设备。
- 运行日志通过 logcat / capture 自动采集，而不是临时人工复制。
- 项目侧继续复用 `SW/scripts/device_test_capture.sh` 作为底座，专项脚本只封装 Demo APP smoke 的 profile / scenario / audit。

## 6. 验证结果

已通过：

```bash
bash -n tools/demo_app_quality_smoke_capture.sh
python3 -m py_compile tools/demo_app_quality_smoke_audit.py
tools/demo_app_quality_smoke_capture.sh guide
cd tools/android_demo
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
```

说明：Gradle 仍输出既有 JDK path warning：`/opt/homebrew/Cellar/openjdk@17/17.0.18/... does not exist`。本轮未新增该问题，且测试通过。

## 7. 不覆盖范围

本包未改：

- Demo APP UI 视觉和交互。
- `DemoViewModel`、stores、reducers 的业务行为。
- BLE command 时序。
- ESP32 BLE wire payload。
- 固件协议、校准算法或 MAX485 参数。
- 正式 SW APP 默认行为。
- SW 主仓 `device_test_capture.sh`。

## 8. 真机测试门禁

本轮已完成真机验证。

Capture：

- `/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260607_101316__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-quality-smoke__manual__ESP32_RUNTIME__demo_app_quality_smoke`
- `/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260607_154532__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-quality-smoke__manual__ESP32_RUNTIME__demo_app_quality_smoke`
- `/Users/r.w.hui/Desktop/SW/.artifacts/device-test-captures/20260607_161021__Redmi_Redmi_K30_Pro_Zoom_Edition__esp32_plus_normal__ESP32-demo-quality-smoke__manual__ESP32_RUNTIME__demo_app_quality_smoke`

专项 audit：

- `demo_app_quality_smoke_audit.md` verdict：`PASS_CANDIDATE`。
- `notes.md` 有 3 个用户确认 marker。
- `visual_evidence` 有 9 个截图 / window dump 旁证。
- Android 采到 `SonicWaveTransport=2005`、`SNAPSHOT=1091`、`EVT:STREAM=368`。
- ESP32 采到 `WAVE:START`、`WAVE:STOP`、`STOP_SUMMARY result=NORMAL stop_reason=MANUAL_STOP`、`MOTION_SAMPLE_MODE enabled=true`。
- `20260607_154532` 完整 UI baseline 采到 6 个用户确认点和 18 个 visual evidence 文件，覆盖默认型号页、固定顶部 Tab、型号页精简、校准页压缩、采样页、运行页和日志页；该轮 ESP32 串口采集不足，所以只作为 UI / Android BLE 侧证据。
- `20260607_161021` 使用 PlatformIO monitor 补采运行页 Start -> Stop，ESP32 串口采到 `WAVE:START=1`、`START_ALLOW=1`、`WAVE:STOP=1`、`STOP_REQUEST=1`、`i2s_stop=1`、`STOP_SUMMARY=1`，`STOP_SUMMARY result=NORMAL stop_reason=MANUAL_STOP`；Android 侧 `SNAPSHOT / EVT:STREAM` 持续存在，无 fatal runtime evidence。

观察项：

- `device config` 写入证据为 0；该步骤本来按现场安全条件可选。
- Android focus logcat stop 时进程已停止 / stale；本轮仍有 stop snapshot、transport、ESP32 串口和 visual evidence 旁证，所以不作为功能 blocker。若后续复现 UI 瞬态异常，先补采集底座或启用更完整 logcat。
- `20260607_160544` 曾用 raw `stty` 采集 ESP32 串口并出现乱码；后续 Demo APP quality smoke 默认改用 PlatformIO monitor。

最终结论：

- `20260607_154532` 的完整 UI / 信息架构 marker 与 `20260607_161021` 的 ESP32 Start -> Stop 设备闭环证据合并后，可判定 Demo APP quality baseline smoke 通过。
- 该结论不表示 `device config` 写入已覆盖，也不表示后续 UI 瞬态问题可不采证；它只覆盖当前 B2-B8 重构与 B10 信息架构阶段的 happy path baseline。

## 9. 下一步

- B9 不再阻塞 Demo APP 当前重构阶段。
- B10 信息架构精简已随本轮 smoke 通过。
- 后续若继续提升体验，优先继续做小 owner / 小 section 的可测试重构；不要直接开多 ViewModel、完整 event reducer 或 BLE owner 重构。
