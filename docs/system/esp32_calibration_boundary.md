# ESP32 Calibration Boundary

## 文档定位

本文说明当前 SonicWave 校准体系里，哪些职责应该留在 ESP32，哪些职责应该明确由 Demo APP 持有。

目标不是重写架构，而是防止边界继续漂移。

## 1. 目标边界

### Demo APP 应负责

- 校准流程交互
- 开始 / 停止录制
- 录点工作流
- 已记录点列表与散点图
- 线性 / 二次拟合
- 模型比较与选择
- 最终发起模型写回

### ESP32 应负责

- BLE / 设备通信执行
- 传感器采集
- 实时有效性判断
- 稳定体重生成
- 运行时安全 / 平台状态判断
- 使用当前部署模型进行实时重量计算
- 存储当前部署模型

## 2. 当前真实边界

### 当前已经在 Demo APP 的职责

- 校准点会话数据集
- 录制状态 UX
- 散点图
- 线性 / 二次拟合
- 模型比较摘要
- 模型选择 UI

### 当前已经在 ESP32 的职责

- `CAL:CAPTURE` 的有效性校验
- `CAL:GET_MODEL` / `CAL:SET_MODEL`
- 模型持久化
- 运行时模型求值
- 稳定态样本快照生成

## 3. 当前没有发现的 firmware overreach

本次审计确认，ESP32 当前没有做下面这些本应属于 APP 的事情：

- 没有维护完整校准点列表作为工作流主数据
- 没有执行线性拟合
- 没有执行二次拟合
- 没有比较线性和二次模型
- 没有推荐模型
- 没有承担校准可视化语义

## 4. 当前真正的边界混淆点

边界混淆主要集中在 `CAL:CAPTURE`：

- 从字面上看，像是“让固件记录一个校准点”
- 从真实行为看，它更像“请求固件返回一个经过运行时有效性校验的稳定样本快照”

这意味着：

- 它并没有让固件成为点集工作流的主拥有者
- 但它确实让固件影响了点是否能进入 APP 数据集

这个影响是合理的前提是：

- 固件只保留运行时完整性相关的轻量检查
- APP 继续拥有工作流解释和失败可见性

## 5. 哪些固件校验应保留

应保留在固件：

- 非法参考值保护
- 当前测量无效保护
- 稳定样本未锁定时拒绝返回“稳定快照”
- 稳定基线缺失保护
- 写入模型时的有限值 / 单调性保护

这些都直接关系到：

- 运行时数据完整性
- 模型部署安全性
- 设备返回结果是否可信

## 6. 哪些职责不要继续加到固件里

后续应避免把下面职责继续堆到 ESP32：

- 点集持久化
- 候选模型缓存
- 点集拟合
- 模型推荐
- 线性 / 二次比较逻辑
- 工程分析 UI 语义

## 7. 最小边界修正方向

最小安全方向是：

1. 保持固件为“运行时执行 + 轻量验证 + 模型执行端”
2. 保持 Demo APP 为“校准会话 + 点集 + 拟合比较 + 模型选择端”
3. 不新增固件侧点集存储
4. 不新增固件侧拟合或推荐
5. 以后如果清理协议语义，优先把 `CAL:CAPTURE` 解释成 stable snapshot request，而不是 workflow-owned record

## 8. 当前是否需要立刻改 firmware

本次审计结论是：

- 当前没有发现必须立即修改的 isolated firmware overreach bug
- 现阶段更需要的是边界文档与接口语义澄清

因此本次没有做 firmware 代码变更。

## 9. 下一阶段设计目标

下一阶段目标不是把 stable weight 从 firmware 拿走，而是把 calibration point capture ownership 明确前移到 Demo APP。

建议的边界是：

### Demo APP

- 直接记录 calibration point
- 直接持有点集
- 直接完成拟合、比较、选型

### ESP32

- 继续生成 live measurement
- 继续生成 stable weight
- 继续执行 deployed model
- 继续承担 runtime safety / platform-state logic

这意味着 firmware 未来不应再被 calibration workflow 视为“点能否被加入 APP 点集的唯一 owner”。

## 10. Stable weight 与 capture 的关系应如何调整

设计上建议从：

- stable weight 是 capture 的硬前提

调整为：

- stable weight 是 capture 的质量参考与辅助信号

这样更符合：

- APP 负责 workflow
- firmware 负责 runtime quality

## 11. Task-C.3 落地后的当前边界

Task-C.3 已经把主录点链路改成 APP-authoritative。

当前边界可以概括为：

### Demo APP

- 本地创建 calibration point
- 本地维护点集
- 本地驱动点表 / 散点图 / 拟合比较
- 本地决定何时把候选模型写回设备

### ESP32

- 继续提供 live measurement
- 继续提供 stable weight
- 继续执行 deployed model 的 runtime 计算
- 继续承担 safety / platform-state 逻辑
- 兼容保留 legacy `CAL:CAPTURE`

因此 ESP32 现在不再影响主点集是否增长，除非用户刻意走 legacy 设备稳定快照路径。

## 附记：运行时安全边界中的 FALL_SUSPECTED

2026-03-19 的 Audit-FALL-1 进一步确认：

- `FALL_SUSPECTED` 完全属于 ESP32 runtime safety 责任
- 其当前触发点不在 APP，而在 `LaserModule` 的运行时测量解释链路里

当前 fall path 的真实语义更接近：

- 运行中出现过快的模型派生体重变化

而不是：

- 一个已经充分确认的多信号跌倒分类器

因此后续如果要降低误报，应该优先在 firmware runtime safety 边界内做最小修正，而不是把这类判断错误地转移到 APP 侧。
