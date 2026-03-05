# 构建说明（干净机器）

## 1. 环境要求
- OS: macOS / Linux / Windows（WSL）
- Python 3.9+
- Git
- USB 驱动（若后续烧录/串口监视）

## 2. 获取代码
```bash
git clone https://github.com/livermorerhui/SW_ESP32_firmware.git
cd SW_ESP32_firmware
git checkout main
```

## 3. 安装 PlatformIO Core
```bash
python3 -m pip install --user -U platformio
# 或使用 pipx 安装
# pipx install platformio
```

> 如果 `pio` 不在 PATH，可直接使用：`~/.platformio/penv/bin/pio`

## 4. 安装依赖并构建
```bash
# 首次会自动拉取平台与库
pio run

# 若 PATH 无 pio
~/.platformio/penv/bin/pio run
```

## 5. 烧录与串口（可选）
```bash
pio run -t upload --upload-port <PORT>
pio device monitor -b 115200 --port <PORT>
```

## 6. 项目关键配置
- 工程类型：PlatformIO + Arduino
- 配置文件：`platformio.ini`
- 目标环境：`[env:esp32s3]`
- 板卡：`esp32-s3-devkitc-1`
- 依赖库：`4-20ma/ModbusMaster`

## 7. 本次审计构建结果
- 命令：`~/.platformio/penv/bin/pio run`
- 结果：成功（`[SUCCESS]`）
- 备注：审计前存在 BLE 私有访问编译错误，已在补丁中修复。

## 8. 常见坑
1. `pio: command not found`
- 处理：改用 `~/.platformio/penv/bin/pio` 或把该目录加入 PATH。

2. 串口占用导致上传失败
- 处理：关闭串口监视器/IDE 串口窗口后重试。

3. 板卡不一致
- 处理：核对 `platformio.ini` 的 `board = esp32-s3-devkitc-1` 与实际硬件。

4. BLE 库版本波动
- 处理：删除 `.pio/` 后重新 `pio run`，确保依赖重解。
