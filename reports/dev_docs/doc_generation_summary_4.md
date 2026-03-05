# 文档生成执行报告（四）

## 1. 扫描的关键文件

- 构建与工程入口
  - `platformio.ini`
  - `README.md`（旧内容）

- 核心实现与参数来源
  - `src/main.cpp`
  - `src/config/GlobalConfig.h`
  - `src/core/CommandBus.h`
  - `src/core/EventBus.h`
  - `src/core/ProtocolCodec.h`
  - `src/core/SystemStateMachine.h`
  - `src/core/SystemStateMachine.cpp`
  - `src/modules/wave/WaveModule.h`
  - `src/modules/wave/WaveModule.cpp`
  - `src/modules/laser/LaserModule.h`
  - `src/modules/laser/LaserModule.cpp`
  - `src/transport/ble/BleTransport.h`
  - `src/transport/ble/BleTransport.cpp`

- 现有文档（用于保持一致）
  - `docs/firmware_developer_guide.md`
  - `docs/architecture.md`
  - `docs/protocol.md`
  - `docs/hardware.md`
  - `docs/system_overview.md`
  - `reports/dev_docs/repo_structure.md`

## 2. 本次生成/更新文档

- 更新：`README.md`
- 新增：`docs/system_architecture.svg`
- 新增：`docs/safety_design.md`
- 新增：`docs/development_workflow.md`
- 更新：`reports/dev_docs/repo_structure.md`
- 新增：`reports/dev_docs/doc_generation_summary_4.md`
- 新增：`reports/dev_docs/diff_summary_4.md`
- 新增：`reports/dev_docs/patch_4.diff`

## 3. 无法从仓库确认的信息（TODO）

- TODO: PCM5102A 与 TPA3255 的具体接线引脚映射（仅确认了 ESP32 侧 I2S 引脚）。
- TODO: 激光传感器具体型号与寄存器扩展表（当前仅见 `REG_DISTANCE`）。
- TODO: 不同硬件版本（板卡 revision）引脚差异与功放增益配置表。
- TODO: 实机安全验证的定量指标（振动停止判据、电流/振幅阈值）尚未在仓库固化。
