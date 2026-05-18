# ESP32 Firmware A5 CalibrationRuntime Helper

状态：已完成本地实现与验证，未提交
日期：2026-05-18
类型：LaserModule 低风险瘦身 / calibration runtime pure helper

## 1. 本轮分类

本轮属于 ESP32 固件低风险整包开发，目标是抽取 `LaserModule` 内部校准运行态纯计算 helper。

目标链路：

- calibration model weight calculation。
- effective-zero fallback / runtime-zero clamp / locked zero selection。
- host-side focused tests。

不该动的层：

- BLE 线格式。
- Android APP 消费逻辑。
- `Preferences` 存储 key。
- calibration model validation / monotonic 判断。
- runtime-zero refresh eligibility / window 判断。
- stable / baseline / start-ready timing。
- safety / stop action。
- WAVE 输出时序。

## 2. 实际完成

新增：

- `src/modules/laser/CalibrationRuntime.h`
- `src/modules/laser/CalibrationRuntime.cpp`

迁移：

- `CalibrationRuntime::evaluateWeight(...)`
- `CalibrationRuntime::evaluateClampedWeight(...)`
- `CalibrationRuntime::computeUnlockedEffectiveZero(...)`
- `CalibrationRuntime::computeEffectiveZero(...)`

更新：

- `LaserModule::evaluateCalibrationWeight(...)` 改为委托 pure helper。
- `LaserModule::computeUnlockedEffectiveZeroDistance()` 改为委托 pure helper。
- `LaserModule::computeEffectiveZeroDistance()` 改为委托 pure helper。
- `tools/run_evaluator_unit_tests.py` 增加 focused tests，覆盖：
  - quadratic weight calculation。
  - negative weight clamp。
  - runtime-zero offset clamp。
  - runtime-zero disabled path。
  - calibration zero fallback。
  - locked effective-zero selection。

## 3. 抽象边界

`CalibrationRuntime` 只负责纯计算：

- 不持有 state。
- 不访问 `Preferences`。
- 不调用 `RuntimeZeroObserver`。
- 不决定何时刷新 runtime-zero。
- 不发布 BLE event。
- 不调用 `SystemStateMachine`。

`LaserModule` 仍负责：

- 校准模型加载 / 保存。
- legacy zero / scale 同步。
- runtime-zero reset / observe / refresh 的调用时机。
- stable / baseline / start-ready 行为。

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
- `python3 -m platformio run -e esp32s3` 通过，`CalibrationRuntime.cpp` 已被 PlatformIO 编译纳入。

PlatformIO 输出 `Obsolete PIO Core v6.1.18 is used`，属于本机多版本提示，不影响构建结果。

## 5. 未做事项

- 未进行真机测试。本包只抽 pure helper，不改变 runtime 行为，默认不需要本轮真机验证。
- 未迁移 runtime-zero refresh eligibility / window 判断。
- 未迁移 stable / presence / baseline owner。
- 未改 BLE 线格式。

## 6. 后续建议

如果继续做 LaserModule 瘦身，下一步建议先做审计计划，不直接实现 stable / presence 深拆：

- stable contract logging helper。
- baseline writeback evidence builder。
- calibration runtime diagnostics helper。

仍禁止在普通低风险包中迁移：

- `SystemStateMachine::setRuntimeReady()` 调用语义。
- `SystemStateMachine::setStartReadiness()` 调用语义。
- baseline latch / clear timing。
- `onUserOff()` / `onFallSuspected()` action timing。
