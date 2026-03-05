# SonicWave 固件维护与开发指南

## 1. 项目概览

### 1.1 SonicWave 设备作用
SonicWave 是一个由 ESP32 控制的理疗/声波输出设备，核心目标是：
- 通过 I2S 生成可控频率与强度的波形输出（`WaveModule`）。
- 通过 Modbus 激光测距/称重链路感知用户状态（`LaserModule`）。
- 在“用户在位、无故障”前提下允许输出，在异常时立即进入安全停机（`SystemStateMachine` + 安全联锁）。

### 1.2 ESP32 在系统中的角色
ESP32-S3 是主控，承担：
- BLE 通信网关：与 Android App 双向收发命令/事件。
- 实时任务调度：音频输出任务、传感采样任务、主循环安全监督。
- 状态治理：统一状态机与故障机制。
- 参数持久化：通过 `Preferences` 保存校准参数。

### 1.3 与 Android App 的关系
Android App 是上位机控制端：
- 下行：发送控制指令（如 `WAVE:SET`、`WAVE:START`、`SCALE:CAL`）。
- 上行：接收 ACK/NACK 与事件（状态、故障、稳定体重、参数、实时流）。
- 设计原则：App 不直接操作底层模块，所有动作均通过协议与状态机进行。

## 2. 系统架构

### 2.1 模块职责
- `LaserModule`：
  - 通过 `Serial1 + ModbusMaster` 读取距离寄存器。
  - 执行零点/比例系数校准。
  - 计算重量、稳定性判定、用户在位/离位、跌倒怀疑触发。
  - 向 `EventBus` 发布 `PARAMS/STABLE/STREAM`。
- `WaveModule`：
  - 配置 I2S 驱动、DMA、查表合成音频。
  - 平滑频率与强度，避免突变。
  - 根据启停状态输出波形或静音。
- `BleTransport`：
  - 建立 BLE GATT 服务（RX 写入，TX notify）。
  - 解析命令并分发到 `CommandBus`。
  - 将 `EventBus` 事件编码后发送给 App。
- `SystemStateMachine`：
  - 管理 `IDLE/ARMED/RUNNING/FAULT_STOP`。
  - 执行启动条件校验、故障锁定、清故障窗口。
- `EventBus`：
  - 事件分发总线，目前单 sink（`BleTransport`）。

### 2.2 调用关系（主链路）
```text
Android App
   | BLE Command
   v
BleTransport -> ProtocolCodec::parseCommand -> CommandBus -> HubHandler(main.cpp)
                                                     |                |
                                                     |                +-> WaveModule
                                                     |                +-> LaserModule
                                                     |                +-> SystemStateMachine

LaserTask(Modbus sample) -> SystemStateMachine (onUserOn/off/onSensorErr/onFallSuspected)
LaserTask -> EventBus::publish -> BleTransport::onEvent -> BLE notify -> Android App
```

### 2.3 初始化顺序（setup）
1. `g_fsm.begin(&g_eventBus)`
2. `g_ble.begin(&g_cmdBus)`
3. `g_eventBus.setSink(&g_ble)`
4. `g_cmdBus.setHandler(&g_handler)`
5. `g_wave.begin()`
6. `g_laser.begin(...)` + `g_laser.startTask()`

## 3. 线程与核心分布

### 3.1 FreeRTOS 任务
- 主任务（Arduino `setup/loop`）：默认运行于 Arduino 主任务上下文（ESP32 Arduino 通常在 Core1）。
- `I2S_Audio`（`WaveModule`）：`xTaskCreatePinnedToCore(..., priority=4, core=1)`。
- `LaserTask`（`LaserModule`）：`xTaskCreatePinnedToCore(..., priority=2, core=1)`。
- BLE 回调：由 BLE 协议栈任务触发（具体核心由栈/SDK调度决定，不应假设固定核心）。

### 3.2 当前核心分布解读
- 业务任务基本集中在 Core1，易造成同核竞争。
- 典型风险：状态机与参数跨任务并发访问，目前缺少统一互斥。

### 3.3 建议
- 中期将“通信/事件发送”与“传感采样”做任务隔离（可考虑部分迁移到 Core0）。
- 为状态机与共享参数增加互斥或队列化访问。

## 4. 安全机制

### 4.1 Safety Interlock
- 启动必须走 `SystemStateMachine::requestStart`。
- 非 `RUNNING` 状态在 `loop()` 中会被兜底强制 `g_wave.setEnable(false)`。

