# ESP32 / SW APP / Demo APP Compatibility Notes

## 当前兼容事实

### formal SW APP 当前正式依赖

formal 当前分支真正消费的是：

- `STATE`
- `FAULT`
- `STABLE`
- `PARAM`
- `STREAM`

它当前还没有把 `STOP/SAFETY/BASELINE` 接成正式产品 owner。`PAUSED_RECOVERABLE` / `STOPPED_BY_DANGER` 目前仍是：

- `FAULT.reason`
- `ProductController`
- `SessionCoordinator`

这条链落出来的。

### Demo APP 当前正式依赖

Demo 当前同时依赖两层：

- 老兼容层：`STATE/FAULT/STABLE/PARAM/STREAM`、legacy `F:/I:/E:`、裸 CSV
- 新增强层：`STOP/SAFETY/BASELINE`

因此 current Demo 不是“可以只保留新语义”的状态。

## 当前为了先跑通交互，不应删除哪些输出

这轮绝对不要删：

- `EVT:STATE`
- `EVT:FAULT`
- `EVT:STABLE`
- `EVT:PARAM`
- `EVT:STREAM`
- 已有 `EVT:STOP`
- 已有 `EVT:SAFETY`
- 已有 `EVT:BASELINE`
- legacy `F:/I:/E:` 与裸 CSV 兼容

也不要改这些字段名：

- `baseline_ready`
- `stable_weight`
- `main_state`
- `stop_reason`
- `stop_source`

## 当前最推荐的最小兼容策略

如果当前目标只是先跑通 formal SW APP 的“律动离开”交互，最推荐的策略是：

- 保持 Demo 旧依赖不变
- 保持 current stop / state owner 不变
- 保持 user-left 当前落 `STATE IDLE` 的行为不变
- 仅让 `FAULT` 对 formal current branch 变得可读懂

换句话说，本阶段更像：

- 保留旧输出
- 增加兼容语义

而不是：

- 调整某条现有 owner 输出去替代其它旧链路

## 后续协议治理前，必须先完成什么

在做语义清洁化之前，至少要先完成：

1. formal SW APP 协议层接入 `STOP/SAFETY/BASELINE`
2. formal 侧 stop/pause owner 从 `FAULT.reason` 迁移到更稳定的 `STOP/SAFETY`
3. Demo / analyzer / export 的字段迁移方案明确下来

在这之前，不建议：

- 删除 `FAULT`
- 改 `STATE` 含义
- 移除 legacy `PARAM/STREAM` 兼容格式
- 把 current docs/test 的旧假设直接倒推成代码改动
