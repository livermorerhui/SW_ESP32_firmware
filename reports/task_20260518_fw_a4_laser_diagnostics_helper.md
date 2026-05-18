# ESP32 Firmware A4 Laser Diagnostics Helper

状态：已完成本地实现与验证，未提交
日期：2026-05-18
类型：LaserModule 低风险瘦身 / diagnostics owner 抽取

## 1. 本轮分类

本轮属于 ESP32 固件低风险整包开发，目标是抽取 `LaserModule` 内部纯串口 evidence helper。

目标链路：

- `LaserModule` 可读性和 owner 边界。
- measurement probe 串口 evidence。
- distance valid / invalid diagnostics。

不该动的层：

- measurement 读取节奏。
- `MeasurementAvailabilityProbePolicy` 状态机。
- `MeasurementHealthStateMachine` 行为。
- `EVT:STREAM` BLE 输出语义。
- stable / baseline / start-ready timing。
- safety / stop action。
- Android APP 消费逻辑。

## 2. 实际完成

新增：

- `src/modules/laser/LaserDiagnostics.h`
- `src/modules/laser/LaserDiagnostics.cpp`

迁移：

- `MEASUREMENT_PROBE event=skip/probe/reset/open/still_unavailable/recovered` 串口 evidence。
- `[LASER] VALID ...` 串口 diagnostics。
- `[LASER] INVALID ...` 串口 diagnostics。

保留在 `LaserModule` 的职责：

- 何时调用 diagnostics。
- measurement read / skip / probe 调度。
- validity 状态节流：`lastMeasurementValid`、`lastInvalidReason`、`lastValidityLogMs`。
- BLE `EVT:STREAM` 发布。
- health / probe / stable / start-ready / safety 行为。

## 3. 抽象点

`LaserDiagnostics` 是无状态 helper：

- 只格式化和输出串口 evidence。
- 不持有 state。
- 不调用 `EventBus`。
- 不调用 `SystemStateMachine`。
- 不读写 `LaserModule` runtime 状态。

## 4. 验证

已执行：

```bash
git diff --check
python3 tools/run_evaluator_unit_tests.py
python3 -m platformio run -e esp32s3
```

结果：

- `git diff --check` 通过。
- `python3 tools/run_evaluator_unit_tests.py` 通过，输出 `evaluator unit tests passed`。
- `python3 -m platformio run -e esp32s3` 通过，`LaserDiagnostics.cpp` 已被 PlatformIO 编译纳入。

PlatformIO 输出 `Obsolete PIO Core v6.1.18 is used`，属于本机多版本提示，不影响构建结果。

## 5. 未做事项

- 未进行真机测试。本包不改变 runtime 行为，默认不需要本轮真机验证。
- 未继续抽 stable / presence / baseline owner。
- 未拆 `BleTransport`。
- 未改变 BLE 线格式。

## 6. 后续建议

下一包如果继续 A4 方向，可以继续抽：

- stable contract logging helper。
- baseline writeback evidence builder。
- calibration runtime diagnostics helper。

但不能在同包迁移：

- `SystemStateMachine::setRuntimeReady()` 调用语义。
- `SystemStateMachine::setStartReadiness()` 调用语义。
- baseline latch / clear timing。
- `onUserOff()` / `onFallSuspected()` action timing。
