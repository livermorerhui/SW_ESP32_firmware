# E8 阶段 B：固件配置层代码收口与验收边界

日期：2026-10-13
状态：**代码层收口（有限）**。固件配置代码、维护镜像、存储/协议/reader/BLE 停波的独立主机测试和构建已有通过记录；完整 LaserModule→状态机→BLE 主链、旧机过桥、旧镜像回刷和真机运行待验收。本记录不是现场批量升级放行单，也不把阶段 B 全部测试项标为通过。

## 工作人员会看到的流程

1. 确认是全新板：烧录统一固件，上电为 Base，不启动 485 或 IIC。工作人员首次明确写入 `BASE+NONE` 后，ESP32 才把 Base 配置保存到 NVS；重启能读回。
2. 确认是旧 Base/Plus：升级前按 [过桥操作说明](./e8_stage_b_legacy_bridge_20261013.md)保存身份和原 NVS 证据，必要时人工补写原型号。普通升级保留原 Base/Plus；旧 Plus 暂为 `PLUS+NONE`，还需新版 Demo APP 选实际传感器并设置空载距离。**本轮没有对任何实体旧设备做过桥。**
3. 工作人员通过新命令选 J100-485 或 CYD-IIC：ESP32 停止旧 reader、隔离旧样本及待发距离帧，保存并回读配置，再启动新 reader。设置成功须由 Demo APP 完成独立 GET、重连和重新订阅；阶段 B 成文时 Demo APP 尚未接入，阶段 C 的进度另见 [Demo APP 记录](./e8_stage_c_demo_app_progress_20261013.md)。
4. 平台空载且有新鲜距离时，`EMPTY:SET_CURRENT` 让 ESP32 保存自己的当前距离，并返回实际值、profile、配置代际和空载修订号。体重模型与空载距离分别保存；清除/重设空载距离使旧模型绑定失效，清体重参数不删除空载距离。

## 已实现的固件边界

- `DEVICE:GET_CONFIG` 与 `DEVICE:SET_CONFIG platform_model=...,sensor_profile=...` 使用独立 schema-1 回复。新 SET 不靠 UART/IIC 总线猜型号；旧 `DEVICE:SET_CONFIG` 保留兼容入口。`ACK:CAP`、`SNAPSHOT` 与原控制命令没有加入新字段。
- 新配置使用双槽、CRC、提交标记、generation 与旧 `cfg_model/cfg_laser` 镜像。完整但不支持的 schema/profile 不降回旧值；存储或跨键状态不确定时禁开始。配置故障停波保留 `stop_reason=CONFIG_ERROR`，`stop_source=NONE`、`effect=NONE`，避免 SW APP 将维护故障归成正式安全停机。
- Base 不探测传感器；Plus 按保存的 profile 启动 reader。`PLUS+NONE`、缺空载点、坏配置都不能通过开始门禁。CYD 目前只开放配置/距离链；任一保护开启时不允许把未验收的临时阈值当作受保护开始依据。停止波形的缓降尚未结束时，配置、空载和体重模型写入返回失败。
- BLE 待发 `EVT:STREAM` 帧在配置成功及配置故障返回前按代际失效；正式 APP 不按名称猜是否订阅流。APP 仍须在阶段 C 按合同重连并重新订阅。
- `EMPTY:SET_CURRENT`、旧空载写命令和体重校准写命令在 OTA 忙碌期被拦截；空载读取仍可用。参数清零等体重模型持久化清除并读回成功才 ACK，且成功后继续发送旧 `EVT:PARAM`；失败返回 NACK 且不发参数事件。清零会短暂重建稳定链，体重与距离状态链的最终解耦仍属后续阶段。
- 空载点记录读取失败或长度读取异常时返回配置错误，不自动取上一槽的旧空载距离；完整读出后 CRC 损坏仍按双槽恢复规则处理。

## 已执行验证

本次收口审计先发现并修复四处代码风险：最新空载槽读取失败回取旧值、OTA 期间仍可写空载/校准、旧机过桥把型号读取失败当作未设置、清空体重模型后漏发旧参数事件。相应故障注入与生产方法主机检查通过；复核未见剩余 P0/P1 代码问题。以下为代码层证据，不能代替真机验收。

