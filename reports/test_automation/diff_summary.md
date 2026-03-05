# Test Automation Diff Summary

## 1. 文件变更摘要

### 新增文档
- `docs/test_report_template.md`
- `docs/testing_automation.md`

### 新增自动化测试框架
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

### 新增审阅报告
- `reports/test_automation/audit_report.md`
- `reports/test_automation/diff_summary.md`
- `reports/test_automation/patch.diff`

## 2. 核心功能

- BLE 客户端：连接/断开/发送/订阅/等待事件/自动重连。
- 自动化执行器：支持全量与按 suite 执行，失败返回非 0。
- 报告生成：每次执行输出 `results.md`、`raw_log.txt`、`session.json`。
- 用例覆盖：BLE 协议、Wave 控制、Laser 基础链路、安全联锁。

## 3. 非源码修改说明

- 未修改固件业务逻辑源码（`src/` 下文件未改）。
