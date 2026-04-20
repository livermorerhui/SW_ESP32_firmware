# SonicWave ESP32 Firmware
[![CI](https://github.com/livermorerhui/SW_ESP32_firmware/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/livermorerhui/SW_ESP32_firmware/actions/workflows/ci.yml)
[![Lint](https://github.com/livermorerhui/SW_ESP32_firmware/actions/workflows/lint.yml/badge.svg?branch=main)](https://github.com/livermorerhui/SW_ESP32_firmware/actions/workflows/lint.yml)

SonicWave 是一个基于 ESP32-S3 的理疗振动控制固件项目。系统通过 BLE 与 Android App 通信，结合 I2S 音频输出链路和激光测重链路，实现可控振动输出与安全联锁停机。

ESP32 在系统中的角色：
- BLE 通信网关：接收 App 指令并返回 ACK/NACK 与事件。
- 实时控制核心：驱动 Wave 输出（I2S）与 Laser 监测（UART/Modbus）。
- 安全治理中心：通过 `SystemStateMachine` 决定系统是否允许振动输出。

> [!WARNING]
> **Safety First**
> 所有振动输出必须经过 `SystemStateMachine`。App 不得绕过状态机直接启停振动。

## Quick Start

### 1) Build
```bash
# 如果 pio 已在 PATH
pio run

# 常见本机路径
~/.platformio/penv/bin/pio run
```

### 2) Upload
```bash
pio run -t upload --upload-port <PORT>
```

### 3) Monitor
```bash
pio device monitor -b 115200 --port <PORT>
```

## Android Demo (`tools/android_demo`)

### Open In Android Studio
1. Open Android Studio.
2. Choose `Open` and select `tools/android_demo`.
3. Set Gradle JDK to `17` in project settings.

### Build / Run
```bash
cd tools/android_demo
./gradlew :app-demo:assembleDebug
```

Install/run `app-demo` from Android Studio on a BLE-capable Android device.

### One-Click Validation Flow
Use the demo app controls in this order:
- `CAP?`
- `WAVE:SET f=<freq>,i=<intensity>`
- `WAVE:START`
- `WAVE:STOP`
- `SCALE:ZERO`
- `(Optional) observe EVT:STREAM / CSV`

Disconnect note:
- BLE disconnect may not always deliver a final `EVT` to app.
- In the current APP-driven delivery path, firmware default policy is:
  - disconnect stops wave output
  - reconnect does not auto-resume the previous session
  - app/device should resync from the latest stopped snapshot before a new start

## 关键特性

- BLE 控制协议：文本指令 + ACK/NACK + 事件上报
- I2S Wave Engine：DDS 正弦生成、DMA 输出
- Laser 监测链路：UART/Modbus 采样、稳定体重判定、离位检测
- Safety Interlock：离位/断连/传感器异常/疑似摔倒触发停机

## 文档导航

- 开发指南：[docs/firmware_developer_guide.md](docs/firmware_developer_guide.md)
- 系统架构（详细）：[docs/architecture.md](docs/architecture.md)
- 系统总览（简版）：[docs/system_overview.md](docs/system_overview.md)
- v1.0 路线图：[docs/roadmap_v1.md](docs/roadmap_v1.md)
- BLE 协议：**[docs/protocol.md](docs/protocol.md)**
- 硬件文档：[docs/hardware.md](docs/hardware.md)
- 安全设计：[docs/safety_design.md](docs/safety_design.md)
- 开发工作流：[docs/development_workflow.md](docs/development_workflow.md)
- 架构图索引（新）：[docs/architecture/diagrams.md](docs/architecture/diagrams.md)
- 架构图索引（兼容入口）：[docs/diagrams.md](docs/diagrams.md)

## Architecture Diagrams

- 图谱索引入口：[`docs/architecture/diagrams.md`](docs/architecture/diagrams.md)
- 系统总览图：[`docs/architecture/current/system_master.svg`](docs/architecture/current/system_master.svg)
- 固件运行图：[`docs/architecture/current/firmware_runtime.svg`](docs/architecture/current/firmware_runtime.svg)
- Android MVVM 图：[`docs/architecture/current/android_mvvm.svg`](docs/architecture/current/android_mvvm.svg)
- BLE 时序图：[`docs/architecture/current/ble_sequence.svg`](docs/architecture/current/ble_sequence.svg)

## 项目结构（简要）

- `src/`：固件源码（core/modules/transport）
- `docs/`：项目文档
- `reports/`：审计与文档生成报告
- `platformio.ini`：PlatformIO 构建入口

## DevOps / CI

- CI/CD 与 Release 流水线说明：[`docs/devops.md`](docs/devops.md)
- 自动化测试工具说明：[`docs/testing_automation.md`](docs/testing_automation.md)

## Roadmap

- SonicWave Firmware v1.0 路线图：[`docs/roadmap_v1.md`](docs/roadmap_v1.md)
