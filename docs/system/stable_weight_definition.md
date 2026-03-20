# Stable Weight Definition

## 文档定位

本文定义 SonicWave 下一阶段推荐的稳定体重含义与使用边界。

目标不是删除稳定体重，而是把它从“过于严格的录点硬门禁”调整为“实用的运行时质量概念”。

## 1. Stable Weight 应该是什么

稳定体重应被定义为：

- 在一段连续有效测量窗口内
- 当前体重估计波动已经收敛到可接受范围
- 经过轻量防抖确认后
- 输出的一个高质量运行时体重值

它本质上是：

- firmware 侧的 runtime quality signal

而不是：

- APP 录点工作流的唯一通行证

## 2. Stable Weight 应该何时开始评估

推荐在以下条件满足后开始评估：

1. 连续收到有效 live measurement
2. 当前负载超过最小有效载荷阈值
3. 流没有断开或明显失真

当前代码中的 `weight > MIN_WEIGHT` 思路可以保留，但建议改成更明确的“进入稳定评估阈值”参数。

## 3. 推荐的稳定判定思路

推荐使用：

- sliding window
- combined fluctuation check
- debounced confirmation

而不是只依赖单一瞬时条件。

### 推荐参数方向

- 采样基础：沿用当前约 5 Hz live measurement
- 窗口长度：10 到 12 个样本，约 2.0 到 2.4 秒
- 进入评估：连续有效样本 + 超过最小载荷阈值
- 稳定条件：
  - `stddev(weight)` 低于阈值
  - 同时 `max(weight) - min(weight)` 低于阈值
- 防抖确认：连续 2 到 3 个评估周期满足条件才真正进入 stable

## 4. 为什么要联合使用 stddev 和 range

只看标准差可能掩盖缓慢漂移。

只看 max-min 又可能被单个离群点影响过度。

联合使用更适合 SonicWave 当前阶段：

- 能容忍微小抖动
- 也能避免把明显漂移误判成稳定

## 5. 最终 stable weight 应如何计算

推荐输出值优先使用：

- median
- 或 trimmed mean

不推荐继续只用普通平均值作为唯一输出，因为：

- 微小尖峰或少量异常点会更容易拉偏结果

如果实现成本需要控制，过渡期也可以：

- 先保留平均值
- 再逐步切到 trimmed mean / median

## 6. Stable 应如何丢失

推荐在下面情况失去 stable：

- 连续无效样本超过 grace count
- 载荷降到离台阈值以下
- 当前窗口相对稳定基线出现持续性显著偏移

注意：

- 应使用持续偏移或多次确认来丢失 stable
- 不建议因为极小微抖动立即清除 stable

## 7. 为什么说当前 gate 对 calibration capture 太严格

当前 firmware stable 逻辑是：

- 10 样本窗口
- `weight > MIN_WEIGHT` 才开始
- `stddev(weight) < 0.20kg` 才 latch
- invalid sample 或条件变化会清 candidate

这对 runtime quality 是合理起点，但对于 calibration capture 来说问题在于：

- capture 当前必须依赖 `STABLE_LATCHED`
- 用户主观上已经觉得稳定时，系统仍可能迟迟不给 stable
- 距离微小波动经过模型斜率放大后，可能持续体现在 weight stddev 上

因此：

- 当前 stable 规则可以保留为 runtime signal
- 但不应继续作为 calibration point 录制的统一硬门禁

## 8. Stable Weight 在下一阶段的角色

下一阶段 stable weight 继续用于：

- runtime display
- runtime quality
- safety/platform logic
- calibration guidance

但不再作为：

- APP 录点的唯一准入条件

## 9. Task-C.3 之后 stable weight 的实际角色

Task-C.3 没有修改 firmware stable-weight 生成算法本身，但已经修改了它在 calibration workflow 里的地位。

现在 stable weight 的角色是：

- runtime display signal
- runtime quality/helper signal
- calibration capture 的可见上下文
- calibration point 的可选 metadata

它不再承担：

- 本地 calibration point append 的统一硬门禁

这使得系统边界更清晰：

- stable weight 继续偏 firmware/runtime
- point capture 继续偏 APP/workflow

后续如果继续优化 stable algorithm，应目标明确地服务于 runtime quality，而不是重新把它变回 APP 录点的唯一入口。
