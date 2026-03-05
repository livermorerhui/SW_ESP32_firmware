# SonicWave Testing Automation Guide

## 1. 依赖安装

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r tools/test_runner/requirements.txt
```

> 依赖核心为 `bleak`（跨平台 BLE），其余用于配置与报告输出。

## 2. 配置 targets.yaml

1. 复制示例文件：
```bash
cp tools/test_runner/config/targets.example.yaml tools/test_runner/config/targets.yaml
```
2. 修改为你的设备信息（不要提交个人设备配置）：
- `device_name`
- `service_uuid`
- `rx_char_uuid`
- `tx_char_uuid`
- `connect_timeout_s`
- `reconnect_retry`
- `notify_timeout_ms`
- `optional_serial_port`（可选）

## 3. 运行全部自动化测试

```bash
python tools/test_runner/run_tests.py --config tools/test_runner/config/targets.yaml
```

## 4. 只运行某一组用例

```bash
# 仅运行 safety suite
python tools/test_runner/run_tests.py --config tools/test_runner/config/targets.yaml --suite safety

# 可选 suite: ble / wave / laser / safety / all
```

## 5. 报告输出位置

每次运行会在以下目录生成结果：

`tools/test_runner/reports/<timestamp>/`

包含：
- `results.md`
- `raw_log.txt`
- `session.json`

## 6. 如何回传给 ChatGPT 审阅

1. 运行自动化测试。
2. 打开最新时间戳目录（`tools/test_runner/reports/<timestamp>/`）。
3. 将以下文件路径发给 ChatGPT：
- `results.md`
- `raw_log.txt`
- `session.json`

若有失败项，建议同时附上串口日志或示波器截图路径。
