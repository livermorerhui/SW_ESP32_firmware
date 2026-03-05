# SonicWave BLE Test Runner

本目录提供 SonicWave 固件的 BLE 自动化测试框架，覆盖：
- BLE 协议链路
- Wave 启停与状态
- Laser 标定基础链路
- Safety Interlock（含部分手工辅助项）

## 1. 安装依赖

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r tools/test_runner/requirements.txt
```

依赖说明：
- `bleak`: 跨平台 BLE 客户端（核心）
- `PyYAML`: 读取目标配置
- `rich`: 终端日志美化（可选）
- `pytest`: 可选，便于后续迁移为 pytest 组织形式

当前实现使用**纯脚本 runner**（非 pytest 主驱动），原因：
- 更适合 BLE 实机会话式流程（连接、断开、重连、事件等待）
- 单命令可生成统一会话报告目录

## 2. 配置目标设备

复制示例配置：

```bash
cp tools/test_runner/config/targets.example.yaml tools/test_runner/config/targets.yaml
```

按你的设备修改字段：
- `device_name`
- `service_uuid`
- `rx_char_uuid`
- `tx_char_uuid`
- `connect_timeout_s`
- `reconnect_retry`
- `notify_timeout_ms`
- `optional_serial_port`（可选）

## 3. 运行测试

### 3.1 全量执行

```bash
python tools/test_runner/run_tests.py --config tools/test_runner/config/targets.yaml
```

### 3.2 指定 suite

```bash
python tools/test_runner/run_tests.py --config tools/test_runner/config/targets.yaml --suite safety
```

可选 suite: `ble`, `wave`, `laser`, `safety`, `all`

## 4. 报告输出

每次运行生成目录：

`tools/test_runner/reports/<timestamp>/`

包含：
- `results.md`：测试结果摘要（通过/失败/跳过）
- `raw_log.txt`：原始时间戳日志
- `session.json`：结构化结果，可供二次分析

## 5. 新增测试用例

1. 在 `tools/test_runner/test_cases/` 新建或扩展 suite 文件。
2. 实现 `async def run(client, ctx)`，返回 case 结果列表。
3. 每个 case 返回字段：`id/suite/description/status/details/duration_s`。
4. 在 `run_tests.py` 的 `SUITES` 注册新 suite。

建议状态值：
- `PASS`
- `FAIL`
- `SKIPPED`（无真机条件或需手工配合）

## 6. 无设备连接时行为

- Runner 不会崩溃，会优雅退出并提示下一步。
- 仍会生成报告目录，便于回传审阅。
- 退出码：
  - `0`: 无 FAIL
  - `1`: 有 FAIL
  - `2`: 环境/连接问题（如找不到设备、配置错误）
