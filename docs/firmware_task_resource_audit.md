# ESP32-S3 固件任务与资源审计

版本：2026-10-22
适用分支：`codex/construct/esp32-architecture`

本文记录重构后源码中的任务、队列、锁和构建资源事实。源码审计结果与目标板运行结果分开记录；没有目标板采集值时，不把源码配置当作运行时高水位。

## 1. 任务边界

| 任务 | 创建位置 | 栈配置 | 优先级 | 核 | 职责 |
|---|---|---:|---:|---:|---|
| `I2S_Audio` | `src/modules/wave/WaveModule.cpp` | 4096 | 4 | 1 | 正弦波、律动输出、软停、提示音 |
| `LaserTask` | `src/modules/laser/LaserModule.cpp` | 4096 | 2 | 1 | 传感器读取、测量事实、稳定和保护输入 |
| `BleCtrl` | `src/transport/ble/BleTransport.cpp` | 12288 | 3 | 1 | BLE 控制队列、命令解码和连接生命周期 |
| `BleOta` | `src/transport/ble/BleTransport.cpp` | 8192 | 3 | 1 | OTA 控制和数据队列 |
| `BleTx` | `src/transport/ble/BleTransport.cpp` | 4096 | 3 | 1 | 控制事件、流事件和 ACK/NACK 通知 |
| `DiagnosticLog` | `src/core/FirmwareDiagnostics.cpp` | 静态 `stackStorage` | 1 | 不固定 | 有界诊断日志写入 |
| Arduino 主循环 | `src/main.cpp` | 框架默认 | 框架管理 | 框架管理 | 仅保留轻量 `delay(20)` 守护 |

## 2. 队列与背压

| 队列 | 容量 | 消息 | 满时处理 | 级别 |
|---|---:|---|---|---|
| BLE 控制 | 8 | `ControlMsg` | 控制命令走有界入队及前置重试逻辑 | 不可静默丢失 |
| BLE OTA | 8 | `OtaMsg` | 回调只入队，OTA 任务顺序消费 | 不可静默丢失 |
| BLE 控制发送 | 64 | `TxMsg` | 控制帧按有界等待入队 | 高优先级 |
| BLE 流发送 | 1 | `TxMsg` | 使用覆盖策略，只保留最新流帧 | 可丢失旧帧 |
| BLE 队列集合 | 65 | 队列句柄 | `BleTx` 按控制优先、流帧次序消费 | 控制优先 |
| 诊断日志 | 源码有界缓冲 | 诊断帧 | 队满或写失败记录健康状态，不阻塞业务任务 | 诊断可降级 |

`EventBus` 当前是单接收者同步发布；接收者为 `BleTransport`，其 `onEvent` 只进入发送队列。测量、状态机和波形模块不直接操作 BLE 特征。

## 3. 阻塞点

- `LaserTask` 使用配置转换互斥锁保护一次采样和配置切换；锁不跨越 `vTaskDelay`。
- 传感器读写使用读写互斥锁；配置命令和采样不会并发改写同一个 reader。
- `BleOta` 和 `BleTx` 使用 `portMAX_DELAY` 等待各自队列，业务任务不在这些等待点同步调用它们。
- `I2S_Audio` 的 `i2s_write` 使用 `portMAX_DELAY` 等待硬件 DMA；波形输出 Owner 保持在 `WaveModule` 任务内。
- `BleTx` 的流发送锁只保护通知发送临界区；控制帧和流帧仍由队列优先级隔离。

本轮重构没有新增任务、队列或新的长时间等待点；命令、协议和激光状态拆分均保留原任务边界。

## 4. 构建资源证据

`python3 -m platformio run -e esp32s3_unified` 已成功：

- RAM：49,780 / 327,680，约 15.2%。
- Flash：1,037,241 / 6,553,600，约 15.8%。
- 标准依赖警告仍来自 `ModbusMaster` 的未使用函数和旧 PlatformIO Core，不是本轮重构引入的编译错误。

## 5. 目标板待采集项

以下数据必须在用户烧录后通过串口和 APP 联调采集，源码和构建不能替代这些证据：

1. 每个任务的 `uxTaskGetStackHighWaterMark` 最小余量。
2. BLE 控制、OTA、控制发送、流发送队列的峰值水位和满队次数。
3. `EventBus → BleTx → Notify` 的事件延迟，特别是停止、故障和控制 ACK。
4. 配置切换互斥锁、传感器读写锁和 I2S DMA 的最长等待时间。
5. 冷启动、连接、启动、软停、断连提示音、离位保护和 OTA 期间的堆内存最低值。

这些数据用于阶段 9 的目标板回归；在完成采集前，重构结论只能标记为“源码与构建通过，运行证据待验收”。

## 6. 依据

- Espressif FreeRTOS 任务、队列和同步原语文档：<https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/freertos.html>
- 当前正式重构蓝图：[`firmware_refactor_blueprint.md`](firmware_refactor_blueprint.md)
