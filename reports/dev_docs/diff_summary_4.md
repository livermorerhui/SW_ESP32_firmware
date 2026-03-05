# Diff 摘要（四）

## 1. 本次新增/修改文件

- 更新 `README.md`
- 新增 `docs/system_architecture.svg`
- 新增 `docs/safety_design.md`
- 新增 `docs/development_workflow.md`
- 更新 `reports/dev_docs/repo_structure.md`
- 新增 `reports/dev_docs/doc_generation_summary_4.md`
- 新增 `reports/dev_docs/diff_summary_4.md`
- 新增 `reports/dev_docs/patch_4.diff`

## 2. README 新增章节

- 项目简介（SonicWave + ESP32 角色）
- Quick Start（build/upload/monitor）
- 关键特性（BLE/I2S/Laser/安全联锁）
- 文档导航（链接到 docs 全部核心文档）
- Safety First 警示段落（必须经过 `SystemStateMachine`）

## 3. system_architecture.svg 包含组件

- Android App -> BLE -> ESP32 Firmware 主链路
- BleTransport / ProtocolCodec
- CommandBus / EventBus
- SystemStateMachine（安全闸门）
- WaveModule（I2S -> PCM5102A -> TPA3255 -> 平台）
- LaserModule（UART/Modbus -> 激光传感器）

## 4. 非文档类文件变更说明

- 本次未修改任何非文档类文件（未改动固件业务逻辑源码）。
- 仓库中存在本次任务前的既有源码改动，本次未触碰。
