# ESP32 / SW APP / Demo APP Compatibility Notes

## 当前兼容事实

### formal SW APP 当前正式依赖

formal 当前分支已经正式消费：

- BLE 初始化链路 `connect -> notify -> CAP? -> ACK:CAP -> SNAPSHOT? -> SNAPSHOT`
- `ACK:CAP` 的 bootstrap truth：
  - `fw`
  - `proto`
  - `platform_model`
  - `laser_installed`
- `SNAPSHOT` / `EVT:WAVE_OUTPUT` 的 runtime truth：
  - `start_ready`
  - `baseline_ready`
  - `degraded_start_available`
  - `degraded_start_enabled`
  - `wave_output_active`
- `STOP`
- `SAFETY`
- `BASELINE`

同时它仍保留兼容链：

- `STATE`
- `FAULT`
- `STABLE`
- `PARAM`
- `STREAM`

当前结论不是“formal 还没接 `STOP/SAFETY/BASELINE`”，而是：

- `STOP/SAFETY/BASELINE` 已接入正式产品路径
- 旧 `STATE/FAULT/STABLE/PARAM/STREAM` 兼容链尚未删除
- 因此在迁移完全结束前，不能误删旧输出

### Demo APP 当前正式依赖

Demo 当前同时依赖两层：

- 老兼容层：`STATE/FAULT/STABLE/PARAM/STREAM`、legacy `F:/I:/E:`、裸 CSV
- 新增强层：`STOP/SAFETY/BASELINE`

因此 current Demo 不是“可以只保留新语义”的状态。

## 当前为了先跑通交互，不应删除哪些输出

这轮绝对不要删：

- `ACK:CAP` bootstrap 字段：
  - `fw`
  - `proto`
  - `platform_model`
  - `laser_installed`
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
- `stop_reason`
- `stop_source`
- `start_ready`
- `degraded_start_available`
- `degraded_start_enabled`
- `wave_output_active`

## 当前最推荐的最小兼容策略

如果当前目标是保持当前 formal SW APP 与 current delivery boundary 对齐，最推荐的策略是：

- 保持 `ACK:CAP` 只承载 bootstrap truth
- 保持 runtime truth 落在 `SNAPSHOT`
- 保持 control confirmation 落在 `EVT:WAVE_OUTPUT` / authoritative `SNAPSHOT`
- 保留旧 `STATE/FAULT/STABLE/PARAM/STREAM` 兼容链，直到 formal 产品 owner 明确下线它们

换句话说，本阶段更像：

- 冻结新旧 owner 分工
- 避免把 bootstrap/runtime/control plane 再次混写

而不是：

- 回退到 `ACK:CAP` feature dump
- 让 `STATE/STOP/SAFETY` supporting truth 再次充当 wave-output confirmation owner
- 在未完成迁移前删除旧兼容输出

## 后续协议治理前，必须先完成什么

在做语义清洁化之前，至少要先完成：

1. 明确 formal SW APP 何时下线 `STATE/FAULT/STABLE/PARAM/STREAM` 兼容链
2. 明确 `STOP/SAFETY/BASELINE` 与 `SNAPSHOT/WAVE_OUTPUT` 的最终 owner 范围
3. Demo / analyzer / export 的字段迁移方案明确下来

在这之前，不建议：

- 删除 `FAULT`
- 改 `STATE` 含义
- 移除 legacy `PARAM/STREAM` 兼容格式
- 把 current docs/test 的旧假设直接倒推成代码改动
