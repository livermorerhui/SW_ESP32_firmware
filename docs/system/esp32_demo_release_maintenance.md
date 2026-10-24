# ESP32 / Demo APP 当前维护与证据入口

状态：当前正式入口。
文档类型：维护索引 / 产品合同导航。
Owner：ESP32 与 Demo APP 维护者。
更新日期：2026-10-25。
真相源：用户最终规则、当前 main、设备正式状态和配置持久化。
证据源：下列源码/合同、SW 仓对应日志与发布凭据。

先看[跨仓维护总表](../../../SW/docs/system/firmware-release/esp32-demo-maintenance-entry.md)，再按任务进入本仓文件。总表负责当前剩余项；历史阶段报告不再担任当前状态入口。

| 任务 | 本仓入口 |
|---|---|
| 了解重构与模块职责 | [重构蓝图](../firmware_refactor_blueprint.md)、[运行架构](../architecture_runtime.md)（任务/传输参考；其中旧状态转换条件以当前 ready 合同和源码为准） |
| 型号与开始就绪 | [自主判断施工蓝图](esp32_demo_start_ready_independent_judgment施工蓝图.md)、[开始就绪合同](../start-readiness-contract.md) |
| Demo 操作和实现 | [Demo README](../../tools/android_demo/README.md)、[工程控制台说明](demo_app_engineering_console.md) |
| Base / Plus 持久化与旧镜像升级 | [DeviceConfigStore](../../src/modules/laser/DeviceConfigStore.cpp)、[LaserModule](../../src/modules/laser/LaserModule.cpp) |
| 默认 Upload / 1.1.3 / 工厂与 OTA 同源 | [产品构建说明](product_build_and_factory_export.md)、[SW 发布合同](../../../SW/docs/system/firmware-release/esp32-unified-upload-factory-contract.md) |
| 日志是否真的通过 | [专项采集说明](../../../SW/docs/system/Demo%20APP与ESP32开始就绪验收采集说明.md) |
| 整段成功、失败与 AI 候选 | [整段复盘](../../../SW/reports/2026-10-25-esp32-demo-factory-dialogue-retrospective.md)、[AI 资料卡](../../../SW/docs/system/native-translation-toolkit/model-assets/retrospectives/esp32-demo-factory-20261025.md) |

## 本轮稳定边界

- ESP32 是 `start_ready` 的唯一裁决源。Demo 的故障弹窗只通知，不授权；旧命令兼容解析/ACK 不等于旧授权仍控制 ready，也不能据此声称所有旧字段/setter已经删除。
- 型号页面选择不写设备；匹配回读后才切换正式型号及卡片。当前 SET 等待期卡片暂隐藏的源码差异、旧协议兼容证据缺口见总表 R1/R3，不以目标文字冒充已实现。
- 两保护都关可绕过站稳；任一开走保护链。CYD 的保护策略准入仍有边界，见总表 R2，不能借 APP 放行解决。
- 本次正式候选是 `SW-ESP32-1.1.3 / u113-77d0f0df57ec`；应用 SHA 以发布凭据为准。用户之前的 Base、波形、旧 1.1.2 OTA 反馈和 1.0.0 测试日志不自动覆盖该候选。
- 日志业务观测通过与完整性通过分开。后两轮采集为业务 PASS、总体 PARTIAL，不能称完整日志验收。
- 当前 main 不包含商业分期授权技术原型；历史归档分支保留并不等于污染主线。此结论和激光降级授权是两个不同主题。

后续按总表 R1–R7 对应范围补验证/修复。本文不要求重开已冻结的 SW APP、服务器、商业合约或其他 Demo 页面，也不把本次文档更新写成业务修复。
