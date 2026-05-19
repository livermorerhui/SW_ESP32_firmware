# ESP32 BLE OTA 开发规划

状态：当前正式
文档类型：阶段计划 / 交付规划 / ADR
适用范围：`SW_ESP3_Firmware` OTA 开发、跨仓联调、发布包与真机验收
Owner：SW_ESP3_Firmware OTA / SW Android 设备接入
更新日期：2026-05-19
真相源：`docs/system/esp32_ble_firmware_ota_contract.md`、`docs/system/esp32_firmware_optimization_priority_table.md`、`src/ota/FirmwareOtaManager.*`
证据源：host-side evaluator tests、PlatformIO build、Android JVM tests、OTA capture 产物

## 1. 目的

本规划把 ESP32 BLE OTA 从“能升级”推进到“可稳定交付、可回退、可复盘、可发布”的工程闭环。

核心原则：按成熟工程的标准来指导开发。先定合同，再分阶段，再验证，再扩展；不要等功能堆大了再回头修边界。

这份规划回答四件事：

1. OTA 现在应该按什么顺序继续做。
2. 哪些能力是必须项，哪些只是可选增强。
3. 哪些边界必须冻结，哪些只允许在新合同里改。
4. 后续真机和发布验收应该看什么证据。

## 2. 当前基线

当前已冻结的正式基线是：

- 独立 OTA GATT service，不能复用业务 BLE 文本协议传二进制。
- 双 OTA 分区 + inactive slot 写入。
- `esp_ota_begin / esp_ota_write / esp_ota_end / esp_ota_set_boot_partition / esp_restart` 标准链路。
- v1 先保持 `write-with-response`，不把吞吐和合同改动混在一起。
- OTA 失败必须 abort，旧固件保持可启动。
- 重启后必须做版本确认，不允许只凭“传完 100%”算成功。
- `firmware.bin + firmware.sha256 + firmware_manifest.json` 的 release 包结构。

对应真相源：

- ESP32 OTA state、分区选择、image validation、boot partition 切换。
- `OTA status` / `ota_query` / post-reboot firmware identity。

## 3. 不动边界

本规划不改：

- `CAP? / SNAPSHOT? / WAVE:* / EVT:*` 业务 BLE 语义。
- 训练 session 主链和 `SystemStateMachine` 的最终 owner 语义。
- Wi-Fi OTA。
- 通过业务 BLE 传输固件二进制。
- 在没有新合同的情况下静默切换到 `WRITE_NO_RESPONSE`。
- 把版本确认、rollback confirmation 或发布策略藏进 UI 文案。

## 4. 规划原则

### 4.1 独立协议

OTA 只走独立 GATT service。业务 BLE 只保留运行控制和状态回读。

### 4.2 可靠回退

任何 OTA 失败都必须留在旧固件可启动状态，不允许覆盖当前运行镜像。

### 4.3 先稳定，再提速

v1 的目标是稳定可交付，不是极限吞吐。高吞吐只能作为 v2 可选项。

### 4.4 版本确认必须闭环

升级成功必须同时满足：

- ESP32 已切到目标 slot。
- ESP32 已重启。
- APP 已回连。
- 版本身份已确认。

### 4.5 可观测日志优先

每轮真机必须能对齐：

- 下载开始 / 完成
- `ota_begin`
- 传输进度
- `VERIFYING`
- `READY_TO_REBOOT`
- 重启
- 回连确认

### 4.6 发布包可信

release 包必须可校验、可追溯、可复核；签名和 anti-rollback 可作为后续加固，不作为 v1 基础可用性前提。

## 5. 分阶段规划

| 阶段 | 状态 | 目标 | 关键交付 | 验收门槛 |
| --- | --- | --- | --- | --- |
| Phase 0 合同冻结 | `done` | 冻结 OTA 协议、状态机、错误码、分区语义 | `esp32_ble_firmware_ota_contract.md`、OTA GATT UUID、control/data/status frame、双分区模型 | host-side / PlatformIO / Android JVM 合同测试通过 |
| Phase 1 v1 稳定闭环 | `done / current baseline` | 跑通下载 -> 传输 -> 校验 -> 切槽 -> 重启 -> 回连确认 | OTA manager、inactive slot 写入、abort / retry、post-reboot identity | 真机 happy path 通过，旧固件可回退 |
| Phase 2 证据闭环 | `in_progress` | 让日常测试和 release capture 可复盘 | capture marker、`ota_query`、状态汇总、失败码中文化、post-reboot 版本事件 | 任何一次真机都能从证据判断是哪一段失败 |
| Phase 3 高吞吐 v2（可选） | `deferred` | 在 v1 稳定后再提吞吐 | `WRITE_NO_RESPONSE`、`window_size`、`ack_interval_chunks`、窗口 ACK | 失败时必须能回退到 v1 |
| Phase 4 发布硬化 | `deferred` | 让 release 包更可信 | manifest 签名、rollback confirmation、anti-rollback、灰度 / 强制升级策略 | 不签名或签名错误的包不能进入正式发布链 |

## 6. 推荐开发顺序

### 6.1 先收 Phase 2

优先把证据闭环做扎实：

- 补齐 OTA capture 的 start / mark / stop 规则。
- 补齐 `ota_query` 和 post-reboot 版本身份确认。
- 让 `WRITE_FAILED / VERIFY_FAILED / BLE_DISCONNECTED / USER_CANCEL` 都能被清晰识别。

### 6.2 再守住 v1 稳定面

继续保持：

- `write-with-response`
- 双分区
- old image 仍可启动
- 训练主链和业务 BLE 不受污染

### 6.3 最后再评估 v2

只有满足以下条件，才进入高吞吐：

- v1 真机稳定。
- 失败回退稳定。
- capture 和日志可复核。
- APP / ESP32 对窗口 ACK 的合同已经写清。

### 6.4 发布硬化单独推进

签名、anti-rollback、灰度和强制升级是 release hardening，不应和 v2 吞吐优化混在一个包里。

## 7. 验证矩阵

### 7.1 v1 必测

- happy path
- wrong board
- wrong sha256
- oversized image
- BLE disconnect mid-transfer
- APP process restart during OTA
- 运行中升级拒绝

### 7.2 证据要求

- `ota_begin`
- `ota_status PREPARED`
- `ota_progress`
- `ota_status VERIFYING`
- `ota_status READY_TO_REBOOT`
- `ota_reboot`
- post-reboot version confirmation
- ESP32 串口和 Android OTA 日志可对齐

### 7.3 通过标准

- 旧固件保持可启动。
- 新固件必须完成版本确认闭环。
- 失败原因必须可定位到具体阶段。
- 不能只凭“传完了”写通过。

## 8. 接手顺序

后续接手者优先读：

1. `docs/system/esp32_ble_firmware_ota_contract.md`
2. `docs/system/esp32_ble_ota_development_plan.md`
3. `docs/system/esp32_firmware_optimization_priority_table.md`
4. `docs/firmware_developer_guide.md` 的 OTA 历史说明

不要把高吞吐当成第一目标。先把稳定、回退、版本确认和证据闭环做实，再谈提速。