### 4.2 Fault Stop
以下任一事件会进入 `FAULT_STOP`：
- 用户离开（`onUserOff`）。
- 跌倒怀疑（`onFallSuspected`）。
- 传感器异常（`onSensorErr`）。

同时具备：
- 冷却锁定窗口：`FAULT_COOLDOWN_MS`。
- 清故障确认窗口：`CLEAR_CONFIRM_MS`（离位持续满足后回到 `IDLE`）。

### 4.3 用户离开检测
- `weight < LEAVE_TH` 判定离开。
- 触发状态迁移并重置稳定体重缓冲。

### 4.4 传感器异常
- Modbus 读寄存器失败会触发 `onSensorErr()`。
- 建议后续增加“连续失败阈值 + 故障去抖”避免故障/通知风暴。

## 5. BLE 协议说明

### 5.1 GATT 角色
- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic（Write）: `...0002...`
- TX Characteristic（Notify）: `...0003...`

### 5.2 新协议指令
- `CAP?`
- `WAVE:SET freq=<float> amp=<int>`
- `WAVE:START`
- `WAVE:STOP`
- `SCALE:ZERO`
- `SCALE:CAL z=<float> k=<float>`

### 5.3 Legacy 协议指令
- `ZERO`
- `SET_PS:<zero>,<factor>`
- `F:<freq>,I:<inten>,E:<0|1>`（字段可部分出现）

### 5.4 ACK/NACK 格式
- 成功：`ACK:OK` / `ACK:CAP fw=<ver> proto=<ver>`
- 失败：`NACK:<reason>`，例如 `NACK:NOT_ARMED`、`NACK:FAULT_LOCKED`、`NACK:INVALID_PARAM`

### 5.5 事件格式
- `EVT:STATE IDLE|ARMED|RUNNING|FAULT_STOP`
- `EVT:FAULT <code>`
- `EVT:STABLE:<weight>`
- `EVT:PARAM:<zero>,<factor>`
- `EVT:STREAM:<dist>,<weight>`

## 6. 构建与烧录

### 6.1 PlatformIO 构建
```bash
# 若 PATH 已配置
pio run

# 常见本机路径
~/.platformio/penv/bin/pio run
```

### 6.2 Upload
```bash
pio run -t upload --upload-port <PORT>
```

### 6.3 Monitor
```bash
pio device monitor -b 115200 --port <PORT>
```

### 6.4 项目配置要点
- `platformio.ini`
- `env: esp32s3`
- `framework = arduino`
- `lib_deps = 4-20ma/ModbusMaster`

## 7. 新功能开发指南

### 7.1 增加新的硬件模块（推荐模板）
```cpp
// src/modules/eeg/EegModule.h
#pragma once
#include <Arduino.h>
#include "core/EventBus.h"

class EegModule {
public:
  void begin(EventBus* eb);
  void startTask();
private:
  static void taskThunk(void* arg);
  void taskLoop();
  EventBus* bus = nullptr;
};
```

```cpp
// src/modules/eeg/EegModule.cpp
#include "EegModule.h"

void EegModule::begin(EventBus* eb) { bus = eb; }
void EegModule::startTask() {
  xTaskCreatePinnedToCore(taskThunk, "EegTask", 4096, this, 2, nullptr, 1);
}
void EegModule::taskThunk(void* arg) { static_cast<EegModule*>(arg)->taskLoop(); }
void EegModule::taskLoop() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));
    Event e{};
    e.type = EventType::STREAM;
    e.v1 = 0.0f;
    e.v2 = 0.0f;
    e.ts_ms = millis();
    if (bus) bus->publish(e);
  }
}
```

### 7.2 如何接入 EventBus
1. 在模块持有 `EventBus*`。
2. 在合适时机构造 `Event` 并 `publish`。
3. 在 `ProtocolCodec::encodeEvent` 增加新事件编码。

示例：
```cpp
Event e{};
e.type = EventType::PARAMS;
e.v1 = zero;
e.v2 = factor;
e.ts_ms = millis();
bus->publish(e);
```

### 7.3 如何接入状态机
- 所有“影响输出安全”的动作必须经过 `SystemStateMachine`。
- 禁止模块直接绕过状态机启停输出。

