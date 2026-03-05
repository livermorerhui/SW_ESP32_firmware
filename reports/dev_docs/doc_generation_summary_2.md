# 文档生成执行报告（二）

## 1. 本次扫描的源码与文档

- 架构与任务调度：
  - `src/main.cpp`
  - `src/core/SystemStateMachine.h`
  - `src/core/SystemStateMachine.cpp`
  - `src/modules/wave/WaveModule.h`
  - `src/modules/wave/WaveModule.cpp`
  - `src/modules/laser/LaserModule.h`
  - `src/modules/laser/LaserModule.cpp`

- 通信与协议：
  - `src/transport/ble/BleTransport.h`
  - `src/transport/ble/BleTransport.cpp`
  - `src/core/ProtocolCodec.h`
  - `src/core/CommandBus.h`
  - `src/core/EventBus.h`
  - `src/config/GlobalConfig.h`

- 现有文档与报告：
  - `docs/firmware_developer_guide.md`（仅参考，未修改结构）
  - `reports/dev_docs/repo_structure.md`

## 2. 本次生成的文档

- 新增：`docs/architecture.md`
- 新增：`docs/protocol.md`
- 更新：`reports/dev_docs/repo_structure.md`
- 新增：`reports/dev_docs/doc_generation_summary_2.md`
- 新增：`reports/dev_docs/diff_summary_2.md`
- 新增：`reports/dev_docs/patch_2.diff`

## 3. 新增章节覆盖

`docs/architecture.md` 新增章节：
1. 系统总体架构
2. 固件模块架构
3. 线程架构
4. 数据流
5. Safety Interlock 架构
6. 扩展架构

`docs/protocol.md` 新增章节：
1. GATT 服务结构
2. 控制指令
3. Legacy 协议
4. ACK / NACK
5. 事件
6. App 与固件通信流程

## 4. 说明

- `docs/firmware_developer_guide.md` 结构未改动。
- 本次为纯文档增量，无固件源码行为变更。
