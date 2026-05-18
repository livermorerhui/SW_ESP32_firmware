# ESP32 固件 BLE 安全重构冻结清单

日期：2026-04-21

## 1. 目的

这份清单只回答一件事：

**在当前正式交付仍是 `APP_DRIVEN_SESSION` 的前提下，ESP32 固件做内部优化或重构时，哪些 BLE 行为默认绝不能动。**

它用于：

- firmware 内部重构前冻结边界
- Demo APP / SW APP 联调前做回归核对
- 避免把 future `DEVICE_OWNED_SESSION` 假设偷偷混入当前正式链路

它不用于：

- 设计新的 device-owned session
- 宣布正式协议升级
- 替代单次任务报告

## 2. 当前正式前提

当前正式交付前提必须先固定：

- 正式 execution model：`APP_DRIVEN_SESSION`
- SW APP owner：
  - session lifecycle
  - start / pause / stop
  - countdown
  - 会话 UI 状态
- ESP32 owner：
  - 参数执行
  - 波形输出
  - 设备侧 safety
  - output confirmation

当前不交付：

- `DEVICE_OWNED_SESSION`
- 断连后设备继续持有旧会话并自动续跑
- reconnect 自动恢复旧 `RUNNING` 会话

跨仓真相源：

- 固件协议：`docs/protocol.md`
- 固件运行结构：`docs/architecture_runtime.md`
- 固件与 APP 通讯语义：`docs/system/esp32_app_communication_semantics.md`
- 固件 start gate 合同：`docs/start-readiness-contract.md`
- SW 主仓 execution model 真相源：
  - `SW/docs/system/安卓APP与ESP32律动会话执行模型说明.md`

## 3. 冻结级别

本清单按两类处理：

- `冻结`
  当前重构默认不能改变；要改就属于正式协议/行为变更
- `可内部重构`
  允许改内部 owner、抽取模块、改善实现，但对外行为必须保持等价

## 4. BLE 外部合同冻结项

### 4.1 GATT 与分帧

