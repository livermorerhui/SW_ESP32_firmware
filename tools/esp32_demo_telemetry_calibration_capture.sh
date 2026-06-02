#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SW_ROOT="$(cd "$FIRMWARE_ROOT/../SW" && pwd)"
CAPTURE_SCRIPT="$SW_ROOT/scripts/device_test_capture.sh"
AUDIT_SCRIPT="$SCRIPT_DIR/esp32_demo_telemetry_calibration_audit.py"

usage() {
  cat <<'EOF'
Usage:
  tools/esp32_demo_telemetry_calibration_capture.sh devices
  tools/esp32_demo_telemetry_calibration_capture.sh start [--android-serial SERIAL] [--esp32-port PORT]
  tools/esp32_demo_telemetry_calibration_capture.sh mark --note TEXT
  tools/esp32_demo_telemetry_calibration_capture.sh stop --result pass|fail|observe --summary TEXT
  tools/esp32_demo_telemetry_calibration_capture.sh audit [CAPTURE_DIR]
  tools/esp32_demo_telemetry_calibration_capture.sh status
  tools/esp32_demo_telemetry_calibration_capture.sh reveal
  tools/esp32_demo_telemetry_calibration_capture.sh guide

Purpose:
  Capture ESP32 firmware Demo APP telemetry curve and calibration input evidence.

Checks:
  - RS485 / Modbus measurement windows are running
  - ESP32 measurement_health reaches READY
  - ESP32 measurement plane produces valid samples
  - BLE / Demo APP receives EVT:STREAM
  - Demo APP consumes stream into telemetry points
  - Calibration point recording has live distance input
EOF
}

COMMAND="${1:-}"

require_capture_script() {
  if [[ ! -x "$CAPTURE_SCRIPT" ]]; then
    echo "[esp32-demo-telemetry-calibration-capture] missing SW capture script: $CAPTURE_SCRIPT" >&2
    exit 2
  fi
}

case "$COMMAND" in
  start)
    shift
    require_capture_script
    exec "$CAPTURE_SCRIPT" start \
      --profile esp32_plus_normal \
      --accessory-model "ESP32-plus-demo-telemetry-calibration" \
      --mode manual \
      --strategy ESP32_RUNTIME \
      --scenario esp32_demo_telemetry_calibration \
      --backend-mode skip \
      --esp32-mode auto \
      --full-logcat \
      "$@"
    ;;
  stop)
    shift
    require_capture_script
    capture_dir="$("$CAPTURE_SCRIPT" stop "$@")"
    echo "$capture_dir"
    if [[ -f "$AUDIT_SCRIPT" ]]; then
      python3 "$AUDIT_SCRIPT" "$capture_dir" || true
    fi
    ;;
  audit)
    shift
    python3 "$AUDIT_SCRIPT" "$@"
    ;;
  mark|status|reveal|devices)
    shift
    require_capture_script
    exec "$CAPTURE_SCRIPT" "$COMMAND" "$@"
    ;;
  guide)
    cat <<'EOF'
ESP32 固件 Demo APP 遥测曲线 / 校准无实时数据专项采集步骤

执行目录：
  /Users/r.w.hui/Desktop/SW_ESP3_Firmware

现在能不能开始测：能，但这是证据采集，不是修复验收。
APP：如果手机上已经装的是当前出问题的 Demo APP，不需要重装；如果刚改过 Demo APP 才需要重装。
ESP32：如果当前就是出问题的固件，不需要重烧；如果刚改过固件或不确定烧录版本，先不要测，先确认版本。
后台：不参与本轮，不需要启动。
手机：需要通过 ADB 连接电脑。
ESP32：需要通过 USB 连接电脑，用于串口采集。
MAX485 / 激光传感器：保持当前出问题时的接线和供电，不要先换线。

1. 准备：查看手机和 ESP32 串口

  tools/esp32_demo_telemetry_calibration_capture.sh devices

如果同时有多台 Android 设备或多个 ESP32 串口，下一步 start 必须带 --android-serial 和 --esp32-port。

2. 开始记录

如果只有一台手机和一个 ESP32 串口：

  tools/esp32_demo_telemetry_calibration_capture.sh start

如果有多台 Android 设备或多个串口：

  tools/esp32_demo_telemetry_calibration_capture.sh start --android-serial <手机序列号> --esp32-port <ESP32串口>

3. 手机上操作：复现曲线和校准问题

手机上只做：
  1. 打开 Demo APP。
  2. 连接 ESP32-plus。
  3. 打开遥测曲线所在区域，等待 10 到 15 秒。
  4. 如果曲线仍然没有实时数据，回电脑输入一次 mark。

看到“曲线没有实时数据”后输入：

  tools/esp32_demo_telemetry_calibration_capture.sh mark --note "用户观察：遥测曲线没有实时数据，踩上去没有数据，离开后才有数据；MAX485 TX/RX 灯偶尔停闪后恢复"

然后手机继续做：
  5. 进入校准工具。
  6. 点击开始录制。
  7. 保持当前重量 / 位置稳定 5 到 10 秒。
  8. 尝试记录一个校准点。

如果校准点成功记录但不是实时数据，回电脑输入：

  tools/esp32_demo_telemetry_calibration_capture.sh mark --note "用户观察：校准点已记录，但数据不是实时刷新"

如果“记录校准点”不可用或提示没有实时距离，回电脑输入：

  tools/esp32_demo_telemetry_calibration_capture.sh mark --note "用户观察：校准工具因为没有实时数据无法记录校准点"

4. 结束记录

如果复现了曲线无实时数据或校准无法实时记录：

  tools/esp32_demo_telemetry_calibration_capture.sh stop --result fail --summary "Demo APP telemetry is not realtime; curve empty while stepping on, data appears after leaving; calibration point can be recorded only from stale data"

如果没有复现，曲线实时有数据且校准点能记录当前数据：

  tools/esp32_demo_telemetry_calibration_capture.sh stop --result pass --summary "Demo APP telemetry curve and calibration point capture passed with realtime EVT:STREAM"

结束后我会复核：
  - esp32_demo_telemetry_calibration_audit.md
  - notes.md
  - session_meta.env
  - warnings.log
  - android_logcat_focus.log
  - esp32_serial_focus.log
  - esp32_serial.log
EOF
    ;;
  -h|--help|"")
    usage
    ;;
  *)
    echo "[esp32-demo-telemetry-calibration-capture] Unknown command: $COMMAND" >&2
    usage >&2
    exit 1
    ;;
esac
