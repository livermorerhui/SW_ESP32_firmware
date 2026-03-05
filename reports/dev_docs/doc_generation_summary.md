# 文档生成摘要

## 1. 本次扫描的关键文件
- 工程与构建：
  - `platformio.ini`
  - `README.md`
- 核心架构与协议：
  - `src/main.cpp`
  - `src/core/Types.h`
  - `src/core/CommandBus.h`
  - `src/core/EventBus.h`
  - `src/core/ProtocolCodec.h`
  - `src/core/SystemStateMachine.h`
  - `src/core/SystemStateMachine.cpp`
- 功能模块：
  - `src/modules/wave/WaveModule.h`
  - `src/modules/wave/WaveModule.cpp`
  - `src/modules/laser/LaserModule.h`
  - `src/modules/laser/LaserModule.cpp`
  - `src/transport/ble/BleTransport.h`
  - `src/transport/ble/BleTransport.cpp`
- 配置参数：
  - `src/config/GlobalConfig.h`
- 已有报告：
  - `reports/firmware_audit/audit_report.md`
  - `reports/firmware_audit/build_notes.md`
  - `reports/firmware_audit/gitignore_recommendations.md`

## 2. developer_guide.md 覆盖的模块
- 系统总览：SonicWave 设备角色、ESP32 职责、Android App 交互。
- 体系结构：`LaserModule`、`WaveModule`、`BleTransport`、`SystemStateMachine`、`EventBus`。
- 线程模型：主循环 + `I2S_Audio` + `LaserTask` + BLE 回调上下文。
- 安全机制：Interlock、Fault Stop、离位检测、传感器异常路径。
- BLE 协议：新协议、Legacy 协议、ACK/NACK、事件编码。
- 工程操作：构建、烧录、串口监控。
- 扩展开发：新增模块、接入 EventBus、接入状态机。
- 运维增强（本次新增）：
  - 开发者接手 Checklist
  - 参数与阈值说明
  - 常见维护任务

## 3. 无法确认或需后续补充（TODO）
- TODO: 硬件层的“振动停止”判定方式（示波器标准、电流阈值）尚未在仓库中形成统一测试规范。
- TODO: BLE App 侧完整指令/事件兼容矩阵（不同版本 Android/蓝牙栈）尚未提供。
- TODO: Modbus 从站设备型号差异与寄存器映射变体未在仓库文档中给出。
- TODO: Core0/Core1 最终绑定策略是否要调整，当前仅基于代码现状说明。
