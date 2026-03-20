# Calibration Recording Ownership Audit

## 文档定位

本文说明 Demo APP 与 ESP32 固件之间，校准录点和模型拟合到底由谁负责。

目标不是解释理想设计，而是固定当前仓库中的真实实现语义。

## 1. 录点真正发生在哪里

用户点击 `记录校准点` 后，Demo APP 不会先在本地生成一个点。

当前真实链路是：

1. Demo APP 发送 `CAL:CAPTURE w=<参考重量>`
2. 固件在 `LaserModule::captureCalibrationPoint(...)` 中检查是否满足录点条件
3. 只有固件返回 `ACK:CAL_POINT ...`，APP 才会把该点追加到本地 `calibrationPoints`

因此：

- 点击按钮不等于已经录点
- 收到 `ACK:CAL_POINT` 才等于录点成功

## 2. 当前录点数据的 source of truth

当前实现不是单侧绝对权威，而是一个分层 source of truth：

- “本次录点是否成功” 以固件返回的 `ACK:CAL_POINT` 为准
- “界面上有哪些已记录点” 以 APP 本地 `UiState.calibrationPoints` 为准

补充说明：

- 固件当前会生成单次 capture 结果，但没有维护完整的点列表供 APP 回读
- APP 会把每个成功 ACK 追加到本地列表，用于点数、表格、散点图和本地拟合

## 3. 为什么会出现“点击后还是 0”

只要没有成功收到并处理 `ACK:CAL_POINT`，APP 就不会追加点，以下 UI 也都会保持不变：

- 已记录校准点计数
- 校准点表格
- 散点图

所以“还是 0”并不优先指向 UI 绑定失效，更高概率表示：

- 固件拒绝了本次录点
- 或者 ACK 没有成功到达 / 解析 / 进入状态更新链

本仓审计中，最高置信度原因是固件前置条件失败，而失败原因之前没有在录点区域直接显示。

## 4. 拟合真正发生在哪里

线性与二次拟合都由 Demo APP 本地完成，具体在：

- `tools/android_demo/sonicwave-protocol/.../CalibrationComparison.kt`

APP 会使用已成功追加到本地列表的点来计算：

- 线性拟合
- 二次拟合
- 误差摘要
- 单调性检查
- 散点图上的曲线

固件不负责根据录点集合自动拟合模型。

## 5. 固件负责什么

固件当前负责：

- 接收 `CAL:CAPTURE`
- 判断样本是否有效、是否稳定
- 生成 `ACK:CAL_POINT` 或 `NACK:<reason>`
- 存取当前部署模型
- 在运行时使用当前模型进行重量计算

固件不负责：

- 维护完整录点列表供 UI 查询
- 对点集合执行线性/二次拟合对比

## 6. 获取模型 / 写入模型 / CAL:ZERO 的真实含义

- `获取模型`：从固件读取当前已部署模型
- `写入模型`：把当前 UI 输入的模型参数写回固件
- `CAL:ZERO`：协议层兼容入口，当前会落到固件 `SCALE_ZERO`

这些都不是 APP 本地拟合按钮，也不是“录点成功”的确认动作。

## 7. 本次最小修复做了什么

本次审计没有重做录点架构，只做了两个最小改动：

- 在录点区域增加持久 `captureStatus`
- 增加 bounded `[CAL_AUDIT]` 日志，帮助区分：
  - 点击发生了没有
  - 指令发出没有
  - ACK/NACK 收到没有
  - 点被追加没有

这样后续真机验证时，可以更快判断问题是在前置条件、传输、解析，还是状态更新。

## 8. 从边界角度看，这个实现是否合理

从最新边界审计看，当前实现总体上是可接受的，但要避免误解：

- Demo APP 才是校准工作流与点集分析的主拥有者
- ESP32 不是拟合比较拥有者
- ESP32 在 `CAL:CAPTURE` 上承担的是“稳定样本验证 + 单次快照返回”职责

真正容易造成边界混淆的，不是固件偷偷做了拟合，而是：

- 命令名看起来像“固件录点”
- 用户界面里如果没有把失败原因解释清楚，就会误以为固件在掌控整个录点流程

后续如果再做边界清理，优先方向应是：

- 保持固件轻量验证
- 保持 APP 持有点集与拟合比较
- 不让固件新增点集存储、模型推荐、候选模型比较等职责

## 9. Task-C.2 之后 APP 如何表达这条边界

为了减少“录点为什么没成功”的误解，Demo APP 现在在录点区直接显示两类信息：

- 可见前置条件
- 最近一次录点结果

这样用户更容易理解：

- APP 在发起录点请求
- firmware 在决定当前稳定样本是否可接受
- 只有成功后，APP 才会把点追加到本地点表和散点图

也就是说，Task-C.2 不是改变 ownership，而是把既有 ownership 用界面讲清楚。

## 10. 下一阶段设计将如何进一步调整 ownership

下一阶段设计建议进一步前移 point capture ownership：

- 点击“记录校准点”时，由 Demo APP 直接创建本地 calibration point
- 该点至少包含：
  - 当前输入参考重量
  - 当前实时距离快照

可选附带：

- 当前实时体重
- 当前稳定体重
- 时间戳
- 质量标记

在这个设计下：

- APP 成为真正的 point-recording owner
- firmware 不再作为本地点集写入的最终门禁
- firmware 继续保留 live validity / stable-weight / deployed-model execution 职责

因此 current `CAL:CAPTURE` 更适合在过渡期内被理解为：

- optional engineering validation path
- 或 legacy stable snapshot path

而不是长期的主录点路径。

## 11. Task-C.3 之后当前仓内的实际 ownership

Task-C.3 已经把当前主录点路径前移到 Demo APP。

现在真实主路径是：

1. 用户点击 `记录校准点`
2. Demo APP 从当前 UI/live state 直接创建 `CalibrationPointUi`
3. 新点立即进入本地 `calibrationPoints`
4. 点数、表格、散点图、拟合比较同时使用这批本地点

也就是说，当前主 source of truth 已经进一步收敛为：

- calibration workflow 的点集 ownership = Demo APP 本地状态

firmware 仍然保留的相关职责是：

- 提供 live measurement
- 提供 stable weight
- 提供 deployed model runtime execution
- 在 legacy `CAL:CAPTURE` 路径上返回经过 device-side gating 的稳定样本

## 12. Task-C.3 之后 stable 与 capture 的关系

当前实现下：

- stable weight 仍可见
- stable flag 仍会随点一起记录成 metadata
- 但 stable 已不再是本地录点的必需前提

因此现在更准确的表达应当是：

- stable = 质量辅助信号
- capture = APP 主导的本地快照记录

## 13. Task-C.4 之后“主流程 ownership”如何表达

Task-C.4 进一步把主界面的 ownership 讲清楚了：

- APP 不只拥有点集和拟合
- APP 还拥有“选哪种模型作为待部署结果”的主流程

因此主界面的最终语义现在是：

- APP 记录点
- APP 比较模型
- APP 选择模型
- APP 准备待写入参数
- APP 发起写入模型

firmware 继续负责：

- 接收最终模型
- 存储最终模型
- 用最终模型做运行时重量计算

这比旧的“主界面同时摆出校准、写模型、低层命令”边界更清晰。
