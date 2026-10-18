# E8 阶段 B：旧 Base/Plus 升级前过桥

日期：2026-10-13

状态：USB 维护镜像已编译；未对任何实体旧设备执行，不能据此批准批量升级。

## 工作人员实际操作

1. **先核实实物。**查设备台账和机身，确认这是旧 Base 还是旧 Plus；记下设备原固件版本、物理传感器和序列号。旧 Pro/Ultra 不走此流程。
2. **在任何烧录前备份原机。**先用 Espressif `esptool` 读取整片 Flash，再读取原机分区表并按**原机分区表**的地址、大小单独备份 NVS。核对读取成功、文件大小与 SHA-256，保存设备与备份文件的对应关系。不能先烧维护镜像再备份；首次 `INSPECT` 发生在烧录维护镜像之后，不能代替烧录前的原始证据。下列命令仅供真机验证阶段由工作人员替换端口和目录后执行，本轮未执行：

   ```bash
   E8_PORT="/dev/cu.usbmodemXXXX"
   E8_BACKUP_DIR="/path/to/this-device-backup"
   E8_PY="/Users/wurh/.platformio/penv/bin/python"
   E8_ESPTOOL="/Users/wurh/.platformio/packages/tool-esptoolpy/esptool.py"
   E8_PARTTOOL="/Users/wurh/.platformio/packages/framework-espidf/components/partition_table/gen_esp32part.py"
   umask 077
   mkdir -p "$E8_BACKUP_DIR"
   "$E8_PY" "$E8_ESPTOOL" --chip esp32s3 --port "$E8_PORT" --baud 115200 flash_id
   "$E8_PY" "$E8_ESPTOOL" --chip esp32s3 --port "$E8_PORT" --baud 115200 get_security_info
   "$E8_PY" "$E8_ESPTOOL" --chip esp32s3 --port "$E8_PORT" --baud 115200 read_flash 0 ALL "$E8_BACKUP_DIR/full_flash.bin"
   # 以下 0x8000 仅适用于已证实沿用本工程分区表偏移的旧机。
   "$E8_PY" "$E8_ESPTOOL" --chip esp32s3 --port "$E8_PORT" --baud 115200 read_flash 0x8000 0x1000 "$E8_BACKUP_DIR/partitions.bin"
   "$E8_PY" "$E8_PARTTOOL" "$E8_BACKUP_DIR/partitions.bin" "$E8_BACKUP_DIR/partitions.csv"
   # 从原机 partitions.csv 取得 NVS 的真实 Offset、Size 后，替换下面两行示意值：
   E8_NVS_OFFSET="OLD_NVS_OFFSET_FROM_CSV"
   E8_NVS_SIZE="OLD_NVS_SIZE_FROM_CSV"
   "$E8_PY" "$E8_ESPTOOL" --chip esp32s3 --port "$E8_PORT" --baud 115200 read_flash "$E8_NVS_OFFSET" "$E8_NVS_SIZE" "$E8_BACKUP_DIR/nvs.bin"
   stat -f '%z %N' "$E8_BACKUP_DIR/full_flash.bin" "$E8_BACKUP_DIR/partitions.bin" "$E8_BACKUP_DIR/nvs.bin"
   shasum -a 256 "$E8_BACKUP_DIR/full_flash.bin" "$E8_BACKUP_DIR/partitions.bin" "$E8_BACKUP_DIR/nvs.bin"
   ```

   上述 `E8_NVS_OFFSET` 与 `E8_NVS_SIZE` 是待替换的示意值，不能原样执行。ESP-IDF 默认分区表偏移与本工程 `sdkconfig` 均为 `0x8000`；目标固件 CSV 中 NVS 为 `0x9000`、`0x5000`，但它们都**不能证明旧实机的布局**。若无法确认旧机分区表偏移、表无法解码、NVS 地址与目标烧录区域冲突，或检测到加密/安全配置而无法确认备份可用，停止烧录并制定单独迁移方案。不得运行 `erase_flash`、`erase_region` 或带整片擦除的上传命令。
