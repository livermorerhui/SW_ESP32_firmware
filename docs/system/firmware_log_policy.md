# 固件日志规则（Firmware Log Policy）

最后更新时间：2026-10-17

当前状态：A1/A4 诊断代码与原生测试已实施；本地验证见第 7 节。未执行完整固件构建、烧录或真机时序验证，不宣称吞吐、栈水位或保护时延已经达标。

跨端入口：SW 仓库 `docs/system/SW功能开发与Bug修复可观测性协议.md`、`docs/system/SW跨端精准日志与采集优化施工蓝图-20261017.md`、同日接口/测试矩阵。APP 会话、正式 BLE 帧、输出及 Plus 保护业务语义冻结。

## 1. 用户操作与实际职责

1. 用户在 APP 操作，ESP32 按原有控制、测量和保护逻辑执行。服务器不参与本地诊断。
2. 固件任务提交固定大小的诊断事件；不在控制/保护任务中等待串口、等待队列空位或同步兜底打印。
3. 唯一产品诊断后台任务输出原有中文，并在末尾追加可对齐的事件身份。电脑采集器只记录收到的字节，不回写设备业务。
4. 电脑停止采集不关闭固件的静态排障配置。恢复默认模式需要用户下一次选择正常构建；本包没有新增 BLE 日志控制命令、动态租约或 APP 会话职责。

## 2. 唯一 Owner 与代码边界

| 模块 | 职责 | 不负责 |
|---|---|---|
| `HumanReadableLog.h` | 原接口、中文默认文案，提交 typed event | 串口 I/O、设备判断 |
| `VerboseSerial.h` / `OtaDiagnosticLog.h` | 静态 profile 先判定，详细宏参数随后求值 | 创建第二个 writer |
| `FirmwareDiagnostics.*` | task-only 提交、原子准入、后台任务、健康快照 | ISR、BLE 帧、业务锁、保护动作 |
| `FirmwareDiagnosticBuffer.*` | 固定记录、FIFO、关键预留、UTF-8 边界、后台中文编码 | RTOS 或串口依赖 |
| `FirmwareDiagnosticSink.*` | 后台单次有限写入，返回驱动接纳字节数 | 证明电脑收到 |
| `FirmwareLogPolicy.h` | 高频摘要节流，显式初始化状态 | 业务状态或输出时序 |

`main` 在已有 `Serial.begin` 后初始化诊断任务。重复初始化不创建第二个任务；失败返回不可用，后续提交计数并拒绝，不退回同步打印。所有原始文本复制到记录内，不借用调用者指针。

当前白名单覆盖：控制入口、状态机/保护、波形参数/启停、提示音、激光/基线、BLE 压力/连接以及 OTA 诊断。通过既有共用日志接口接入，未改这些模块的状态、公式、命令顺序或阈值。`src/` 中原有产品 `Serial.print*` / `ESP_LOGI` 调用已收口；Arduino、ESP-IDF、ROM、panic/brownout 等系统输出以及 `test_firmware/` 独立测试镜像不属于本 writer。系统日志可能插入产品字节流，不能承诺整个串口都具有产品序号或整行原子性。

## 3. 默认模式与静态 scope

| 配置 | 实际内容 |
|---|---|
| `SERIAL_VERBOSE_DIAGNOSTICS=0`（默认） | 基本中文事实、关键事件、低频诊断健康信息 |
| `SERIAL_VERBOSE_DIAGNOSTICS=1` | 开启详细日志，由下列 profile 进一步筛选 |
| `SW_DIAGNOSTIC_PROFILE=0` | 关闭详细日志 |
| `SW_DIAGNOSTIC_PROFILE=1` | 全部详细日志；verbose 开启时的默认值 |
| `SW_DIAGNOSTIC_PROFILE=2/3/4/5` | 分别为 wave / laser / ble / ota 及公共 system 详细日志 |