- `python3 tools/run_evaluator_unit_tests.py`：通过，覆盖原有 evaluator、新命令解析和部分旧帧逐字 golden 检查（CAP、两种 SNAPSHOT、摔倒 STOP 等）；`EVT:STREAM` 当前是字段断言，不是逐字 golden。
- `python3 tools/run_device_config_store_unit_tests.py`：通过，覆盖新板/旧机读取、显式同值迁移、断电重读、双槽损坏/中断/写读失败等存储向量。
- `python3 tools/test_profile_empty_distance_store.py`：通过，覆盖 profile/generation/revision 绑定、写入失败、清除 tombstone 回退，以及最新槽读取失败不得恢复旧空载距离。
- `python3 tools/run_laser_reader_unit_tests.py`：通过，直接编译生产 reader，覆盖 J100↔CYD 热切、旧读取 epoch 隔离、Base 不开传输、485 数据转换及切换等待在途读取。
- `python3 tools/test_stage_b_stream_and_stop.py`：通过，直接编译生产 BLE 队列、OTA 命令门禁、体重清零和状态机相关方法，覆盖旧流帧/旧会话隔离、控制 ACK 保留、OTA 期禁止写入、成功清零后参数事件和失败不发事件、配置故障停波及阻断故障早退后的待处理原因清理。
- `python3 tools/test_e8_legacy_bridge.py`：通过，直接编译维护镜像主逻辑并注入 NVS 错误；覆盖命名空间不存在与打开失败区分、已有 Plus 型号读失败/冲突不得改 Base、激光兼容位读失败阻断以及显式过桥。
- `/Users/wurh/.platformio/penv/bin/pio run -e esp32s3_unified -e esp32s3 -e e8_legacy_config_bridge`：三环境构建通过；`git diff --check` 通过。构建只说明代码可生成固件，不说明硬件正常。

## 测试矩阵如何判定

| 项目 | 本次代码证据 | 尚缺的证据 |
|---|---|---|
| E8-C01/C03 | 空白与旧键读取、双槽提交/中断/CRC/未知记录等存储主机测试通过 | 旧板普通重烧、断电和 NVS 实物结果 |
| E8-C02 | 维护镜像构建及主机故障注入通过；`INSPECT` 区分命名空间不存在与 NVS 故障，`BRIDGE` 的已存型号读失败不覆盖 | 旧设备身份台账、原 NVS 备份、维护镜像真机串口日志、写后断电直读；不能凭主机测试放行 |
| E8-P01/P02 | 固件新命令解析、新配置 ACK/GET 编码和部分旧帧 golden 通过 | Demo APP 的 ACK→GET/断线/迟到回包判定属阶段 C；`EVT:STREAM` 逐字 golden、实际低 MTU 分帧及 BLE 收发仍待验证 |
| E8-M01/M04 | 生产 reader 热切、BLE 待发帧失效、配置故障停波等方法级测试通过；空载记录的 profile/generation 绑定有存储测试 | LaserModule 对单帧/停更/旧代际/过期样本、NVS 失败后旧 reader 恢复、`EMPTY:SET_CURRENT` 拒绝路径和停波缓降交错仍缺完整集成回放或实物证据 |
| OTA/旧镜像 | 当前统一、旧环境及维护镜像可构建；新固件的旧键镜像写读有存储测试 | 真实旧镜像读取新旧键、受控回刷、自动回滚及普通升级保留 NVS 的证据 |

这些通过记录是代码层证据，不等于 E8-C/P/M 的所有场景已验收。阶段 C 可继续实现 Demo APP 的配置入口；在设备联调、旧机升级或对用户开放空载设置前，须补齐相应门槛。

## 尚未通过，下一阶段不能跨过

- E8-M 已有 reader、BLE 队列与停波方法的直接主机测试，但 FreeRTOS/GATT/Wave 用替身；LaserModule→状态机→BLE 全链、实际调度下在途通知、停波缓降交错仍缺集成回放或实物证据。本轮的主机测试不能代替这些行为验收。
- 尚未烧录板卡、安装新 Demo APP 或做旧 Base/Plus 设备台账/NVS 过桥。NVS CRC 损坏回退到上份有效配置是维修恢复；同为 Plus 的不同 profile 仍需工作人员核对现场实物。
- 健康 Plus 双保护关闭免站稳开始、运行中重开保护、SW APP 摔倒开关重连回显、J100 距离判断公式及 CYD 保护算法属于后续阶段；本轮不宣称已实现或验收。J100 的空载容差 `0.10 mm` 只是已讨论的初值，CYD 容差和采样新鲜度仍需实测定值。
- 阶段 B 没有改 SW APP、服务器或 Demo APP 页面；阶段 C 单独接入工作人员操作路线。本报告成文时阶段 B 代码尚未提交；后续分类提交不代表真机验收或发布。

回撤点：`backup/e8-before-stage-b-20261013`（`2db3cff6bf453211dfa13d20744c172b5b7cf77e`），包含阶段 B 开始前工作区文件快照。恢复前先比较当前改动和该分支，勿用 `reset --hard` 丢失现场证据。

依据：[阶段 A 合同](./esp32_base_plus_sensor_profile_stage_a_contract_20261013.md)、[施工蓝图](./esp32_base_plus_sensor_profile_app_configuration施工蓝图.md)、[Espressif Preferences API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)、[Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html)、[Espressif OTA 回滚](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html)。
