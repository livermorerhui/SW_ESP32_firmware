# 固件工程审计报告（ESP32 / PlatformIO / BLE / I2S / Modbus）

## 1. 概览
- 审计时间：2026-03-05
- 仓库：`https://github.com/livermorerhui/SW_ESP32_firmware`（本地 `main`）
- 审计范围：工程结构、`.gitignore`、可编译性、BLE/I2S/Modbus/安全联锁/参数边界/并发风险
- 结论：
  - 工程类型明确为 **PlatformIO + Arduino**。
  - 已完成最小可落地修复，当前可本地编译通过。
  - 仍有并发与通信鲁棒性风险，建议按“待修复项”继续收敛。

## 2. 工程类型判定
### 2.1 顶层结构盘点（仓库根目录）
- `.git/`
- `.gitignore`
- `.pio/`（本地构建产物，已忽略）
- `.vscode/`（本地 IDE 配置，已忽略）
- `README.md`
- `platformio.ini`
- `include/`
- `lib/`
- `src/`
- `test/`

### 2.2 类型识别结论
- 存在 `platformio.ini`、`src/`、`include/`、`lib/`，判定为 **PlatformIO 工程**。
- `platformio.ini` 显示：
  - `framework = arduino`
  - `board = esp32-s3-devkitc-1`
  - 依赖 `4-20ma/ModbusMaster`

### 2.3 不应提交产物检查
- 检测到本地存在：`.pio/`、`.vscode/`。
- `git ls-files | rg` 未发现 `.pio/.vscode/*.bin/*.elf` 等已被跟踪文件。
- `git ls-files -ci --exclude-standard` 输出为空（当前无“已跟踪但应忽略”文件）。

## 3. 构建方式与结果
### 3.1 实际执行命令
- `~/.platformio/penv/bin/pio run`

### 3.2 首次构建结果（失败）
- 失败点：`src/transport/ble/BleTransport.cpp`
- 原因：回调类访问 `BleTransport` 私有成员（`deviceConnected` / `startAdvertisingSafe` / `sendLine` / `bus`）导致编译错误。

### 3.3 修复后构建结果（成功）
- 同命令再次构建：`[SUCCESS]`
- 目标环境：`env:esp32s3`

## 4. 发现的问题列表（按严重度）

## 严重（Critical）
1. BLE 传输层编译不可通过（已修复）
- 证据：`src/transport/ble/BleTransport.cpp` 访问私有成员失败。
- 修复：在 `BleTransport` 中声明 friend 回调类。
- 变更位置：`src/transport/ble/BleTransport.h:23-24`

2. 状态机接口实现缺失导致链接风险（已修复）
- 证据：`SystemStateMachine.h` 声明 `begin/state`，原 `.cpp` 未实现。
- 修复：补充 `begin()` 与 `state()`。
- 变更位置：`src/core/SystemStateMachine.cpp:3-14`

3. 旧协议可绕过状态机直接启停输出（已修复）
- 证据：原 `LEGACY_FIE` 分支直接 `setEnable`，未走 `requestStart()` 联锁。
- 修复：新增 `hasEnable` 触碰位；`E` 字段启停复用状态机校验。
- 变更位置：
  - `src/core/Types.h:16-20`
  - `src/core/ProtocolCodec.h:72-80`
  - `src/main.cpp:62-81`

4. 故障后仍可能持续输出（已修复兜底）
- 证据：`onSensorErr/onFallSuspected/onUserOff` 仅改状态，未统一关输出。
- 修复：主循环增加“非 RUNNING 强制停波”。
- 变更位置：`src/main.cpp:123-126`

## 高（High）
1. 跨任务并发访问状态机，无互斥保护（待修复）
- 证据：
  - BLE 命令路径调用 `requestStart/requestStop`：`src/main.cpp:35-50` + `src/transport/ble/BleTransport.cpp:35-37`
  - 激光任务调用 `onUserOn/onUserOff/onSensorErr/onFallSuspected`：`src/modules/laser/LaserModule.cpp:108-120`
  - `SystemStateMachine` 内部无锁直接读写 `st/fault_ms`：`src/core/SystemStateMachine.cpp:43-119`
