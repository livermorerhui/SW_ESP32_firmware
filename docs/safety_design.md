# SonicWave Safety Design

## 1. 安全设计原则

1. App 不能直接控制振动输出硬件。
2. 所有启动/停止动作必须先经过 `SystemStateMachine` 判定。
3. 任何安全异常优先级高于业务输出，必须立即进入停机路径。
4. `SystemStateMachine` 是唯一允许调用 `WaveModule.start()/stopSoft()` 的闸门。

说明：当前实现中 `WAVE:START` 与 Legacy `E:1` 启动都走 `requestStart()`，禁止直接 BLE 命令绕过安全闸门。

## 2. Safety Interlock 触发条件与动作

### 2.1 用户离开平台
- 触发条件：测重结果低于 `LEAVE_TH`。
- 触发入口：`LaserModule -> SystemStateMachine::onUserOff()`。
- 动作：切换 `FAULT_STOP`，禁止继续启动；主循环兜底停波。

### 2.2 BLE 断连
- 触发条件：连接状态由 connected 变为 disconnected。
- 触发入口：`BleTransport` 断连回调通知状态机 `onBleDisconnected()`。
- 当前默认策略：`RECOVERABLE_PAUSE`
- 当前默认动作：
  - 如果运行中，立即停波
  - 不自动续跑断连前会话
  - reconnect 后以最新停波态重新同步，再允许新的 start
- 可编译切换策略：
  - `WARNING_ONLY`
  - `RECOVERABLE_PAUSE`
  - `BLOCKING_FAULT`

### 2.3 传感器异常（Modbus 读取失败）
- 触发条件：`readInputRegisters` 返回非成功。
- 触发入口：`LaserModule -> SystemStateMachine::onSensorErr()`。
- 动作：切换 `FAULT_STOP`，输出停机。

### 2.4 疑似摔倒
- 触发条件：运行态下重量变化率超过 `FALL_DW_DT_SUSPECT_TH`。
- 触发入口：`LaserModule -> SystemStateMachine::onFallSuspected()`。
- 动作：切换 `FAULT_STOP`，输出停机。

## 3. FAULT_STOP、冷却时间与解除条件

参数来自 `src/config/GlobalConfig.h`：
- `FAULT_COOLDOWN_MS = 3000`
- `CLEAR_CONFIRM_MS = 1000`
- 相关判定阈值：`LEAVE_TH`、`MIN_WEIGHT`、`FALL_DW_DT_SUSPECT_TH`

行为说明：
1. 进入 `FAULT_STOP` 后，冷却窗口内拒绝启动（`NACK:FAULT_LOCKED`）。
2. 冷却结束后，需满足“离位持续确认窗口”才可回到 `IDLE`。
3. 回到 `IDLE` 后仍需按正常状态机路径再次进入 `ARMED/RUNNING`。

## 4. 参数与调参建议

- `LEAVE_TH`（默认 3.0）
  - 过低：离位反应慢。
  - 过高：可能误判离位。
- `MIN_WEIGHT`（默认 5.0）
  - 建议保持 `MIN_WEIGHT > LEAVE_TH` 形成滞回。
- `STD_TH`（默认 0.20）与 `WINDOW_N`（默认 10）
  - 联合控制稳定判定灵敏度和延迟。
- `FALL_DW_DT_SUSPECT_TH`（默认 25.0 kg/s）
  - 需基于真实样本逐步标定，不建议一次性大幅调整。

## 5. 推荐验证步骤（实机）

## 准备
1. 编译并烧录固件。
2. 连接 BLE，设置 `WAVE:SET` 并进入 `RUNNING`。
3. 确认振动平台已有输出。

## 测试 A：用户离开平台
1. 运行中让用户离开平台（重量跌破 `LEAVE_TH`）。
2. 预期：进入 `FAULT_STOP`，振动停止。

## 测试 B：蓝牙断开
1. 运行中主动断开 App BLE 连接。
2. 预期：
  - 当前默认 `RECOVERABLE_PAUSE` 构建下：振动停止，重连后不同步续跑旧会话，可重新发起新 start。
  - 如果切到 `BLOCKING_FAULT` 构建：进入 `FAULT_STOP`，冷却窗口内拒绝重启。

## 测试 C：传感器异常
1. 运行中断开激光传感器总线或制造 Modbus 读取失败。
2. 预期：触发 `onSensorErr()`，进入 `FAULT_STOP`，振动停止。

## 测试 D：疑似摔倒
1. 运行中制造快速重量变化场景（受控、安全前提下）。
2. 预期：触发 `onFallSuspected()`，进入 `FAULT_STOP`。

验收标准：
- 上述任一触发条件出现时，系统必须停止振动输出。
- 当前默认 `RECOVERABLE_PAUSE` 构建下：
  - BLE 断连后必须停止振动输出
  - reconnect 后不得保留旧 `RUNNING` 会话继续执行
  - reconnect 后应允许按当前 readiness 重新开始
- `BLOCKING_FAULT` 构建下：
  - 冷却窗口内必须拒绝重新启动。
