# 产品构建与工厂导出

本说明对应 2026-10-24 至 2026-10-25 的构建入口收口；不修改 Base/Plus、ready、保护、波形和 OTA 协议。

## 操作顺序

1. VSCode 打开固件仓 `main`，使用默认 `esp32s3_unified` 环境。`firmware_release.json` 是产品版本和正式构建环境的唯一入口，当前版本为 `SW-ESP32-1.1.3`。
2. Build/Upload 读取当前源码与固定构建配置，生成 `u113-<12位内容摘要>` 构建号。文档或 Git 提交号变化不改构建号；源码、版本文件、CMake、平台配置、引脚或分区变化会改构建号。
3. 构建入口把版本、构建号、UNIFIED 后端选择写入构建目录中的生成头；`src/CMakeLists.txt` 强制让每个应用编译单元包含它。根 CMake 也使用同一产品版本设置 ESP-IDF app descriptor。
4. 构建成功后，检查实际 component 编译命令、当前源码清单、实际 SDK 配置和镜像内身份，生成 `.pio/build/esp32s3_unified/product_build.json`。
5. 用户在对应硬件上验收此候选；验收记录必须绑定构建号和应用 SHA-256。
6. SW 仓的工厂导出器读取这份凭据并原样复制四个镜像。OTA 入口复用同一个 `firmware.bin`；工厂包和 OTA 不再各自重编译。

默认 Upload 始终构建当前工作区，不能自动知道用户是否验收过新改动。要称为“最新验证过的固件”，必须同时具有构建凭据与对应二进制的验收记录。

## 唯一入口和校验

```bash
# 构建（VSCode Build/Upload 使用同一默认环境）
~/.platformio/penv/bin/pio run

# 查看当前源码应生成的产品身份，不触发构建
python3 tools/product_build_contract.py --identity-only

# 核对已经完成的产品构建，不重编译、不烧录
python3 tools/product_build_contract.py
```

正式导出和 OTA 打包使用 SW 仓的 `scripts/ota/` 工具，合同见
[`esp32-unified-upload-factory-contract.md`](../../../SW/docs/system/firmware-release/esp32-unified-upload-factory-contract.md)。
旧 `.github/workflows/release.yml` 已删除：它曾取任意找到的 `firmware.bin`，再用 Git tag/SHA 填清单，未证明清单身份与应用身份相同。GitHub tag 不再自动生成另一种发布包。

`product_build.json` 记录 schema、版本、构建号、环境、板型、构建校验时源码提交、全部仓内构建输入摘要、实际 SDK 配置摘要，以及 `firmware.bin / bootloader.bin / partitions.bin / ota_data_initial.bin` 的 SHA-256 与长度。构建校验提交与导出提交可能因纯文档提交不同，源码内容摘要必须一致。

固定 SDK 配置从已提交的 `sdkconfig.esp32s3` 复制到构建目录；忽略文件 `sdkconfig.esp32s3_unified` 不再决定产品配置。兼容发布保持 rollback 开启、硬件 anti-rollback 关闭、secure version 为 0。关闭编译日期时间嵌入，避免同源码重编译仅因时钟变化产生新镜像；这不是对任意机器/工具链完全字节重现的承诺。

## 失败与受控测试

- 每次真实构建尝试先写 `product_build.pending` 并撤销旧凭据；失败或取消后禁止借旧凭据导出。成功校验后才移除标记。
- 源码改变、实际宏缺失、SDK 配置选用错误、镜像身份或哈希不符，一律拒绝产品导出。
- 显式 `SW_FIRMWARE_RELEASE_VERSION` 与 `SW_FIRMWARE_RELEASE_BUILD_ID` 仍可共同指定受控版本，但标为 `explicit`，不生成默认产品凭据。请移除临时环境变量后再运行正式产品 Build/Upload。
- 单后端兼容环境与 USB 回退测试环境保留，均不属于默认产品工厂导出。USB 测试继续使用 `SW_FIRMWARE_TEST_VERSION/BUILD_ID`，故障注入仅属于专用测试环境。
- 工厂工具只复制镜像、调用官方 esptool 分段写入；不清空整片，不把已有 NVS 中 Plus 配置当作固件版本的一部分。

## 证据范围

此前测试的“unified”名称、或 CMake 的 `compile_commands.json` 单独都不能证明最终编译宏：PlatformIO 会在 CMake 配置之后补充 SCons 参数，必须结合最终编译命令或预处理结果核对。此次把身份和后端配置显式绑定到 component 并检查生成头，因此新产物仍需绑定本次二进制的真机验证，旧 Base/Plus、正弦波或 1.1.2 OTA 反馈不能自动覆盖新 1.1.3。不能仅因旧 CMake 清单未显示宏，就断言此前测试的固件缺少 UNIFIED 后端。

主机 focused tests 覆盖源码漂移、漏宏、缓存源清单遗漏、错误身份、SDK 选用、被改镜像、缺文件与未成功构建；编译和真实设备验证分别记录在 SW 施工文档。构建凭据不等于硬件验收证明。

## 官方资料

- [PlatformIO Pre & Post Actions](https://docs.platformio.org/en/stable/scripting/actions.html)：构建后与 Upload 前附加检查。
- [ESP-IDF 4.4.7 构建系统](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/build-system.html)：component 编译配置与 PROJECT_VER。
- [ESP-IDF 4.4.7 配置参考](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/kconfig.html#config-app-compile-time-date)：编译日期时间开关。
- [esptool v4 基本命令](https://docs.espressif.com/projects/esptool/en/release-v4/esp32s3/esptool/basic-commands.html)：按地址写入与擦除边界。

最终实施使用 post hook `tools/product_build_evidence.py`：应用对象显式依赖生成头，独立 AlwaysBuild 验证目标依赖四个镜像，并接入 buildprog/upload。已用首次构建、noop 和提交后 noop 验证四镜像哈希不变、凭据重新生成、pending 清除。`firmware_commit` 表示该次校验所对应源码提交，不是每个镜像首次编译时间；最终归档与真机验收状态见 SW 合同第 10 节。