3. **先离线看原始 NVS，再决定是否需要维护镜像。**使用 Espressif **ESP-IDF v5.1.7** 的官方 `components/nvs_flash/nvs_partition_tool/nvs_tool.py` 解析第 2 步保存的 `nvs.bin`。当前 PlatformIO 附带的 ESP-IDF 4.4 包没有这个工具；须使用经核实的官方 v5.1.7 源码，不得把其他版本或自写解析器的输出当作放行证据。以下命令只读备份文件，不访问设备：

   ```bash
   E8_IDF_517="/path/to/verified/esp-idf-v5.1.7"
   E8_NVS_TOOL="$E8_IDF_517/components/nvs_flash/nvs_partition_tool/nvs_tool.py"
   python3 "$E8_NVS_TOOL" "$E8_BACKUP_DIR/nvs.bin" --color never -d namespaces > "$E8_BACKUP_DIR/nvs_namespaces.txt"
   python3 "$E8_NVS_TOOL" "$E8_BACKUP_DIR/nvs.bin" --color never -d minimal > "$E8_BACKUP_DIR/nvs_values.txt"
   python3 "$E8_NVS_TOOL" "$E8_BACKUP_DIR/nvs.bin" --color never -d written -i > "$E8_BACKUP_DIR/nvs_written_integrity.txt"
   ```

   将命名空间索引、当前写入项的类型与数值对照核验，保存三份输出；完整性检查的告警必须人工检查，不能只看进程退出码。若原始备份明确证明 `scale_cal/cfg_model` 是唯一有效的 `uint8_t=1`，与台账和实物 Plus 一致，`cfg_laser` 缺失或为不冲突的 `uint8_t=1`，且实机与目标分区布局兼容，**跳过维护镜像和 `BRIDGE`，直接进入第 7 步**，之后仍须回读确认为 Plus。旧 Base 同理要求 `cfg_model` 为 `uint8_t=0` 并与实物一致。只有原始备份证实型号键缺失/坏类型，或激光兼容键为坏类型，且设备身份与备份齐全，才考虑第 4～6 步的维护镜像；仅 `cfg_laser` 缺失而型号键有效，无须为了保留型号烧维护镜像。加密 NVS、CRC/完整性告警、无法解析、有效值冲突或结果含糊，都须停止；不能把解析失败当作缺键。
4. **只有需要修复时才烧维护镜像并记录第一次 `INSPECT`。**镜像第一次启动后、发送任何 `BRIDGE` 命令前，再按原机 NVS 地址和大小读取并逐字节比较；不同即停止，不把后续缺键判为原机缺键：

   ```bash
   "$E8_PY" "$E8_ESPTOOL" --chip esp32s3 --port "$E8_PORT" --baud 115200 read_flash "$E8_NVS_OFFSET" "$E8_NVS_SIZE" "$E8_BACKUP_DIR/nvs_after_bridge_boot.bin"
   cmp "$E8_BACKUP_DIR/nvs.bin" "$E8_BACKUP_DIR/nvs_after_bridge_boot.bin"
   ```

   随后保存完整 `INSPECT` 串口输出，其中应有稳定设备 ID、`cfg_model` 与 `cfg_laser` 的键存在性、类型和值；`type=NON_U8` 表示原键类型不适合按型号读取。旧板若整个命名空间不存在，会输出 `INSPECT namespace=ABSENT` 及两个缺键行；必须结合烧录前备份和实物记录核实，不能仅凭这行认定旧机原本缺键。若输出 `INSPECT_ERROR reason=NVS_OPEN_FAILED` 或 `NVS_READ_FAILED`，是读取故障，须隔离，不能当作缺键修复。不能用 APP 显示的型号或旧 ACK 代替 NVS 直读。
