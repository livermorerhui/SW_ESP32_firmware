# SonicWave Test Report Template

## Test Record

- Test Case ID:
- Test Description:
- Preconditions:
- Steps:
- Expected Result:
- Actual Result:
- Pass/Fail:
- Logs/Artifacts Link:
- Tester:
- Date:
- Firmware Version:
- Hardware Version:

## 如何填写示例

- Test Case ID: SAFETY-001
- Test Description: BLE 断连后系统应进入安全停机
- Preconditions: 设备上电，已连接 App，已进入 RUNNING
- Steps:
  1. 在 App 端点击断开 BLE
  2. 观察串口日志与事件回传
- Expected Result: 进入 `FAULT_STOP`，振动输出停止
- Actual Result: 收到 `EVT:STATE FAULT_STOP`，平台停止振动
- Pass/Fail: Pass
- Logs/Artifacts Link: `tools/test_runner/reports/20260305_130000/results.md`
- Tester: Alice
- Date: 2026-03-05
- Firmware Version: SW-HUB-1.0.0
- Hardware Version: HW-REV-A