profile 是编译配置。现有源路径映射到 owner；公共 `system` 信息会保留，不等于每条记录都只涉及一个专项。基本中文和关键事实不随详细 profile 消失。原有 rollback USB 测试构建的启动证据继续独立于 verbose，通过固定 `BOOT_DIAGNOSTIC` / `ROLLBACK_READY` 事件进入同一后台 writer。

详细宏在 gate 关闭时不求值参数。gate 开启后执行有界输出缓冲的 `vsnprintf`，然后入队；这仍有计算成本，不能声称零计算、严格恒定时延或所有 C 库内部都无分配。调用处在宏外已经做的准备工作不由该 gate 取消。基本 typed event 的中文格式化在后台完成。

## 4. 固定容量、准入与丢失语义

| 项目 | 当前硬限制 / 合同 |
|---|---|
| 单条事件 | `sizeof(Event) <= 256 B`；固定文本区 200 B；两个文本字段共用该区 |
| 事件容量 | 32 条，包含队列待写与已取出但未写完的 1 条 |
| 关键预留 | 普通事件在总占用达到 24 时拒绝；关键事件可以继续使用剩余 8 个位置 |
| 后台资源 | 4 KiB 静态任务栈、768 B 编码缓冲、优先级 1；队列、在飞记录、栈和任务控制块等静态预算以 `static_assert` 限制为 16 KiB 以内 |
| 每轮写入 | 单次 sink 请求至多 64 B；后台每批至多 8 次尝试后 `vTaskDelay(1)` |
| 健康摘要 | 首次及每 2 秒到期时，在当前半行完成后输出；不入事件队列、不分配事件序号、不递归记录自身失败 |

任务准入在同一短临界区完成序号分配、容量检查、预留判断和固定记录复制；临界区内没有格式化或 I/O，也不获取业务互斥锁。ESP-IDF 跨核临界区可能短暂自旋，不能称为 wait-free。API 拒绝 ISR 调用；本包没有新增 ISR 日志通道。

- 详细 profile 过滤发生在事件序号分配前，不制造假缺号。
- 队列拒绝发生在分配序号后，分别累计 `normal_dropped` / `critical_dropped`；有限内存不保证关键事件永不丢失。
- 初始化不可用和 ISR 拒绝不分配事件序号；初始化不可用有计数，ISR 仅返回 `InvalidContext`，不在 ISR 获取 task 锁或输出日志。
- UTF-8 文本在完整字符边界截短，`truncated=1`；非法字节替换为 `?` 并标 `malformed=1`。CR/LF 归一为空格，避免输入生成伪造的额外日志行。详细格式化截短的尾部若包含不完整编码，也会显式标记。
- sink 的短写、零写或失败保留同一 frame 的 offset，恢复后从剩余字节继续；没有前缀重发，也不丢弃半行后直接拼接下一个事件。持续不可写时内存保持有界，健康报告也可能延后到恢复之后。

## 5. 串口格式与证据边界

正常长度的既有中文整行及前缀保留，新增身份在末尾：

```text
[波形参数] 参数已应用：频率=35.00 Hz，最终强度=80/120 [SW_DIAG schema=1 stream=0123456789abcdef seq=42 ts_us=123456 event=WAVE_PARAMETERS owner=wave level=info truncated=0 malformed=0]
```

- `stream` 是本次固件启动的随机 64 位标识，`seq` 是该流的诊断序号，`ts_us` 是设备单调时间。并发任务的采样时间不保证按序号严格递增。
- `event` / `owner` 是稳定诊断身份，不是正式 BLE 业务字段，也不是 ACK。
- 健康行以 `[诊断状态]` 开头，包含 `schema`、`stream`、`scope_mode=static`、`profile`、`backend`、`event_bytes`、队列/预留容量、`assigned`、`queued`、`peak`、两类 drop、`filtered`、`unavailable`、`write_errors`、`transport_uncertain`、`transport_accepted`、`collector_observed=unknown` 与 `ready`。
- `transport_accepted` 只表示最后一个整帧已交给驱动的事件序号，不能证明此前没有 drop，更不能证明电脑收齐。`transport_uncertain` 统计未能作端到端确认的写尝试，不等于精确丢失条数。
- 本包没有 writer flush/checkpoint 交接命令。健康快照只证明当时已发布状态；采集窗口尾部、掉电前未发布事件和系统日志完整性不能据此宣称已证明。