5. **仅需修复时写入。**若 `cfg_model` 已是 `PT_U8` 且与实物相符，保持原值；若缺键、缺整个命名空间或坏类型，在保存并核对原机备份与第一次 `INSPECT` 后执行如下命令。设备 ID 必须与刚才的 `INSPECT` 完全相同。维护镜像拒绝覆盖一个已经有效但与输入型号不同的 `cfg_model`；型号读取失败或读到未知值也会拒绝写入。

   ```text
   BRIDGE model=PLUS id=sw1_<INSPECT显示的32位十六进制> evidence_saved=1
   ```

   旧 Base 使用 `model=BASE`。看到 `BRIDGE_WRITE_OK_PENDING_POWER_CYCLE_VERIFY` 只表示这次写入与即时读回一致，**还不是放行升级**。
6. **断电后再次直读。**ESP32 完全断电、重新上电，再发 `INSPECT`；必须看到相同设备 ID，`cfg_model` 类型 `PT_U8` 且值为 Base=0 或 Plus=1，`cfg_laser` 类型 `PT_U8` 且值分别为 0 或 1。保存这次日志。任何失败、NVS 打不开或型号冲突，都停止升级并保留原始数据。
7. **再刷统一固件。**无论是第 3 步直接放行，还是第 4～6 步过桥后放行，都须确认上传命令不会整片擦除、旧机与目标分区布局兼容，再使用保留 NVS 的普通应用升级；重启后发 `DEVICE:GET_CONFIG` 核对原 Base/Plus。旧 Plus 在选定传感器前仍是 `PLUS+NONE`，不能投入日常使用；工作人员继续用新版 Demo APP 选择实际传感器。旧镜像回刷只在受控测试中进行，不能将旧键可读当成旧镜像理解新 profile 的证据。

维护镜像在 `platformio.ini` 中名为 `e8_legacy_config_bridge`，源码位于 `test_firmware/e8_legacy_config_bridge/main.cpp`。它只走 USB 串口，没有 BLE、波形输出或传感器探测；**其业务代码**启动后只检查、不因缺键自动修复。但该环境使用 Arduino Core：本机所用 Core 的 `initArduino()` 在 `nvs_flash_init()` 返回 `ESP_ERR_NVS_NO_FREE_PAGES` 或 `ESP_ERR_NVS_NEW_VERSION_FOUND` 时，会在进入维护镜像 `setup()` 前擦除 NVS 分区（`/Users/wurh/.platformio/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c:249-255`）。所以不能把“业务代码启动只读”理解为“整个启动链绝不改写 NVS”；若烧录前备份与首次 `INSPECT` 不一致，立即停止，不得把擦除后的缺键当成原机缺键继续过桥。构建命令：

```bash
/Users/wurh/.platformio/penv/bin/pio run -e e8_legacy_config_bridge
```

本轮维护镜像主机故障注入测试 `python3 tools/test_e8_legacy_bridge.py` 已覆盖缺命名空间、NVS 打开失败、已有 Plus 型号读失败、型号冲突和显式过桥。维护镜像构建已通过；尚未烧录或对旧板做断电/重烧验证。真实设备验收前需要准备串口/NVS 自动采集命令和操作标记，再由工作人员执行物理操作。

依据：[Espressif esptool 基本命令](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)（`flash-id`、`read-flash 0 ALL`、擦除命令）、[ESP-IDF 分区表](https://docs.espressif.com/projects/esp-idf/en/v5.1.1/esp32s3/api-guides/partition-tables.html)（默认偏移、二进制表解码）、[ESP-IDF v5.1.7 NVS 解析工具](https://docs.espressif.com/projects/esp-idf/en/v5.1.7/esp32s3/api-reference/storage/nvs_partition_parse.html)（只读解析、类型/值输出和完整性检查；不支持解密）、[Espressif 固件烧录说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/flashing-firmware.html)（PlatformIO verbose 上传会显示实际写入区域）、[Espressif Preferences API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)（`getUChar` 失败时返回默认值，不能用它证明读取成功）、[Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html)（`nvs_open`/`nvs_get_u8` 错误码、掉电与空间故障）、[PlatformIO build_src_filter](https://docs.platformio.org/en/stable/projectconf/sections/env/options/build/build_src_filter.html)（独立维护镜像的源文件范围）。本项目的人员核实、命令和放行规则以 [阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md) 为准。