示例（命令处理中）：
```cpp
FaultCode reason = FaultCode::NONE;
if (!sm->requestStart(reason)) {
  outAck = "NACK:NOT_ARMED";
  return false;
}
wave->setEnable(true);
```

## 8. 调试指南

### 8.1 常见问题
- `pio: command not found`
  - 使用 `~/.platformio/penv/bin/pio`。
- 上传失败（端口占用）
  - 关闭串口监视器后重试。

### 8.2 BLE 问题
- 现象：可连接但收不到事件
  - 检查 App 是否启用 TX notify。
  - 检查是否频繁 fault 导致事件风暴。
- 现象：断连后不恢复
  - 检查 `onDisconnect` 是否触发 `startAdvertisingSafe()`。

### 8.3 I2S 问题
- 现象：无波形输出
  - 检查 `RUNNING` 状态是否达成。
  - 检查 `setEnable(true)` 是否被联锁逻辑立即拉低。
  - 检查 I2S 引脚定义与硬件连线是否一致。
- 现象：杂音/卡顿
  - 检查 `BUFFER_LEN`、任务优先级、同核负载。

### 8.4 Modbus 问题
- 现象：`Modbus read fail`
  - 检查波特率、从站地址、寄存器地址、RX/TX 反接。
  - 检查串口供电与地线共地。
- 现象：故障频发
  - 建议引入连续失败计数与故障去抖再触发 fault。

## 9. 代码规范

### 9.1 注释规范
- 仅对“意图、约束、边界条件”写注释。
- 不写重复代码语义的注释。
- 协议字符串、状态迁移条件必须有注释说明。

### 9.2 模块划分原则
- 单一职责：采样、控制、通信、状态治理分离。
- 统一入口：控制流经 `CommandBus + HubHandler`。
- 统一出口：事件流经 `EventBus`。

### 9.3 跨模块调用约束
- 不允许直接跨模块调用内部细节。
- 允许方式：
  - 命令路径：`CommandBus`。
  - 事件路径：`EventBus`。
  - 安全动作：`SystemStateMachine`。
- 禁止方式：模块 A 直接改模块 B 的状态变量。

## 10. 扩展规划

### 10.1 EEG
- 新建 `EegModule` 周期采样。
- 增加新 `EventType::EEG_STREAM`。
- App 增加 EEG 视图与记录。

### 10.2 心率
- 复用传感任务框架，加入滤波与异常检测。
- 将心率阈值异常映射到状态机告警（非立即停机，按策略）。

### 10.3 血氧
- 增加 SpO2 模块及校准命令。
- App 接入参数同步与趋势图。

### 10.4 OTA
- 推荐两阶段：
  - 阶段1：串口 OTA/本地包升级。
  - 阶段2：BLE/Wi-Fi OTA（带签名与版本回滚策略）。
- OTA 期间必须进入 `FAULT_STOP` 或等效安全态，禁止波形输出。

## 附录 A：开发者接手第一天清单
1. 拉取代码并完成 `pio run`。
2. 用手机验证 BLE 指令链路（`CAP?`、`WAVE:SET`、`WAVE:START/STOP`）。
3. 验证离位/断连联锁是否能停波。
4. 查看 `reports/firmware_audit/`，优先处理 High 风险项（并发、fault 限流）。

## 11. 开发者接手 Checklist

### 11.1 编译固件
```bash
# 推荐
~/.platformio/penv/bin/pio run

# 或 PATH 已配置时
pio run
```
通过标准：输出包含 `[SUCCESS]`，生成 `.pio/build/esp32s3/firmware.bin`。

### 11.2 烧录固件
```bash
pio run -t upload --upload-port <PORT>
```
通过标准：烧录成功，无 `Timed out waiting for packet header` 等错误。

### 11.3 验证 BLE
1. 手机 App 连接设备 `SonicWave_Hub`。
2. 发送 `CAP?`，应收到 `ACK:CAP fw=... proto=...`。
3. 发送 `WAVE:SET freq=40 amp=80`，应收到 `ACK:OK`。
4. 发送 `WAVE:START`，状态事件应出现 `EVT:STATE RUNNING`。

### 11.4 验证 I2S 输出
1. 在已 `RUNNING` 状态下，用示波器/逻辑分析仪检查 `I2S_BCLK_PIN/I2S_LRCK_PIN/I2S_DOUT_PIN`。
2. 切换 `WAVE:SET` 的频率和强度，确认波形幅值/频率变化。
3. 发送 `WAVE:STOP` 后输出应进入静音。

