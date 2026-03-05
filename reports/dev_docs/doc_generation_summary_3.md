# 文档生成执行报告（三）

## 1. 本次扫描文件

- 硬件与引脚配置：
  - `src/config/GlobalConfig.h`

- 架构与模块关系：
  - `src/main.cpp`
  - `src/core/CommandBus.h`
  - `src/core/EventBus.h`
  - `src/core/SystemStateMachine.h`
  - `src/core/SystemStateMachine.cpp`
  - `src/modules/wave/WaveModule.h`
  - `src/modules/wave/WaveModule.cpp`
  - `src/modules/laser/LaserModule.h`
  - `src/modules/laser/LaserModule.cpp`
  - `src/transport/ble/BleTransport.h`
  - `src/transport/ble/BleTransport.cpp`

- 现有文档结构：
  - `docs/firmware_developer_guide.md`
  - `docs/architecture.md`
  - `docs/protocol.md`
  - `reports/dev_docs/repo_structure.md`

## 2. 本次生成/更新文件

- 新增：`docs/hardware.md`
- 新增：`docs/system_overview.md`
- 更新：`reports/dev_docs/repo_structure.md`
- 新增：`reports/dev_docs/doc_generation_summary_3.md`
- 新增：`reports/dev_docs/diff_summary_3.md`
- 新增：`reports/dev_docs/patch_3.diff`

## 3. 新增章节概览

`docs/hardware.md`：
1. 硬件系统组成
2. 硬件连接拓扑
3. ESP32 引脚定义
4. 音频链路
5. 测重链路
6. 安全联锁

`docs/system_overview.md`：
1. 系统整体结构
2. 软件架构
3. 数据流
4. 安全机制
5. 扩展能力

## 4. 约束说明

- 未修改 `docs/firmware_developer_guide.md`、`docs/architecture.md`、`docs/protocol.md` 的结构。
- 本次仅文档新增与文档更新，无固件代码行为变更。
