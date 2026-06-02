# STREAM:SET 修复复盘

状态：阶段交付记录
文档类型：复盘 / 历史快照
适用范围：Demo APP 遥测曲线无数据、校准无实时数据、显式实时流订阅修复
Owner：ESP32 firmware / Demo APP engineering tools
更新日期：2026-06-02
当前替代入口：`docs/system/esp32_realtime_stream_subscription_contract.md`
证据源：本轮 commits `bf76f0a`、`ef9d9b8`、`93b1737`、merge `bc18a74`、真机 capture `20260602_152844...`

## 1. 真经验

### 1.1 用户体感是线索，不是根因

用户最初看到的是：

- 遥测曲线没有数据。
- 校准工具没有实时距离。
- 踩上去没数据，离开后才出现数据。
- MAX485 LED 偶尔约 2 秒停闪。

这些现象不能直接等同于“MAX485 坏了”或“校准算法错了”。本轮正确拆法是：

- 先确认固件测量还在跑。
- 再确认 BLE transport 是否收到 `EVT:STREAM`。
- 再确认 Demo APP 是否需要显式订阅。
- 最后才判断校准 UI 是否有输入。

### 1.2 APP 类型差异必须协议化

本轮最关键的工程判断是：Demo APP 和正式 SW APP 的实时流需求不同，但不能靠 APP 名称或连接来源在固件里猜。

成熟做法是定义显式合同：

- Demo APP 需要调试曲线和校准时发 `STREAM:SET enabled=1,rate_hz=10`。
- 正式 SW APP 不需要持续实时距离时不发。
- 固件只按命令和 session state 决定是否上行 `EVT:STREAM`。

这比“默认给所有 APP 推流”更可控，也比“固件识别 APP 类型”更稳定。

### 1.3 ACK 是正式握手证据

真机 audit 首次判了假失败，因为没有采到 Demo 的 `TX STREAM:SET` 原始行。

但日志里已经有：

- `ACK:STREAM enabled=1 supported=1 rate_hz=10`
- ESP32 `[STREAM_CONTROL] action=set enabled=1`
- Android 连续 `EVT:STREAM`

这些足以证明设备收到了订阅请求并执行成功。后续 audit 不能只依赖单一路 TX 原始日志；设备 ACK 和固件状态日志也是正式证据。

### 1.4 修复主问题后暴露的是证据缺口，不是新的固件 blocker

本轮最终 verdict 是：

```text
PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP
```

缺口是 Demo APP 没有结构化输出 `MEASUREMENT_CONSUME / CAL_*`，不是实时流链路仍失败。

后续要补的是 Demo 消费层日志和校准 UI 自动验收证据，不是继续改固件协议、校准算法或 MAX485 参数。

## 2. 目标链路和边界

目标链路：

```text
BLE connect
-> CAP?
-> ACK:CAP stream_control_supported=1
-> STREAM:SET enabled=1,rate_hz=10
-> ACK:STREAM enabled=1 supported=1 rate_hz=10
-> EVT:STREAM
-> Demo telemetry / calibration
```

相关链路：

- RS485 / Modbus 测量读取。
- `measurement_health`。
- baseline / stable weight。
- Demo APP 校准 UI。
- 正式 SW APP 控制链路。

真相源：

- ESP32 BLE 协议合同。
- `LaserModule` 的测量和 stream subscription state。
- `ACK:STREAM`。

证据源：

- Android logcat focus/full。
- ESP32 serial log。
- `session_meta.env` / `notes.md` / `warnings.log`。
- `esp32_demo_telemetry_calibration_audit.md`。

不该动的边界：

- 不改 `EVT:STREAM` wire payload。
- 不改校准算法。
- 不改 MAX485 / Modbus 参数。
- 不改 safety / stop 语义。
- 不让正式 SW APP 默认背上实时流订阅。

## 3. 规则分流

### 应进入 AGENTS 的长期规则

- ESP32 实时测量流是否上行必须通过正式协议订阅表达；禁止靠 APP 名称、设备名或连接来源猜测。
- 真机 capture 复核时，如果 TX 原始行缺失，但 ACK 和设备侧执行日志完整，不能直接判 handshake 失败。

### 应进入 docs 的长期规则

- `STREAM:SET` 合同、session 生命周期、参数范围、APP 消费边界和回归标准。
- Demo APP 与正式 SW APP 对实时测量流的不同需求。
- `ACK:CAP` 只能放静态能力，不能放当前订阅状态。

### 应留在 reports 的本轮历史

- MAX485 LED 偶发停闪描述。
- 本轮 RS485 transient 计数。
- `PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP` 这次 capture 的具体数字。
- audit 假阴性修复过程。

### 不需要升级为 skill 的内容

本轮没有发现需要修改通用 skill 的稳定缺口。`production-capture-evidence` 和 `delivery-retrospective` 的现有流程足够覆盖；问题在于执行时一开始误解了用户“需要检查日志是否证明通过”的意思，这属于本轮交互失误，保留在本报告即可。

## 4. 未来触发表

| 以后出现什么情况 | 应该做什么 | 不应该做什么 |
| --- | --- | --- |
| Demo 曲线或校准没有实时数据 | 跑 `tools/esp32_demo_telemetry_calibration_capture.sh`，看 `ACK:STREAM` 和 `EVT:STREAM` | 直接改校准算法或 MAX485 参数 |
| 正式 SW APP 想要实时距离 | 按 `STREAM:SET` 合同显式接入 | 要求固件恢复默认推流 |
| Audit 没采到 `TX STREAM:SET` | 检查 `ACK:STREAM` 和 ESP32 `[STREAM_CONTROL] action=set` | 只因 TX 缺失判失败 |
| MAX485 LED 偶尔停闪 | 看 `MEASUREMENT_DIAG ok/fail/max_read_ms` 和恢复事件 | 只凭 LED 判断协议失败 |
| 真机体感通过 | 继续复核 capture 产物后再收口 | 只凭“通过”直接写完整验收 |
| 真机 stream 通过但 UI 证据缺失 | 标记为 evidence gap，补 Demo 消费层结构化日志 | 继续扩大固件协议改动 |

## 5. 协作复盘

本轮整包推进是合适的：先修复主链，再做协议合同，再做真机 capture，再收口 main。

需要改进的是：

- 用户反馈“通过”后，AI 必须主动复核日志是否也证明通过，不能误解为“不要检查日志”。
- 证据复核结论要分层写清：
  - 体感通过。
  - 传输链路证据通过。
  - UI 消费层是否有结构化证据。
  - 哪些缺口只是 observation，不是 blocker。

## 6. 当前裁决

本轮目标已完成。

- 主 blocker：解除。
- 本地构建和单测：通过。
- 真机体感：通过。
- 真机日志：支持实时流链路通过。
- 剩余缺口：Demo APP 消费/校准 UI 结构化日志不足。

下一步不应继续改固件主链。若继续优化，应单独进入 Demo APP 代码质量和可观测性审计包。
