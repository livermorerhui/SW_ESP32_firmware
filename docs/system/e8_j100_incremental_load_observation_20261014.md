# E8 J100-485 固定托盘递增加载观察（2026-10-14）

## 现场操作与已确认数据

用户在同一托盘、同一位置，设备停波且始终未点「开始」，依次做：空载→放入确认的 5 kg→原位叠加 3 kg 成为 8 kg→取下 3 kg 回到 5 kg→取下 5 kg、托盘留在原位。空载参考距离 `D_e=112.34 mm`。

| 操作 | 用户看到的距离 | 标记前 15～1 秒 BLE 有效距离最小／中位／最大 | 底栏「开始」 |
|---|---:|---:|---|
| 空载 | 112.33 mm | 112.32／112.33／112.33 mm | 灰色不可点 |
| 确认 5 kg | 111.82 mm | 111.81／111.82／111.83 mm | 灰色不可点 |
| 原位叠加 3 kg，共 8 kg | 111.53 mm | 111.53／111.53／111.54 mm | 灰色不可点 |
| 取下 3 kg，回到 5 kg | 111.81 mm | 111.81／111.81／111.81 mm | 灰色不可点 |
| 取下 5 kg，托盘不动 | 112.32 mm | 112.32／112.32／112.33 mm | 截图为灰色不可点；用户只报了距离 |

本轮 0→5 kg 距离减少约 **0.51 mm**，原位 5→8 kg 又减少约 **0.29 mm**；按这两个区间算均约 **0.10 mm/kg**。卸载回 5 kg 与加载 5 kg 相差约 **0.01 mm**，最终空载与初始空载相差约 **0.01 mm**。上轮确认的 3 kg 距离约 112.03 mm，方向和量级相符，但跨轮且仅一次，只能作旁证。**这是一台设备的一次短循环，对 0～8 kg 近似线性提供初步证据，不是模型、传感器精度或正式阈值的验收。**

上一轮曾把 111.98 mm 标为 5 kg；用户在本轮纠正当时放少了、实际重量未知。该值已在[原采数记录](./e8_j100_lightload_real_device_observation_20261014.md)和原 capture 的 `notes.md` 追加纠正，**不可再用于 5 kg 标定或斜率计算**。本轮确认的 5 kg 为 111.82 mm。

## 当前固件与解释边界

J100 未律动距离链的“有人”进入代码现值是相对 `D_e` 偏离 **3 mm、连续 2 个有效样本**，并非由上述实测选出的正式参数。本轮 8 kg 相对 `D_e` 偏离约 **0.81 mm**，所以 5 kg 和 8 kg 时固件仍上报 `start_ready=0`，APP 底栏保持灰色与代码相符。如果产品目标要沿用旧体重版“5 kg 进入、3 kg 退出”的使用体验，这个 3 mm 距离门槛对本台设备的已测载荷不适用；但不能把 0～8 kg 的短区间线性外推为真实的“30 kg 门槛”，也不能直接把单轮 0.51/0.30 mm 写成正式进入/退出值。

下一步仍需保持同托盘、同位置，重复空载→3→5→8→5→3→空载，并采集人站稳、正常晃动和空载回弹数据；比较各档范围、装载/卸载差异后再回放候选门槛。体重模型全程无效，未参与本次距离门禁观察。

## 证据与缺口

原始目录：

`/Volumes/MACminiSSD/SW/.artifacts/device-test-captures/20261014_113012__Xiaomi_MI_8__esp32_plus_normal__J100-485__manual__ESP32_RUNTIME__e8_j100_incremental_5_to_8kg_idle/`

- `notes.md` 保留现场操作及纠正标记；`visual_evidence/` 保存五个阶段的 APP 截图；`demo_transport_logcat_live.log` 覆盖全程，含各 2738 条 `EVT:STREAM` 和 `EVT:BASELINE`。所有测距帧 `distance_valid=1 weight_valid=0 backend=LASER_485`，状态帧全程 `start_ready=0 baseline_ready=0`；未见断连、崩溃或波形启停命令。
- **ESP32 串口证据未通过：**`esp32_serial.log` 为 0 字节，`warnings.log` 有 `esp32_serial_capture_insufficient`。该场景没有状态切换且默认详细串口关闭，可能使串口不打印，但不能据此证明内部判断日志已采到；本轮只能收为 BLE 上报／界面观察与距离采数。`android_logcat_full.log` 的停止时快照仅覆盖末段，独立实时日志才覆盖完整操作。
- 服务器不参与本地停波采数；正式 SW APP 的私有日志不作为 Demo APP 证据。

代码依据：[距离链当前参数](../../src/modules/laser/DistanceStateChain.h)、[距离判断](../../src/modules/laser/DistanceStateChain.cpp)、[J100 参数装载](../../src/modules/laser/LaserModule.cpp)。测试目标见[施工蓝图 E8-J05](./esp32_base_plus_sensor_profile_app_configuration施工蓝图.md)。