- `冻结`
  - Service UUID：`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - RX Characteristic UUID：`6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  - TX Characteristic UUID：`6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
  - RX 仍是 App -> Firmware 文本命令写入
  - TX 仍是 Firmware -> App 文本通知
  - 每条逻辑消息仍以 `\n` 结尾
- `可内部重构`
  - BLE 回调内部组织方式
  - queue / task / recovery 内部拆分
  - advertising 配置代码结构

检查项：

- [ ] UUID 未变化
- [ ] 文本行协议未变成二进制协议
- [ ] `\n` 分帧规则未变化
- [ ] Android / Demo APP 现有按行解析无需同步修改

### 4.2 广播与设备身份

- `冻结`
  - 默认广播名仍使用 `SonicWave_<Model>`
  - `DEVICE_SET_CONFIG` 后广告身份更新的外部效果不能消失
- `可内部重构`
  - advertising power/profile 切换实现
  - 广播重启内部节流实现

检查项：

- [ ] `BASE/PLUS/PRO/ULTRA` 对应名字未变
- [ ] 连接前后、改配置后，APP 仍能按当前方式识别设备

### 4.3 Bootstrap truth

- `冻结`
  - `CAP?` 仍可用
  - `ACK:CAP` 仍保留当前核心字段语义：
    - `fw`
    - `proto`
    - `platform_model`
    - `laser_installed`
  - `platform_model` / `laser_installed` 的消费含义不能暗改
- `可内部重构`
  - `ACK:CAP` 拼接实现
  - 内部 truth source 获取路径

检查项：

- [ ] `CAP?` 未改名
- [ ] `ACK:CAP` 关键字段未删
- [ ] `BASE` 仍可被下游识别为正式 laserless profile
- [ ] 没有把 debug/兼容字段伪装成正式 bootstrap truth

### 4.4 Runtime truth

- `冻结`
  - `SNAPSHOT` 仍是 reconnect 后 authoritative runtime truth 之一
  - 当前正式 APP 依赖的字段语义不能变：
    - `top_state`
    - `runtime_ready`
    - `start_ready`
    - `baseline_ready`
    - `platform_model`
    - `laser_installed`
    - `laser_available`
    - `degraded_start_available`
    - `degraded_start_enabled`
  - connect-time `SNAPSHOT` 仍需尽量保持在常见单 notify 路径内
- `可内部重构`
  - snapshot 组装路径
  - direct query 快速路径实现

检查项：

- [ ] reconnect 后仍能拉到当前停波态 truth
- [ ] 没有把旧会话 `RUNNING` 脏状态带回 `SNAPSHOT`
- [ ] `start_ready` 仍由固件正式 truth 提供，而不是让 APP 自行重推导
- [ ] payload 没有因为重构膨胀到常见 MTU 路径明显超预算

## 5. 控制指令冻结项

### 5.1 正式命令

- `冻结`
  - `WAVE:SET`
  - `WAVE:START`
  - `WAVE:STOP`
  - `SCALE:ZERO`
  - `SCALE:CAL`
  - `CAL:CAPTURE`
  - `CAL:GET_MODEL`
  - `CAL:SET_MODEL`
  - `DEVICE:SET_CONFIG`
  - `DEBUG:DEGRADED_START`
  - `DEBUG:FALL_STOP`
  - `DEBUG:MOTION_SAMPLING`
- `可内部重构`
  - parser 内部结构
  - handler 分发结构

检查项：

- [ ] 命令名未改
- [ ] 现有 alias/compat 命令未被无说明删除
- [ ] 参数缺失/越界时仍返回 `NACK:INVALID_PARAM`
- [ ] `WAVE:START` 失败时仍区分 `NACK:NOT_ARMED` 与 `NACK:FAULT_LOCKED`

### 5.2 Legacy 兼容命令

- `冻结`
  - `ZERO`
  - `SET_PS:<zero>,<factor>`
  - `F/I/E` 组合命令兼容入口
- `可内部重构`
  - legacy parser 和正式 parser 是否共用底层解析器

检查项：

- [ ] 旧调试链路不会因重构被静默切断
- [ ] legacy 启停仍经过正式安全联锁，不允许绕开状态机

## 6. 事件语义冻结项

### 6.1 必须保留的上行事件

- `冻结`
  - `EVT:STATE`
  - `EVT:FAULT`
  - `EVT:SAFETY`
  - `EVT:STOP`
  - `EVT:STABLE`
  - `EVT:PARAM`
  - `EVT:STREAM`
  - `EVT:BASELINE`
- `可内部重构`
  - 事件内部生成位置
  - EventBus / encode path 结构

检查项：

- [ ] 事件名未改
- [ ] 现有正式 consumer 无需同步改 parser
- [ ] 没有把正式事件退化成仅串口日志可见

### 6.2 `EVT:STATE`

- `冻结`
  - 仍只表达设备顶层状态：
    - `IDLE`
    - `ARMED`
    - `RUNNING`
    - `FAULT_STOP`
  - 不能把产品会话语义直接混进 `STATE`

检查项：

- [ ] `STATE` 仍只回答“设备当前顶层运行态是什么”
- [ ] 不把 `PAUSED_RECOVERABLE` 之类产品态塞进 firmware `STATE`

### 6.3 `EVT:SAFETY`

- `冻结`
  - 仍表达：
    - `reason`
    - `code`
    - `effect`
    - `state`
    - `wave`
  - 当前正式 reason/effect 口径不能暗改：
    - `USER_LEFT_PLATFORM -> RECOVERABLE_PAUSE`
    - `FALL_SUSPECTED -> ABNORMAL_STOP`
    - `BLE_DISCONNECTED -> 当前 APP-driven 默认构建为 RECOVERABLE_PAUSE`

检查项：

- [ ] 字段名未改
- [ ] `BLE_DISCONNECTED` 仍能被 APP 映射为“需要重连后重新开始”
- [ ] warning/debug 值不会被冒充为正式主语义

### 6.4 `EVT:STOP`

- `冻结`
  - 仍表达：
    - `stop_reason`
    - `stop_source`
    - `code`
    - `effect`
    - `state`
  - 仍是“这次停波最终按什么语义落地”的正式通道
- `可内部重构`
  - stop context 记忆与拼装路径

检查项：

- [ ] `MANUAL_STOP`、`USER_LEFT_PLATFORM`、`BLE_DISCONNECTED` 等现有停波语义仍可见
- [ ] `stop_source` 不会因为重构丢失

### 6.5 `EVT:FAULT`

- `冻结`
  - numeric code 前缀兼容不能消失
  - 当前最小兼容桥接不能静默删除：
    - `USER_LEFT_PLATFORM`
    - `FALL_SUSPECTED`
- `可内部重构`
  - 兼容桥接实现位置

检查项：

- [ ] Demo APP / legacy parser 现有 numeric 依赖不被破坏
- [ ] SW APP 当前仍需要的 reason bridge 未丢

### 6.6 `EVT:STREAM`

- `冻结`
  - 仍是 measurement plane 的唯一正式 continuous carrier
  - `seq / ts_ms / valid / ma12_ready / distance / weight / ma12 / reason` 的正式含义不能暗改
- `可内部重构`
  - stream 节流、队列实现、测量 owner 内部拆分

检查项：

- [ ] 没有退回到只发裸 CSV 的旧路
- [ ] invalid sample 仍带 formal `reason`

## 7. Disconnect / Reconnect 冻结项

这是当前阶段最关键的冻结区。

- `冻结`
  - BLE 断连后必须停止当前波形输出
  - SW APP 当前会话必须视为中断
  - reconnect 后不得自动恢复断连前旧会话
  - reconnect 后要先恢复当前设备 truth，再允许新的 `WAVE:START`
  - 当前正式默认 disconnect policy 仍是 `RECOVERABLE_PAUSE`
- `可内部重构`
  - disconnect 回调如何进入状态机
  - recovery / queue / advertising 内部组织方式

检查项：

- [ ] 手动断开后设备输出立即停止
- [ ] reconnect 后 `SNAPSHOT` 不带脏 `RUNNING`
- [ ] reconnect 后再次开始不会因为旧会话残留而异常停止
- [ ] 没有偷偷做 auto-resume

## 8. 重构时默认不能顺手做的事

- [ ] 不把当前执行模型从 `APP_DRIVEN_SESSION` 偷偷改成 `DEVICE_OWNED_SESSION`
- [ ] 不把 disconnect 当成“设备继续跑、APP 只是掉线”
- [ ] 不顺手改字段名、事件名、命令名
- [ ] 不删除 Demo APP / SW APP 已消费的兼容桥接输出
- [ ] 不用“内部更优雅”为理由改变对外 ACK/NACK 行为
- [ ] 不把 reconnect-time truth 改成依赖多包拼接、超长 payload 或新的本地推导

## 9. 允许优先做的重构方向

在不破坏以上冻结项前提下，当前优先允许做：

1. `ProtocolCodec` 严格输入校验
2. `BleTransport` 内部 recovery / queue / task 拆分
3. `LaserModule` 内部 owner 抽取，但不改外部命令/事件合同
4. 启动失败路径和资源申请失败的 fail-fast / 显式错误处理

## 10. focused 回归 checklist

每次涉及 BLE 重构，至少回归这些场景：

- [ ] `CAP? -> ACK:CAP`
- [ ] connect 后 `SNAPSHOT` 可用
- [ ] `WAVE:SET -> ACK:OK`
- [ ] `WAVE:START` 在 ready 条件下成功
- [ ] `WAVE:START` 在未 ready 条件下仍返回 `NACK:NOT_ARMED`
- [ ] `WAVE:STOP -> ACK:OK`
- [ ] 运行中手动断开 BLE 后立即停波
- [ ] reconnect 后 snapshot 为当前停波态，不是脏 `RUNNING`
- [ ] reconnect 后可以重新开始新会话，而不是恢复旧会话
- [ ] `EVT:STATE / EVT:SAFETY / EVT:STOP / EVT:FAULT / EVT:STREAM` 仍能被现有 consumer 正常解析

## 11. 使用规则

如果重构只属于内部抽取或内部治理，则必须满足：

- 对外命令不变
- 对外事件不变
- 对外 disconnect / reconnect 行为不变
- Demo APP / SW APP parser 不需要同步修改

如果任一项不满足，就不再是“安全重构”，而是：

- 正式协议变更
- 正式交付行为变更
- 需要跨仓审计、跨端实现、联调验证后才能落地