### 11.5 验证激光测重（Modbus）
1. 检查 `Serial1` 引脚与从站接线（`RX_PIN/TX_PIN`）。
2. 观察串口日志是否存在连续 `Modbus read fail`。
3. 执行 `SCALE:ZERO` 后读取事件，确认参数事件更新。
4. 让用户上秤并稳定，确认 `EVT:STABLE:<weight>` 触发。

### 11.6 验证 Safety Interlock（必须项）
预置条件：先进入 `RUNNING`，且确认有振动输出。

测试项 A：用户离开平台
1. 运行中让用户离开（使 `weight < LEAVE_TH`）。
2. 期望：系统进入 `FAULT_STOP`，振动停止。

测试项 B：蓝牙断开
1. 运行中主动断开 BLE 连接。
2. 期望：触发断连停机逻辑，振动停止。

测试项 C：Modbus 读取失败
1. 运行中断开传感器总线或制造读取失败。
2. 期望：触发传感器故障路径，进入 `FAULT_STOP`，振动停止。

判定标准（A/B/C 全部必须满足）：
- `RUNNING` 期间一旦触发上述事件，系统必须停止振动输出。

## 12. 参数与阈值说明

参数定义位于 `src/config/GlobalConfig.h`。

| 参数 | 默认值 | 作用 | 调整建议 |
|---|---:|---|---|
| `LEAVE_TH` | `3.0f` | 判定“用户离开平台”的重量阈值，低于该值触发离位逻辑。 | 先按空载噪声上界 + 安全余量设定，过低会延迟停机，过高会误判离位。 |
| `MIN_WEIGHT` | `5.0f` | 判定“用户在位”起始阈值，高于该值才进入有效载荷判定。 | 建议与 `LEAVE_TH` 保持滞回（`MIN_WEIGHT > LEAVE_TH`），避免状态抖动。 |
| `STD_TH` | `0.20f` | 稳定体重判定的标准差阈值，小于该值认为稳定。 | 现场噪声大时可适度放宽；放宽过多会降低稳定判定可信度。 |
| `WINDOW_N` | `10` | 稳定性判定窗口样本数。 | 增大可提升稳定性但会增加判定延迟；减小会更灵敏但更易误报。 |
| `FALL_DW_DT_SUSPECT_TH` | `25.0f` (kg/s) | 跌倒怀疑阈值，重量变化率超过该值触发可疑跌倒。 | 需基于真实人群与场景标定；建议分机型维护基线并逐步收敛。 |

## 13. 常见维护任务

### 13.1 修改 BLE 协议：需要改哪些文件
- 指令解析：`src/core/ProtocolCodec.h`
- 命令类型定义：`src/core/CommandBus.h`
- 命令处理分发：`src/main.cpp`（`HubHandler::handle`）
- 事件编码：`src/core/ProtocolCodec.h`（`encodeEvent`）
- BLE 传输实现：`src/transport/ble/BleTransport.cpp/.h`

### 13.2 新增硬件模块：如何接入 EventBus
1. 在 `src/modules/<new_module>/` 新建模块类，提供 `begin/startTask`。
2. 模块内部持有 `EventBus*` 并在关键时机 `publish(Event)`。
3. 在 `setup()` 中初始化并启动任务。
4. 若新增事件类型，更新：
   - `src/core/EventBus.h`（`EventType`）
   - `src/core/ProtocolCodec.h`（编码）

### 13.3 修改 I2S 音频参数：需要改哪些文件
- 参数与引脚：`src/config/GlobalConfig.h`（`SAMPLE_RATE/BUFFER_LEN/I2S_*_PIN`）
- 驱动配置与合成逻辑：`src/modules/wave/WaveModule.cpp/.h`
- 若涉及协议控制范围：`src/core/ProtocolCodec.h`（参数校验）

### 13.4 新增状态机事件：需要改哪些文件
- 状态与故障枚举：`src/core/Types.h`
- 状态迁移逻辑：`src/core/SystemStateMachine.h/.cpp`
- 事件定义与发布：`src/core/EventBus.h`
- 事件编码与上报：`src/core/ProtocolCodec.h` + `src/transport/ble/BleTransport.cpp`
- 如需外部控制入口：`src/core/CommandBus.h` + `src/main.cpp`
