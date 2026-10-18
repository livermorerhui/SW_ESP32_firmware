# E8 J100-485 未律动空载、站稳、离台真机观察（2026-10-14）

## 本轮结论

**仅限本轮 Plus/J100-485、已设空载距离 112.34 mm、始终未点「开始」的操作：**用户看到 Demo APP 常驻底栏「开始」依次灰色不可点、绿色可点、灰色不可点；ESP32 同期上报 `start_ready=0→1→0`。这证明此实物组合的未律动空载→站稳→离台路径能工作，不能外推为阈值、律动中离台或摔倒已经验收。

| 时序与操作者动作 | Demo APP 实际表现 | 设备上报与证据 |
|---|---|---|
| 平台无人、设备停波 | 型号 Plus、传感器 J100-485、空载距离 112.34 mm；实时距离 112.33 mm；底栏「开始」灰色不可点 | `EVT:STREAM distance_mm=112.33 weight_valid=0`；`EVT:BASELINE start_ready=0 baseline_ready=0` |
| 用户站上并站稳，未点「开始」 | 实时距离约 105.64 mm；底栏「开始」绿色可点 | 11:03:17 左右 `distance_mm=105.65 weight_valid=0`、`start_ready=1 baseline_ready=1`；ESP32 串口 `IDLE→ARMED` |
| 用户离开平台，始终未点「开始」 | 实时距离 112.32 mm；底栏「开始」恢复灰色不可点 | 11:04:42 左右 `distance_mm=112.32 weight_valid=0`、`start_ready=0 baseline_ready=0`；ESP32 串口 `ARMED→IDLE` |

本轮手机日志没有 `WAVE:START/STOP`、Demo APP 断连或崩溃记录。体重字段全程 `weight_valid=0`，所以这次 `start_ready` 的变化不依赖可用的体重模型；这只证明实际输出路径，不代替对内部所有计算分支的审计。

## 原始证据与边界

采集场景 `e8_j100_idle_state_observe`，手机为小米 8，ESP32 串口为 `/dev/cu.usbmodem5B910005491`。原始目录：

`/Volumes/MACminiSSD/SW/.artifacts/device-test-captures/20261014_105408__Xiaomi_MI_8__esp32_plus_normal__E8-Plus-J100-idle-state__manual__ESP32_RUNTIME__e8_j100_idle_state_observe/`

- `notes.md` 记录操作标签和用户反馈；`visual_evidence/` 保留空载、站稳、离台截图。
- `demo_transport_logcat_live.log` 是可读的本轮手机 BLE 实时记录；`esp32_serial.log` 有 4 条 `IDLE↔ARMED` 状态变化。`session_meta.env` 与 `esp32_reader_state.env` 留存采集元数据及串口可读性。
- 原采集脚本的 focus logcat 进程提前退出，见 `warnings.log`；随后启用独立实时 logcat，并在停止后复制为上述 `demo_transport_logcat_live.log`。`android_logcat_full.log` 是停止时快照，单独不足以证明早段空载行为。
- 串口详细诊断默认关闭，`esp32_serial_focus.log` 为空；本轮不能从串口重建内部阈值、连续确认次数和各次状态变化的全部原因。采集元数据中的 Git 分支/提交取自 SW 仓，不可当作 ESP32 固件版本。本轮也未覆盖服务器；此操作链不涉及服务器。

## 后续仍需验收

此轮仅覆盖未律动的一次带标签闭环。已知 3 kg/5 kg 载荷、晃动/站稳反例、阈值和容差、断流，以及开始律动后的 16%/28% 比例、离台/摔倒停波均未在本轮测试。现有距离律动分支仍以距离绝对偏差作判断，详见 `src/modules/laser/RhythmStateJudge.cpp`；不能把本轮绿色「开始」解释为律动中保护算法已完成。
