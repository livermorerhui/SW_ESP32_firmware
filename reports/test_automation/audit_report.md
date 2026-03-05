# SonicWave Test Automation Audit Report

## 1. 本次新增文件

### 文档
- `docs/test_report_template.md`
- `docs/testing_automation.md`

### 自动化测试框架
- `tools/test_runner/README.md`
- `tools/test_runner/requirements.txt`
- `tools/test_runner/config/targets.example.yaml`
- `tools/test_runner/ble_client.py`
- `tools/test_runner/reporting.py`
- `tools/test_runner/run_tests.py`
- `tools/test_runner/test_cases/__init__.py`
- `tools/test_runner/test_cases/utils.py`
- `tools/test_runner/test_cases/test_ble_protocol.py`
- `tools/test_runner/test_cases/test_wave_output.py`
- `tools/test_runner/test_cases/test_laser_scale.py`
- `tools/test_runner/test_cases/test_safety_interlock.py`

### 审阅回传报告
- `reports/test_automation/audit_report.md`
- `reports/test_automation/diff_summary.md`
- `reports/test_automation/patch.diff`

## 2. 关键设计选择

1. 采用 `bleak` 作为 BLE 客户端
- 原因：跨平台（macOS/Linux/Windows）支持成熟，适合本地自动化测试。

2. 使用“纯脚本 runner（asyncio）”而非 pytest 主驱动
- 原因：BLE 会话测试天然是有状态流程（连接、订阅、命令、断连重连）。
- 优势：单入口执行与统一报告目录更直观。
- 同时保留 `pytest` 依赖，后续可迁移为 pytest 风格。

3. notify 处理策略
- `subscribe_tx()` 启动 TX notify。
- `wait_for_notify(predicate, timeout)` 按谓词等待目标响应，避免误匹配。
- `clear_notify_queue()` 在发命令前清空旧消息，降低乱序干扰。

4. 断连与重连策略
- `auto_reconnect()` 在操作失败时自动重连重试（基于 `reconnect_retry`）。
- `run_tests.py` 在连接失败时优雅退出，生成报告并给出下一步提示。

5. 测试组织策略
- suite 执行顺序：`BLE -> Wave -> Laser -> Safety`。
- 支持 `--suite` 仅执行某一组。
- 用例状态支持 `PASS/FAIL/SKIPPED`，适配“无真机条件”场景。

## 3. 风险与限制

1. 真机依赖
- BLE、Wave、Laser、Safety 核心验证需要真实设备在线。
- 无设备时 runner 会生成失败报告并退出码 `2`（环境问题）。

2. 手工辅助项
- 传感器异常、用户离开、疑似摔倒等场景缺少协议注入接口。
- 已在 safety suite 中标记为 `SKIPPED` 并输出手工步骤。

3. 事件时序不确定性
- BLE notify 可能存在延迟或丢包。
- 已通过超时、重试与谓词匹配减轻，但无法完全替代硬件级观测。

4. 串口协同
- 目前串口仅在配置中预留字段（`optional_serial_port`），未实现自动串口联测。
