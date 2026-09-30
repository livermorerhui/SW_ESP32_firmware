# ESP32-S3 Pin Profiles

本文件是 ESP32-S3 N16R8 底座引脚切换的稳定入口。后续如果在新窗口继续协作，先读取本文件，再修改 `src/config/GlobalConfig.h` 与同步文档。

## 当前 active profile：新底座

新底座已到货并切换，固件 active profile 使用以下引脚：

| 链路 | 外设信号 | ESP32-S3 GPIO | 固件配置 |
|---|---|---:|---|
| PCM5102A / I2S | BCK | GPIO1 | `I2S_BCLK_PIN = 1` |
| PCM5102A / I2S | LRCK / WS | GPIO21 | `I2S_LRCK_PIN = 21` |
| PCM5102A / I2S | DIN | GPIO2 | `I2S_DOUT_PIN = 2` |
| 485 接口（按本项目 PCB 标记） | 485 RXD -> ESP32 GPIO17 | GPIO17 | `RX_PIN = 17` |
| 485 接口（按本项目 PCB 标记） | 485 TXD -> ESP32 GPIO18 | GPIO18 | `TX_PIN = 18` |
| CYD3S1612-IIC | SCL | GPIO15 | `I2C_SCL_PIN = 15` |
| CYD3S1612-IIC | SDA | GPIO16 | `I2C_SDA_PIN = 16` |

说明：
- `RX_PIN` / `TX_PIN` 是 ESP32 `Serial1.begin(..., RX, TX)` 视角。
- 本项目以实际能正常通信的 PCB 接线为准：485 `RXD` 接 GPIO17，485 `TXD` 接 GPIO18。不要再按其他 MAX485 模块的通用丝印规则交换这两个引脚。
- 如果使用裸 MAX485 芯片或非自动方向模块，还需要额外确认 `DE/RE` 方向控制 GPIO；当前 profile 只定义 RX/TX。
- 该新底座 I2S 方案避开了 `GPIO19/20` USB、`GPIO0/45` strapping、`GPIO35/36/37` Flash/PSRAM 风险脚，以及 `GPIO39~42` 外部 JTAG 相关 pad。

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
