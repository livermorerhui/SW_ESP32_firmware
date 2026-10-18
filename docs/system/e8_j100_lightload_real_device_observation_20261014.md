# E8 J100-485 停波时轻载距离观察（2026-10-14；5 kg 标签已更正）

## 现场路线与结果

这次使用已设为 Plus/J100-485 的同一台 ESP32，空载参考距离 `D_e=112.34 mm`。工作人员始终没有点 Demo APP 常驻底栏的「开始」，律动仪保持停波。先放置 3 kg，再做一次当时标称“5 kg”的放载；每次手离开后保持约 10 秒，取下后再等空载恢复。用户随后确认两次使用同一托盘、同一位置，**并在下一轮操作时纠正：这次标称 5 kg 实际放少了，实际重量未确认。**托盘受力点尚无独立量测记录。

| 操作顺序 | 用户看到的实时距离 | 采集标记前 15～1 秒 BLE 有效距离范围／中位数 | 相对本轮空载的变化 | 常驻底栏「开始」 |
|---|---:|---:|---:|---|
| 平台空载 | 112.33 mm | 112.32～112.33／112.33 mm | 基准 | 灰色不可点 |
| 放稳 3 kg | 112.03 mm | 112.02～112.04／112.03 mm | 减少约 0.30 mm | 灰色不可点 |
| 取下 3 kg | 112.33 mm | 112.32～112.33／112.33 mm | 恢复 | 灰色不可点 |
| 标称 5 kg、事后确认欠载（实际重量未确认） | 111.98 mm | 111.97～111.98／111.98 mm | 减少约 0.35 mm | 灰色不可点 |
| 取下该次负载 | 112.33 mm | 112.32～112.33／112.33 mm | 恢复 | 灰色不可点 |

`demo_transport_logcat_live.log` 覆盖 11:13:35～11:24:36，包含各 5567 条 `EVT:STREAM` 和 `EVT:BASELINE`。测距帧均为 `distance_valid=1`、`weight_valid=0`、`backend=LASER_485`；状态帧全程 `start_ready=0 baseline_ready=0`。两次放载的截图与当时的用户口述、BLE 读数一致；截图不能证明实际放置重量，故以事后纠正为准。本轮未见 `WAVE:START/STOP`、Demo APP 断连或崩溃。

## 如何理解这次结果

当前 J100 的未律动距离链采用内部单位 `mm/100`：进入“有人”的代码现值为偏离 `D_e` 至少 `0.03`，即 **3 mm**，且连续 2 个有效样本；J100 没有覆盖这个默认值。由本轮 BLE 距离算得，3 kg 相对 `D_e` 偏离约 **0.31 mm**、重量未确认的第二次放载偏离约 **0.36 mm**，所以两次都没有触发当前“有人”门槛，按钮仍灰色符合现行代码。这是**代码现值的解释，不是对 3 mm 的产品验收**。

旧体重版的“5 kg 进入、3 kg 退出”仅是距离参数设计的参照；即使未来确定 5 kg 应触发“有人”，还需要稳定基线等条件才能让「开始」变绿。**111.98 mm 不能用于分析 5 kg 载荷或与 3 kg 算斜率**；只能作为重量未确认、同托盘位置的一次距离变化。需要另以确认重量的样本和多轮回弹、晃动反例回放冻结参数。

## 原始证据与缺口

采集场景 `e8_j100_3kg_5kg_idle_gate`：

`/Volumes/MACminiSSD/SW/.artifacts/device-test-captures/20261014_111332__Xiaomi_MI_8__esp32_plus_normal__J100-485__manual__ESP32_RUNTIME__e8_j100_3kg_5kg_idle_gate/`

- `notes.md` 有五个现场操作时间标记，但其中“5 kg”是**当时的误标**；用户在下一轮明确纠正，分析时以本文更正为准。`visual_evidence/` 有每个标记的页面截图；`demo_transport_logcat_live.log` 是独立实时采集、覆盖全程的 Demo BLE 日志。
- `android_logcat_full.log` 是停止时快照，仅覆盖末段，不能单独证明五阶段；SW 通用脚本采集的 `app_logs.jsonl` 属于正式 SW APP，不作为 Demo APP 证据。服务器不参与这条本地操作链。
- **ESP32 串口证据未通过：**`esp32_serial.log` 为 0 字节，`esp32_reader_state.env` 为 `connected_waiting_for_valid_line`，`warnings.log` 明示 `esp32_serial_capture_insufficient`。静态轻载未造成状态切换可能使默认简略串口无输出，但本轮不能用串口独立复核阈值和计算过程。因此本轮是**带标签的距离采数与手机 BLE／页面观察，非固件双端完整验收**。

相关代码：[距离链默认参数](../../src/modules/laser/DistanceStateChain.h)、[有人与站稳判断](../../src/modules/laser/DistanceStateChain.cpp)、[J100 profile 配置](../../src/modules/laser/LaserModule.cpp)。设计门槛见[施工蓝图 E8-J05](./esp32_base_plus_sensor_profile_app_configuration施工蓝图.md)。
