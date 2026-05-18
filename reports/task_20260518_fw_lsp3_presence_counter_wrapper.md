# ESP32 Firmware LSP-003 Presence Counter Wrapper

状态：已完成本地实现与验证，未提交
日期：2026-05-18
类型：LaserModule 低风险瘦身 / presence valid-sample counter wrapper

## 1. 本轮分类

本轮属于 ESP32 固件低风险整包开发。

目标链路：

- 有效测量样本下的 presence enter / exit confirm counter。
- `PresenceContractEvaluator` 输入组装。

相关链路：

- user presence mirror。
- `SystemStateMachine::setRuntimeReady` 调用点。
- `SystemStateMachine::onUserOff` 调用点。
- invalid measurement presence fallback。

不该动的层：

- `invalidPresenceSamples`。
- RUNNING 下 invalid presence 保护行为。
- `SystemStateMachine::onUserOff` 调用时机。
- `SystemStateMachine::setRuntimeReady` 调用时机。
- baseline latch / clear。
- occupied-cycle lock / release。
- BLE 线格式。
- Android APP 消费逻辑。

## 2. 实际完成

更新：

- `src/modules/laser/PresenceContractEvaluator.h`
- `src/modules/laser/PresenceContractEvaluator.cpp`
- `src/modules/laser/LaserModule.cpp`
- `tools/run_evaluator_unit_tests.py`
- `docs/system/esp32_firmware_optimization_priority_table.md`

新增 pure wrapper：

- `PresenceCounterInput`
- `PresenceCounterResult`
- `PresenceContractEvaluator::evaluateWithCounters(...)`

## 3. 抽象边界

`evaluateWithCounters()` 只负责：

- 按有效测量 weight 更新 enter / exit confirm count。
- deadband 时清空 enter / exit confirm count。
- 饱和到 `0xFF`。
- 调用既有 `PresenceContractEvaluator::evaluate(...)` 输出 presence decision。

它不负责：

- 写 `StableContractState`。
- 清空 `invalidPresenceSamples`。
- 记录 `[PRESENCE]` invalid exit 日志。
- 调用 `SystemStateMachine`。
- 执行 occupied-cycle lock / release。
- 处理 baseline latch / clear。

`LaserModule` 仍负责：

- 将 helper 输出写回 `presenceEnterConfirmCount` / `presenceExitConfirmCount`。
- 将 `decision.nextUserPresent` 写回 `stableContract.userPresent`。
- presence enter 后锁定 effective-zero。
- presence exit 后按原 call site 调用 `sm->onUserOff()`。
- 每轮按原 call site 调用 `sm->setRuntimeReady(...)`。
- invalid measurement 下的 presence fallback。

## 4. 测试覆盖

新增 host-side focused tests：

- enter pending。
- enter confirmed。
- exit pending。
- exit confirmed。
- threshold deadband reset。
- counter saturation。

## 5. 验证

已执行：

```bash
git diff --check
python3 tools/run_evaluator_unit_tests.py
python3 -m platformio run -e esp32s3
```

结果：

- `git diff --check` 通过。
- `python3 tools/run_evaluator_unit_tests.py` 通过。
- `python3 -m platformio run -e esp32s3` 通过。

PlatformIO 输出 `Obsolete PIO Core v6.1.18 is used`，属于本机多版本提示，不影响构建结果。

## 6. 行为影响

本包不需要真机测试：

- 未改 BLE 正式合同。
- 未改 start / stop / safety action timing。
- 未改 invalid measurement presence fallback。
- 未改 baseline / occupied-cycle action timing。

## 7. 后续建议

低风险 LaserModule helper 抽取到此基本收口。后续不建议自动进入：

- stable candidate owner。
- baseline latch owner。
- presence owner state carrier。
- occupied-cycle owner。

这些属于中风险动作时序迁移，必须先做单独审计和真机 capture 计划。
