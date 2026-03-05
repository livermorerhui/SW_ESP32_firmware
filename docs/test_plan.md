# SonicWave Firmware Test Plan

## 1 System Test Objective

本测试计划用于验证 SonicWave ESP32 固件在以下维度的功能正确性与安全性：
- BLE 通信：指令解析、ACK/NACK、事件上报是否正确。
- Wave 输出：I2S 音频链路是否稳定、频率/强度调节是否生效。
- Laser 测重：距离采样、重量换算、稳定判定是否正确。
- 安全联锁：离开平台、断连、传感器异常、疑似摔倒是否触发停机。
- 系统稳定性：长时间运行与高频交互下是否出现异常。

## 2 Test Environment

### 2.1 Hardware
- ESP32-S3
- PCM5102A
- TPA3255
- Laser Sensor
- Android App

### 2.2 Tools
- PlatformIO
- BLE 工具（手机 App 或 BLE 调试 App）
- 串口日志工具（`pio device monitor`）
- 示波器（可选，用于观察 I2S/输出波形）

## 3 BLE Protocol Test

测试指令：
- `CAP?`
- `WAVE:SET`
- `WAVE:START`
- `WAVE:STOP`
- `SCALE:ZERO`
- `SCALE:CAL`

验证项：
1. ACK/NACK
- 正确输入返回预期 `ACK:OK` 或 `ACK:CAP ...`。
- 错误输入返回 `NACK:INVALID_PARAM` / `NACK:UNKNOWN_CMD` 等。

2. 参数校验
- `WAVE:SET freq=<float> amp=<int>` 越界参数应拒绝。
- `SCALE:CAL z=<float> k=<float>` 缺字段应拒绝。

3. 错误处理
- 发送空命令、未知命令、格式错误命令，确认固件无崩溃且返回 NACK。

## 4 Wave Output Test

验证目标：
- DDS 正弦生成有效。
- I2S DMA 输出稳定。
- 频率变化可观察。
- 强度变化可观察。

测试频率：
- 10Hz
- 20Hz
- 40Hz

执行步骤：
1. 发送 `WAVE:SET freq=10 amp=40` + `WAVE:START`。
2. 观察输出链路（示波器或平台体感）是否稳定。
3. 分别切换 20Hz 与 40Hz，确认输出变化与设定一致。
4. 调整 `amp`（如 20/60/100），确认输出强度有明显变化。
5. 发送 `WAVE:STOP`，确认输出停止。

通过标准：
- 频率与强度变化响应正确。
- 长时间输出不出现明显卡顿/中断。

## 5 Laser Measurement Test

测试场景：
- 空平台
- 上人
- 稳定
- 离开

验证事件：
- `EVT:STABLE`
- `EVT:STREAM`
- `EVT:PARAM`

执行步骤：
1. 空平台观察 `EVT:STREAM` 基线。
2. 执行 `SCALE:ZERO`，确认收到参数更新相关事件（`EVT:PARAM`）。
3. 上人并保持稳定，确认触发 `EVT:STABLE:<weight>`。
4. 离开平台后，确认测量与状态变化符合预期。

通过标准：
- 事件格式正确且数据合理。
- 稳定体重可重复触发，误报率可接受。

## 6 Safety Interlock Test

测试项：
- 用户离开
- BLE 断开
- 传感器异常
- 疑似摔倒

预期：
- 进入 `FAULT_STOP`
- 振动停止

执行步骤（每项均需在 `RUNNING` 状态验证）：
1. 用户离开：让重量低于 `LEAVE_TH`。
2. BLE 断开：运行中断开 App 连接。
3. 传感器异常：断开/干扰 Modbus 读链路。
4. 疑似摔倒：制造快速重量变化触发可疑阈值。

通过标准：
- 任一触发项出现，系统立即停波并进入故障停机状态。

## 7 Fault Recovery Test

验证目标：
- `FAULT_STOP` 行为正确。
- 冷却窗口（`FAULT_COOLDOWN_MS`）生效。
- 满足恢复条件后回到 `IDLE`。

执行步骤：
1. 触发任一安全故障进入 `FAULT_STOP`。
2. 在冷却窗口内尝试 `WAVE:START`，应返回拒绝（如 `NACK:FAULT_LOCKED`）。
3. 冷却结束并满足清除条件后，确认状态恢复到 `IDLE`。
4. 重新走正常流程，确认可再次进入运行。

## 8 Stress Test

测试内容：
- 长时间运行（连续运行测试）
- 高频 BLE 指令（短时间连续发送）
- 频繁启动停止（快速重复 `START/STOP`）

建议执行：
1. 连续运行 30 分钟并观察串口日志。
2. 以较高频率发送 `WAVE:SET`/查询命令，观察 ACK 丢失率与状态稳定性。
3. 连续执行 100 次启动停止循环，观察是否出现锁死或异常状态。

## 9 Regression Test

每次版本升级必须回归：
- BLE
- Wave
- Laser
- Safety

建议最小回归集合：
1. 协议主指令 100% 覆盖（含错误输入）。
2. 三个频点（10/20/40Hz）与三档强度检查。
3. 安全联锁四场景全部复测。
4. FAULT 恢复流程复测。

## 10 Acceptance Criteria

发布前必须满足：
- 所有安全测试必须通过。
- 协议测试必须通过。
- 连续运行 30 分钟无异常。

