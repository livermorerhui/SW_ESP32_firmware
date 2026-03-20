# SonicWave Development Workflow

## 1. 维护与开发工作流

## 1.1 基线准备
1. 拉取主分支并确认工程可编译。
2. 阅读以下核心文档：
   - `docs/firmware_developer_guide.md`
   - `docs/architecture.md`
   - `docs/protocol.md`
   - `docs/hardware.md`
   - `docs/system_overview.md`
   - `docs/safety_design.md`
3. 运行基础构建命令：`pio run`。

## 1.2 日常开发流程
1. 明确需求类型：模块开发 / 协议变更 / 参数调优 / 文档更新。
2. 先改设计文档，再改代码，再补测试与验证记录。
3. 提交前检查：
   - 编译通过
   - 关键安全场景回归
   - 文档更新完成

## 2. 模块边界规则

1. 禁止跨模块直接改内部状态变量。
2. 命令入口统一走 `CommandBus`。
3. 事件出口统一走 `EventBus`。
4. 安全相关动作统一走 `SystemStateMachine`。
5. 通信层（BLE）不承载业务决策，只做收发与编码。

示例：
- 正确：`BleTransport -> CommandBus -> Handler -> StateMachine/Module`
- 禁止：`BleTransport` 直接修改 `WaveModule` 内部运行变量绕过状态机。

## 3. 新增模块标准步骤

1. 在 `src/modules/<new_module>/` 创建模块文件。
2. 通过 `begin/startTask` 接入初始化流程（`main.cpp`）。
3. 如有对外数据，上报到 `EventBus`。
4. 如有安全风险输入，新增状态机事件接口并接入 `SystemStateMachine`。
5. 更新文档：
   - `docs/architecture.md`
   - `docs/system_overview.md`
   - 必要时更新 `docs/hardware.md`

## 4. 新增指令标准步骤

1. 在 `src/core/CommandBus.h` 增加命令类型。
2. 在 `src/core/ProtocolCodec.h` 增加解析与参数校验。
3. 在 `src/main.cpp` 的 `HubHandler` 增加处理逻辑。
4. 如需响应事件，更新 `EventBus`/`ProtocolCodec::encodeEvent`。
5. 更新协议文档 `docs/protocol.md` 与 README 文档导航说明。

## 5. 新增事件标准步骤

1. 在 `src/core/EventBus.h` 增加 `EventType`。
2. 在相关模块发布事件（`EventBus::publish`）。
3. 在 `src/core/ProtocolCodec.h` 增加编码。
4. 在 App 侧补充事件解码和 UI 处理。
5. 更新 `docs/protocol.md` 与 `docs/architecture.md`。

## 6. 文档更新触发条件

以下改动必须同步更新文档：
- 新增/修改 BLE 指令、ACK/NACK、事件格式。
- 新增/修改安全阈值参数（如 `LEAVE_TH`、`MIN_WEIGHT`、`STD_TH`、`WINDOW_N`、`FALL_DW_DT_SUSPECT_TH`）。
- 新增硬件器件、改引脚定义、改通信链路。
- 新增任务、修改核心绑定、调整状态机行为。

## 7. 交付检查清单

1. `README.md` 文档导航可点击且路径正确。
2. `docs/architecture/current/system_master.svg` 可在 GitHub 直接预览（完整系统视角）。
3. 协议、架构、安全文档与代码实现一致。
4. `reports/dev_docs/` 下生成对应变更报告与 patch 文件。