- 风险：状态竞争导致误判、重复 fault、启动/停机时序异常。

2. 校准参数跨上下文读写未加锁（待修复）
- 证据：
  - 命令线程写：`src/modules/laser/LaserModule.cpp:30-38`
  - 采样任务读：`src/modules/laser/LaserModule.cpp:104`
- 风险：`zeroDistance/scaleFactor` 竞争更新导致瞬时跳变与误报。

3. Fault 事件可高频重复上报，可能触发 BLE notify 风暴（待修复）
- 证据：
  - Modbus 失败每轮触发 `onSensorErr`：`src/modules/laser/LaserModule.cpp:80-89`
  - `onSensorErr` 每次均 `emitFault`：`src/core/SystemStateMachine.cpp:72-77`
  - `sendLine` 无节流/排队：`src/transport/ble/BleTransport.cpp:73-77`
- 风险：手机端丢包、连接不稳、CPU 占用攀升。

## 中（Medium）
1. I2S/驱动调用未检查返回值（待修复）
- 证据：`i2s_driver_install/i2s_set_pin/i2s_write` 返回值未判断。
- 位置：`src/modules/wave/WaveModule.cpp:38-40,162`

2. `SCALE:CAL` 参数缺少边界与异常值约束（待修复）
- 证据：协议层仅解析，不限制 `z/k` 合法范围：`src/core/ProtocolCodec.h:31-38`；
  存储层直接写入：`src/modules/laser/LaserModule.cpp:30-35`
- 风险：`k<=0` 或极端值导致体重计算异常与联锁误触发。

3. BLE notify 未判断订阅状态/返回状态（待修复）
- 证据：`pTx->notify()` 直接调用：`src/transport/ble/BleTransport.cpp:76`
- 风险：兼容性差时表现为“偶发发不出”或连接抖动。

## 5. 建议修复（可落地）
1. 为 `SystemStateMachine` 增加互斥（`portMUX_TYPE` 或 `SemaphoreHandle_t`），保证状态迁移原子化。
2. `LaserModule` 参数写入改为“命令入队 -> 任务线程消费”，避免跨线程直接改 `zeroDistance/scaleFactor`。
3. Fault 上报加去抖：同故障码在 `N` ms 内只发一次；Modbus 连续失败达到阈值后再触发 fault。
4. `sendLine/notify` 加节流与队列，建议统一发送任务（固定频率 + 背压丢弃策略）。
5. 给 `SCALE:CAL` 增加 clamp：例如 `k` 限制在 `(0.01, 100)`，`z` 限制在设备量程。
6. 检查 I2S API 返回值，失败时上报 fault 并停波。

## 6. 风险矩阵
| 风险项 | 影响 | 发生概率 | 等级 | 当前状态 |
|---|---|---|---|---|
| BLE 回调私有访问导致无法编译 | 高 | 高 | Critical | 已修复 |
| 旧协议绕过联锁直接启停 | 高 | 中 | Critical | 已修复 |
| 故障后输出未兜底关闭 | 高 | 中 | Critical | 已修复 |
| 状态机并发竞争 | 高 | 中 | High | 待修复 |
| Modbus 失败导致 fault/notify 风暴 | 中-高 | 中 | High | 待修复 |
| 参数边界不足（校准） | 中 | 中 | Medium | 待修复 |
| I2S 返回值未检查 | 中 | 中 | Medium | 待修复 |

## 7. 本次已落地改动摘要
- `.gitignore` 补全编辑器/临时文件项。
- 修复 BLE 编译错误。
- 补齐状态机缺失实现。
- 修复 LEGACY 命令联锁绕过。
- 增加非 RUNNING 态停波兜底。
