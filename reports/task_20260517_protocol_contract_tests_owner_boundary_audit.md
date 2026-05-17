# ESP32 Protocol Contract Tests And Owner Boundary Audit

日期：2026-05-17

## 1. 本包目标

本包在 SW APP 重构期间独立推进，目标是锁住 ESP32 现有协议合同，并形成后续结构拆分边界。

本包不改变运行行为：

- 不改 `CAP? / SNAPSHOT / WAVE:* / EVT:* / ACK:* / NACK:*`。
- 不改 Android APP 消费逻辑。
- 不改 start / stop / safety action timing。
- 不改 measurement unavailable / degraded-start 语义。

## 2. 实现内容

### 2.1 ProtocolCodec host-side tests

更新 `tools/run_evaluator_unit_tests.py`：

- 扩展 Arduino stub，支持 `String`、`Serial.printf` 和必要字符串方法。
- 将 `core/ProtocolCodec.h` 纳入 host-side 编译。
- 新增 focused tests：
  - `CAP?` / `SNAPSHOT?`
  - `WAVE:SET / WAVE:START / WAVE:STOP`
  - `DEVICE:SET_CONFIG`
  - `DEBUG:DEGRADED_START`
  - `SAFETY:LEAVE_PROTECTION`
  - `DEBUG:FALL_STOP`
  - legacy `F/I/E`
  - `encodeSnapshot()` 当前 APP 依赖字段
  - `EVT:STREAM` valid / invalid payload
  - `EVT:STOP` / `EVT:SAFETY` reason / code / effect / state

### 2.2 Owner boundary audit

新增 `docs/system/esp32_firmware_owner_boundary_audit.md`：

- 记录 `HubHandler`、`BleTransport`、`LaserModule` 当前职责。
- 标出可安全抽取项。
- 标出禁止迁移项。
- 标出后续验证要求。

### 2.3 Priority table sync

更新 `docs/system/esp32_firmware_optimization_priority_table.md`：

- `FW-OPT-002` 标为已完成。
- `FW-OPT-003 / FW-OPT-004 / FW-OPT-005` 标为审计已完成，具体拆分仍待单独决策。

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

未执行 `esp32_plus_laser_sim`：本包未改 shared headers、simulator 源码或正式固件运行代码，按计划不触发条件构建。

## 4. 当前结论

- 协议合同测试已覆盖关键命令解析与事件输出字段。
- 三个偏宽 owner 已形成后续拆分边界。
- 下一步不应默认进入 BLE 或 safety 主链大拆；如果继续实现，优先考虑 `HubHandler` ACK builder 这类低风险内部抽取。