### UART 与 USB

当前默认构建由 Arduino `Serial` 使用 UART0，波特率沿用 115200。sink 使用 `uart_tx_chars` 将可用 FIFO 空间作为短写结果；该 API 内部仍可能等待驱动互斥锁，所以它只在后台调用。不能把该实现描述为整个驱动无等待。

既有 rollback USB 构建使用 USB Serial-JTAG/HWCDC；后台先检查连接和可写空间，单次写超时限制为 1 ms。USB 不按 UART 115200 估算吞吐。Arduino 2.0.17 的 HWCDC 在断开或缓存压力下可能清缓存/丢旧数据，即使 `write` 返回也不等于电脑收到，因此 USB 写结果始终保留不确定性。TinyUSB CDC 是条件代码，当前默认 SDK 配置未启用，不计入本轮已验证后端。

## 6. 节流与冻结的业务规则

`FirmwareLogPolicy::RateLimitState` 将“是否初始化”与 `lastMs` 分开，修复首次/强制记录在 0 ms 时，1 ms 被错误放行的问题。原有 BLE 非关键 `send_skipped`、`BLE_TX_PRESSURE` 的 2 秒间隔不变，支持 `uint32_t` 回绕。该修复不证明此前用户体感问题由此引起。

以下边界保持原样：

- `CAP?`、`SNAPSHOT`、`WAVE:*`、`EVT:STOP/SAFETY/FAULT/STREAM` 的字段、发送链和缓存清除合同。
- reconnect snapshot dirty / compensated、critical BLE frame 的入队策略。
- ESP32 状态机、摔倒/离开平台检测、保护开关、Plus 测量公式和动作时序。
- APP 的手动/方案会话、倒计时、实时调参与服务器行为。

## 7. 验证与剩余验收

正式入口（使用本地已安装 C++ 编译器，不下载依赖）：

```bash
python3 tools/run_firmware_log_policy_unit_tests.py --cxx <已安装的C++编译器>
```

2026-10-17 本地结果：5 个程序、79 条断言通过。覆盖正常/强制/回绕/0 ms 节流、宏惰性求值、任务初始化失败、ISR 拒绝、32/24/8 饱和边界、多生产者原子准入、并发生产/排空及 FIFO 序号、在飞计入容量、部分写/失败恢复、UTF-8/超长/换行、24 个中文接口、rollback 启动证据、静态 profile、非递归健康与资源上限。host 测试以真实诊断实现配合 RTOS/硬件替身；不代表设备性能。

既有 `tools/run_evaluator_unit_tests.py` 与 `tools/run_laser_reader_unit_tests.py` 通过；后者仅增加诊断接口替身，原测量断言不变。使用本地锁定 Xtensa GNU++11 工具链和已有依赖，对实际生产源执行 syntax-only（含 UART、USB Serial-JTAG 路径）；没有完整链接、固件镜像或安装产物。

仍须由真实硬件确认：任务栈高水位、真实 DRAM/链接布局、UART/USB 背压与断开恢复、系统插入日志、详细模式开关对控制/保护/波形/测量任务的耗时与连续输出影响，以及与电脑采集器的端到端字节/序号核对。旧固件无该 schema 时只能兼容采集，不能冒充本版序号证据。

## 8. 匹配锁定版本的官方依据

- [ESP-IDF 4.4.7 FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/freertos.html)：任务/ISR API、临界区、静态任务与栈单位；不意味着跨核临界区完全无等待。
- [ESP-IDF 4.4.7 UART](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/uart.html)：FIFO/缓冲/等待边界；还须结合本地驱动实现的锁。
- [Arduino ESP32 2.0.17 HWCDC](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)：USB 断开缓存、部分写与返回值的证据局限。

公开资料支持工程选择；本地源码/测试证明实现边界；真机采集才用于判定实际设备时序和输出表现。
