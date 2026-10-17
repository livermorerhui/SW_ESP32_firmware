# Firmware Log Policy

最后更新时间：2026-10-17

当前状态：已补施工前源码审计与原生策略测试；现有同步串口日志尚未迁移到有界后台writer。下文目标不得解释为已经完成的性能保证。

跨端统一标准见SW仓库 `docs/system/SW功能开发与Bug修复可观测性协议.md`，迁移见 `docs/system/SW跨端精准日志与采集优化施工蓝图-20261017.md` 与同日接口/测试矩阵。固件职责仅为诊断层；APP会话、BLE正式协议、ESP32输出及Plus保护语义冻结。

## 1. 目标

固件日志必须同时满足：

- 不阻塞或拖慢实时任务。
- 不在 BLE 断开、测量高频、stream 高频场景刷爆串口。
- 保留真机排障需要的关键证据。
- 保持现有 capture 脚本可识别的日志前缀。

## 2. 成熟方案对齐

参考原则：

- ESP-IDF 官方日志模型使用 tag、level、compile-time / runtime level 控制。
- FreeRTOS 日志建议避免应用任务被日志 I/O 阻塞。
- RTOS 任务优先级设计中，日志应属于低优先级或后台职责，不能抢占实时 I/O 与控制链路。

当前工程还大量使用 `Serial.printf`，因此第一阶段不直接大规模替换为 `ESP_LOGx`。原因是现有 capture、报告和跨端排障依赖固定文本前缀。

第一阶段成熟化策略：

- 保留现有关键日志前缀。
- 高频日志走统一 `FirmwareLogPolicy`。
- 当前关键事件仍直接打印；目标是即时有界提交，后台输出，不让保护/控制任务等待串口。
- 非关键高频事件只打印周期摘要。
- debug 细节继续由显式开关控制。

### 日常串口模式与排障模式

日常使用默认关闭底层诊断输出：

- `SERIAL_VERBOSE_DIAGNOSTICS=0`：只输出中文运行日志、重要警告和错误。
- `SERIAL_VERBOSE_DIAGNOSTICS=1`：恢复测量、BLE、波形、状态机和 OTA 的详细诊断日志。
- 两种模式都不改变 BLE 通信帧、APP 控制、状态机和波形行为。
- 详细日志代码保留，不在日常串口中打印；排障结束后必须恢复为 `0`。

## 3. 当前规则

### 关键事件

以下日志不应被普通节流吞掉：

- `EVT:STOP`
- `EVT:SAFETY`
- `EVT:FAULT`
- reconnect snapshot dirty / compensated
- enqueue failure
- reset / panic / Brownout / Guru

### 高频非关键事件

以下日志默认只能周期摘要：

- BLE 未连接时的 `send_skipped`
- `BLE_TX_PRESSURE`
- stream replace / suppress 类压力统计
- measurement diagnostic summary
- motion shadow observe-only summary

### BLE TX 当前实现

`FirmwareLogPolicy` 当前统一管理：

- `kBleTxSendSkipLogIntervalMs`
- `kBleTxPressureSnapshotIntervalMs`

当前行为：

- BLE 未连接时，非关键 `send_skipped` 约 2 秒最多打印一次。
- `BLE_TX_PRESSURE` 约 2 秒最多打印一次。
- critical frame 仍可即时触发 reconnect snapshot dirty。

## 4. 已定迁移方向与边界

1. 先实现纯准入策略、固定事件与有界队列，再按白名单迁移同步日志；保留中文文案和采集前缀兼容。
2. 首包接口只供任务调用。序号分配、容量检查、关键预留和固定记录复制原子完成；短临界区内不格式化、不做I/O、不持业务锁、不动态分配。
3. 唯一后台sink格式化并输出，部分写入保留offset。普通和关键队列满都立即返回并计数，不同步兜底；有限内存不能承诺关键日志绝不丢失。
4. 分开记录事件准入、驱动接纳和电脑观察。当前UART内部mutex、HWCDC断开缓存行为说明，write返回不能证明电脑收到。系统/ROM panic输出单列。
5. 默认仍为静态编译profile；电脑stop不代表固件明细已关闭，不新增APP/BLE诊断开关。排障profile恢复按下一次用户选择的构建执行。
6. 候选预算为96B事件、64条FIFO内预留16条关键、4KiB任务栈、512B格式化缓冲、总静态预算不超过16KiB；均须sizeof、饱和测试与实际栈/时序验证，不能写成已达标。

保持输出/测量/保护业务Owner；不全仓机械替换`Serial.printf`，不同时保留两套活跃writer。是否采用`ESP_LOGx`属于sink实现选择，不能替代业务调用端隔离。

## 5. 原生策略回归与已知缺口

测试入口：`tools/run_firmware_log_policy_unit_tests.py`。它用已安装C++编译器和临时Arduino stub编译现有头文件，不构建完整固件、不烧录、不操作设备、不下载依赖。

在仓库根目录，已配置本地C++编译器时运行：

```bash
python3 tools/run_firmware_log_policy_unit_tests.py
```

需要指定现有编译器时使用`--cxx <已安装编译器路径>`，不得为此自动安装工具。

2026-10-17实际结果：3个程序、16条断言通过（节流正常/强制/uint32回绕等10条，详细宏关闭/开启各3条）。详细宏关闭时参数不求值，开启时保留输出；测试不证明串口背压下的实际性能。

独立反例：`FirmwareLogPolicy.h`用`lastLogMs==0`同时表示未初始化。首次或强制记录发生在0ms后，1ms再次被允许，两条目标合同断言失败。该缺口登记A4修复，尚未修改生产代码；默认setup延迟1500ms，不能据此归因用户体感问题。不得把该错误行为写成正式通过断言。

新writer还须覆盖多生产者关键预留、队满、初始化失败、慢/部分写sink、中文完整行、失败后恢复、RAM/栈与UART/USB真实后端。结果和进度回写跨端蓝图。

## 6. 版本匹配的官方依据

- [ESP-IDF 4.4.7 FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/freertos.html)：任务/ISR API与临界区；本项目任务栈单位按字节理解。
- [ESP-IDF 4.4.7 UART](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/uart.html)：FIFO/缓冲与等待边界，仍须结合本地驱动锁实现。
- [Arduino ESP32 2.0.17 HWCDC](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)：USB断开缓存与返回值，不能当作电脑收齐回执。

以上支持工程合同，不替代本项目编译、资源、时序与真机验收。
