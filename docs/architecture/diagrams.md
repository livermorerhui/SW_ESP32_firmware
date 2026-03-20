# SonicWave Architecture Diagrams Index

## current/（当前有效图）

### 1. `system_master.svg`
- 路径：[docs/architecture/current/system_master.svg](current/system_master.svg)
- 谁该看：跨团队成员（Android / Firmware / Hardware / QA）。
- 什么时候看：做端到端联调、责任边界评审、验收说明时。
- 关注点：Control Plane / Data Plane / Safety Flow 三条链路与关键命令事件。

### 2. `firmware_runtime.svg`
- 路径：[docs/architecture/current/firmware_runtime.svg](current/firmware_runtime.svg)
- 谁该看：固件开发、代码审计、性能与并发排查人员。
- 什么时候看：修改状态机、任务调度、总线关系（CommandBus/EventBus）时。
- 关注点：`audioTask` / `laserTask` / BLE callbacks / `main loop`，以及 FSM 唯一启停闸门。

### 3. `android_mvvm.svg`
- 路径：[docs/architecture/current/android_mvvm.svg](current/android_mvvm.svg)
- 谁该看：Android 开发、SDK 维护、协议联调人员。
- 什么时候看：调整 Compose/MVVM 分层、SDK/transport 抽象、协议 fallback 策略时。
- 关注点：`UI -> ViewModel -> sonicwave-sdk -> sonicwave-transport -> BLE -> ESP32`，以及 `rawLines`/`events` 双通道。

### 4. `ble_sequence.svg`
- 路径：[docs/architecture/current/ble_sequence.svg](current/ble_sequence.svg)
- 谁该看：联调工程师、测试工程师、协议排障人员。
- 什么时候看：复现连接流程、命令 ACK/事件时序、安全联锁触发链路时。
- 关注点：连接订阅、`WAVE:SET/START/STOP`、Laser/Disconnect 安全联锁。

## archive/（历史归档）

- 路径示例：[docs/architecture/archive/v0_concept/system_concept.svg](archive/v0_concept/system_concept.svg)
- 定位：保留历史概念图与旧版本图，供演进回溯与变更对比。
- 使用原则：归档图不作为当前实现依据；实现判断以 `current/` 与 `src/` 为准。

## 更新规则

仅在以下“结构变化”发生时更新图谱：

1. 新增或移除模块（如新增传感模块、拆分 transport/sdk）。
2. 系统边界变化（Android/Firmware/Hardware 责任边界调整）。
3. 协议变化（命令、ACK/NACK、事件格式或语义变化）。
4. 链路变化（Control/Data/Safety 流向变化，或关键通路新增/删除）。

非结构性改动（文案、小参数调整）默认不改图。
