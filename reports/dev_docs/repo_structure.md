# 仓库结构说明（深度 2~3）

```text
.
├── docs/
│   ├── firmware_developer_guide.md
│   ├── architecture.md
│   ├── protocol.md
│   ├── hardware.md
│   ├── system_overview.md
│   ├── system_architecture.svg
│   ├── safety_design.md
│   ├── development_workflow.md
│   └── test_plan.md
├── include/
│   └── README
├── lib/
│   └── README
├── reports/
│   ├── dev_docs/
│   │   ├── developer_guide.md
│   │   ├── repo_structure.md
│   │   ├── doc_generation_summary.md
│   │   ├── diff_summary.md
│   │   ├── patch.diff
│   │   ├── doc_generation_summary_2.md
│   │   ├── diff_summary_2.md
│   │   ├── patch_2.diff
│   │   ├── doc_generation_summary_3.md
│   │   ├── diff_summary_3.md
│   │   ├── patch_3.diff
│   │   ├── doc_generation_summary_4.md
│   │   ├── diff_summary_4.md
│   │   ├── patch_4.diff
│   │   ├── doc_generation_summary_5.md
│   │   ├── diff_summary_5.md
│   │   └── patch_5.diff
│   └── firmware_audit/
│       ├── audit_report.md
│       ├── build_notes.md
│       ├── gitignore_recommendations.md
│       ├── diff_summary.md
│       └── patch.diff
├── src/
│   ├── config/
│   │   └── GlobalConfig.h
│   ├── core/
│   │   ├── Types.h
│   │   ├── CommandBus.h
│   │   ├── EventBus.h
│   │   ├── ProtocolCodec.h
│   │   ├── SystemStateMachine.h
│   │   └── SystemStateMachine.cpp
│   ├── modules/
│   │   ├── laser/
│   │   │   ├── LaserModule.h
│   │   │   └── LaserModule.cpp
│   │   └── wave/
│   │       ├── WaveModule.h
│   │       └── WaveModule.cpp
│   ├── transport/
│   │   └── ble/
│   │       ├── BleTransport.h
│   │       └── BleTransport.cpp
│   └── main.cpp
├── test/
│   └── README
├── .gitignore
├── platformio.ini
└── README.md
```

## 目录用途
- `src/`：固件核心代码。
- `src/config/`：全局阈值、引脚、协议版本等配置。
- `src/core/`：类型定义、命令总线、事件总线、协议编解码、状态机。
- `src/modules/`：硬件功能模块实现（波形输出、激光测重）。
- `src/transport/ble/`：BLE GATT 传输层与协议入口。
- `include/`：公共头文件预留目录（当前仅模板 README）。
- `lib/`：本地私有库预留目录（当前仅模板 README）。
- `docs/`：面向开发者的正式文档（开发指南、架构、协议、硬件、安全、工作流、测试计划、架构图）。
- `reports/`：审计与文档生成产物。
- `reports/firmware_audit/`：工程审计报告与构建记录。
- `reports/dev_docs/`：开发文档增强任务与配套 Artifact。
- `test/`：测试目录（当前仅模板 README）。
- `platformio.ini`：PlatformIO 工程入口与构建配置。
