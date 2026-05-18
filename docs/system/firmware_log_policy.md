# Firmware Log Policy

最后更新时间：2026-04-28

## 1. 目标

固件日志必须同时满足：

- 不阻塞或拖慢实时任务。
- 不在 BLE 断开、测量高频、stream 高频场景刷爆串口。
- 保留真机排障需要的关键证据。
- 保持现有 capture 脚本可识别的日志前缀。

## 2. 成熟方案对齐

参考原则：

- ESP-IDF 官方日志模型使用 tag、level、compile-time / runtime level 控制。
- FreeRTOS 日志建议避免应用任务被日志 I/O 阻塞。
- RTOS 任务优先级设计中，日志应属于低优先级或后台职责，不能抢占实时 I/O 与控制链路。

当前工程还大量使用 `Serial.printf`，因此第一阶段不直接大规模替换为 `ESP_LOGx`。原因是现有 capture、报告和跨端排障依赖固定文本前缀。

第一阶段成熟化策略：

- 保留现有关键日志前缀。
- 高频日志走统一 `FirmwareLogPolicy`。
- 关键事件即时打印。
- 非关键高频事件只打印周期摘要。
- debug 细节继续由显式开关控制。

## 3. 当前规则

### 关键事件

以下日志不应被普通节流吞掉：

- `EVT:STOP`
- `EVT:SAFETY`
- `EVT:FAULT`
- reconnect snapshot dirty / compensated
- enqueue failure
- reset / panic / Brownout / Guru

### 高频非关键事件

以下日志默认只能周期摘要：

- BLE 未连接时的 `send_skipped`
- `BLE_TX_PRESSURE`
- stream replace / suppress 类压力统计
- measurement diagnostic summary
- motion shadow observe-only summary

### BLE TX 当前实现

`FirmwareLogPolicy` 当前统一管理：

- `kBleTxSendSkipLogIntervalMs`
- `kBleTxPressureSnapshotIntervalMs`

当前行为：

- BLE 未连接时，非关键 `send_skipped` 约 2 秒最多打印一次。
- `BLE_TX_PRESSURE` 约 2 秒最多打印一次。
- critical frame 仍可即时触发 reconnect snapshot dirty。

## 4. 后续演进

后续如果继续成熟化，顺序应为：

1. 先把 BLE TX / measurement / motion shadow 的高频日志全部接到 `FirmwareLogPolicy`。
2. 再给日志加 tag / level 语义。
3. 最后再评估是否迁移到 ESP-IDF `ESP_LOGx`。

禁止第一刀直接全仓替换 `Serial.printf`，因为这会破坏 capture 兼容性和现有真机证据链。
