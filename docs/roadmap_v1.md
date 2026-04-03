# SonicWave Firmware v1.0 Roadmap

## 0. Current Stage Update

Status recorded on April 2, 2026:

- `Phase 1 Exit Decision = Passed`
- `Can Proceed to Phase 2 = Yes`
- `Phase 2 WP1 Skeleton = Passed`
- `Phase 2 WP2 Behavior Migration = Passed`
- `Phase 3 Script Experimental Line = Frozen`

Current stage:

- Phase 1 is closed
- Phase 2 stable baseline is the current firmware mainline
- SW repo alignment should continue against the non-script Phase 2 surface

Next stage:

- proceed to real-device validation and Phase 2 threshold/state convergence on the stable path
- proceed to SW repo aligned development without depending on Phase 3 script surfaces
- keep the Phase 3 script offload line frozen unless reopen conditions are satisfied on a separate experiment track

## 1. v1.0 目标与范围（Scope Freeze）

v1.0 的目标是交付一个可稳定运行、可追溯发布、具备基础安全联锁能力的固件版本。

Scope Freeze（冻结范围）：
- 保留并稳定当前主链路：BLE 控制、Wave 输出、Laser 监测、安全状态机。
- 以 `SystemStateMachine` 为唯一安全闸门，不引入绕过路径。
- 固件构建、测试自动化、CI/Nightly/Release 流水线可用。
- 文档体系齐备（架构/协议/安全/测试/DevOps）。

v1.0 不包含（超范围）：
- OTA 商用闭环（仅保留规划与接口预留）。
- 复杂多传感器融合算法（EEG/心率/血氧仅做扩展规划）。
- 云端远程管理平台。

## 2. 里程碑（F1–F5）与 DoD

## F1: 构建与发布基线
- 内容：PlatformIO 构建稳定、固件 artifact 可回收、Release 产物可校验。
- DoD：
  - `pio run` 在 CI 成功。
  - `firmware.bin` 可从 `.pio/build/*/firmware.bin` 收集。
  - Release 包含 `firmware.bin` 与 `firmware.sha256`。

## F2: 协议与控制链路稳定
- 内容：BLE 协议命令、ACK/NACK、事件编码行为可复现。
- DoD：
  - `CAP?`, `WAVE:*`, `SCALE:*` 指令链路可验证。
  - 非法指令返回 NACK。
  - 协议文档与实现一致。

## F3: 安全联锁闭环
- 内容：离位、断连、传感器异常、疑似摔倒触发停机。
- DoD：
  - 触发条件出现时进入 `FAULT_STOP`。
  - 输出停止且冷却窗口生效。
  - 安全验证步骤文档化。

## F4: 自动化测试与回归机制
- 内容：Python BLE test runner + 标准报告产物。
- DoD：
  - `tools/test_runner/run_tests.py` 可在无硬件场景优雅退出。
  - 有硬件时可执行 BLE/Wave/Laser/Safety suites。
  - 产出 `results.md/raw_log.txt/session.json`。

## F5: 文档与流程收敛
- 内容：开发、测试、发布、运维文档形成闭环。
- DoD：
  - README 导航完整且链接有效。
  - DevOps 文档覆盖 CI/Nightly/Release/HW Integration。
  - 路线图、测试计划、风险矩阵可供评审与追踪。

## 3. 风险矩阵（至少 6 类）

| 风险类别 | 典型风险 | 影响 | 缓解措施 |
|---|---|---|---|
| BLE 通信稳定性 | notify 延迟/丢包 | 控制与事件丢失 | 增加超时重试、报告原始日志、关键命令 ACK 校验 |
| 安全联锁漏触发 | 状态竞争导致未停机 | 高安全风险 | 强制状态机闸门、非 RUNNING 停波兜底、回归测试覆盖 |
| 传感器链路异常 | Modbus 连续失败 | 错误状态频发 | 失败去抖与重试策略、手工断链路验证流程 |
| 构建环境漂移 | 依赖版本波动 | CI 不稳定 | 缓存策略、固定 Python 版本、nightly 持续验证 |
| 文档与实现偏差 | 协议变更未同步文档 | 交接与测试失真 | 变更控制流程 + PR 检查文档更新项 |
| 发布资产不一致 | bin 与校验不匹配 | 发布不可追溯 | Release 自动生成 sha256 并同包上传 |
| 实机环境差异 | self-hosted 配置不一致 | 集成测试不稳定 | runner 标签管理、硬件接线基线文档化 |

## 4. Release 约定

- Tag 规则：`v*`（例如 `v1.0.0`）
- 发布产物：
  - `firmware.bin`
  - `firmware.sha256`
- 校验要求：
  - 通过 `sha256sum` 生成并随发布上传。
- Release Notes 模板至少包含：
  - 变更摘要（Change Summary）
  - 兼容性（Compatibility）
  - 已知问题（Known Issues）
  - 测试状态（Test Status）

## 5. 与现有文档的关联

- 系统架构：[`docs/architecture.md`](architecture.md)
- 协议定义：[`docs/protocol.md`](protocol.md)
- 安全设计：[`docs/safety_design.md`](safety_design.md)
- 测试计划：[`docs/test_plan.md`](test_plan.md)
- 自动化测试：[`docs/testing_automation.md`](testing_automation.md)
- DevOps 流程：[`docs/devops.md`](devops.md)

## 6. 变更控制

协议冻结后的变更流程：
1. 提案（Proposal）
- 在 Issue/PR 中明确变更动机、影响范围、回滚方案。

2. 评审（Review）
- 至少包含：固件负责人、测试负责人、文档负责人。
- 安全相关变更需额外进行安全评审。

3. 记录（ADR）
- 对关键决策形成 ADR（Architecture Decision Record）。
- 建议目录：`docs/adr/ADR-xxxx-<topic>.md`。

4. 发布控制
- 协议变更需更新 `docs/protocol.md` 和测试用例。
- 未更新文档与测试，不得合并到 release 分支。
