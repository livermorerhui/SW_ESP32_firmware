# ESP32 HubHandler ACK Builder Refactor

日期：2026-05-17

## 1. 目标

本包承接 `esp32_firmware_owner_boundary_audit.md` 的建议，只做 `HubHandler` 内部 ACK/NACK 拼装收口。

边界：

- 不改 `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*` 线格式。
- 不改 `SystemStateMachine` start / stop / safety action timing。
- 不改 Android APP 消费逻辑。
- 不改 BLE transport 行为。

## 2. 实现

新增：

- `src/HubAckBuilder.h`

职责：

- 只接收已经由 `HubHandler` / action owner 计算好的值。
- 只返回 `String` 类型的 `ACK:* / NACK:*` 文本。
- 不读取 `SystemStateMachine`、`LaserModule`、`WaveModule` 状态。
- 不调用任何 action owner。

修改：

- `src/main.cpp`
  - 删除局部 `appendKey*` 和 `buildSimpleNack` 拼装 helper。
  - `HubHandler` 保持 command dispatch / action owner 调用不变。
  - ACK/NACK 文本改由 `HubAckBuilder` 生成。

- `tools/run_evaluator_unit_tests.py`
  - 将 `HubAckBuilder` 纳入 host-side tests。
  - 锁住 CAP、DEVICE_CONFIG、DEGRADED_START、CAL_POINT、CAL_MODEL、CAL_SET_MODEL、FALL_STOP、LEAVE_PROTECTION、MOTION_SAMPLING、通用 OK/NACK 文本。

## 3. 验证

已执行：

```bash
git diff --check
python3 tools/run_evaluator_unit_tests.py
python3 -m platformio run -e esp32s3
```

结果：

- `git diff --check` 通过。
- 通过，输出 `evaluator unit tests passed`。
- `esp32s3` PlatformIO 构建通过。

本包不需要真机测试，因为不改变运行行为、BLE 线格式或 action timing。
