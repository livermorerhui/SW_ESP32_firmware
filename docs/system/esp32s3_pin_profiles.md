# ESP32-S3 Pin Profiles

本文件是 ESP32-S3 N16R8 底座引脚切换的稳定入口。后续如果在新窗口继续协作，先读取本文件，再修改 `src/config/GlobalConfig.h` 与同步文档。

## 当前 active profile：新底座

新底座已到货并切换，固件 active profile 使用以下引脚：

| 链路 | 外设信号 | ESP32-S3 GPIO | 固件配置 |
|---|---|---:|---|
| PCM5102A / I2S | BCK | GPIO1 | `I2S_BCLK_PIN = 1` |
| PCM5102A / I2S | LRCK / WS | GPIO21 | `I2S_LRCK_PIN = 21` |
| PCM5102A / I2S | DIN | GPIO2 | `I2S_DOUT_PIN = 2` |
| MAX485 模块 | TXD / RO -> ESP32 RX | GPIO17 | `RX_PIN = 17` |
| MAX485 模块 | RXD / DI <- ESP32 TX | GPIO18 | `TX_PIN = 18` |

说明：
- `RX_PIN` / `TX_PIN` 是 ESP32 `Serial1.begin(..., RX, TX)` 视角。
- MAX485 模块丝印通常按模块视角命名，因此模块 `RXD/DI` 接 ESP32 TX，模块 `TXD/RO` 接 ESP32 RX。
- 如果使用裸 MAX485 芯片或非自动方向模块，还需要额外确认 `DE/RE` 方向控制 GPIO；当前 profile 只定义 RX/TX。
- 该新底座 I2S 方案避开了 `GPIO19/20` USB、`GPIO0/45` strapping、`GPIO35/36/37` Flash/PSRAM 风险脚，以及 `GPIO39~42` 外部 JTAG 相关 pad。

## Legacy profile：旧底座

旧底座如需回退，使用以下引脚：

| 链路 | 外设信号 | ESP32-S3 GPIO | 固件配置 |
|---|---|---:|---|
| PCM5102A / I2S | BCK | GPIO4 | `I2S_BCLK_PIN = 4` |
| PCM5102A / I2S | LRCK / WS | GPIO5 | `I2S_LRCK_PIN = 5` |
| PCM5102A / I2S | DIN | GPIO6 | `I2S_DOUT_PIN = 6` |
| MAX485 / Modbus | ESP32 RX | GPIO15 | `RX_PIN = 15` |
| MAX485 / Modbus | ESP32 TX | GPIO14 | `TX_PIN = 14` |

说明：
- 旧底座 profile 仅用于回退或对照，不再是当前 active 固件默认值。

## 切换时必须同步的文件

1. `src/config/GlobalConfig.h`
2. `docs/hardware.md`
3. `docs/architecture/current/system_master.svg`
4. 如本轮需要留痕，补充 `reports/` 阶段记录

## 切换验证

每次切换后至少执行：

```bash
~/.platformio/penv/bin/pio run -e esp32s3
```

真机验证进入硬件阶段后再执行：
- 烧录目标固件到对应底座。
- 串口启动日志无 boot / reset / brownout 异常。
- BLE 可连接并响应 `CAP?`。
- `WAVE:START` 后示波器或逻辑分析仪能看到 I2S `BCK / LRCK / DIN`。
- PLUS 测量链无连续 `Modbus read fail`，且能产生有效 `EVT:STREAM`。
