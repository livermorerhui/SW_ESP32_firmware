#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SW_ROOT="$(cd "$FIRMWARE_ROOT/../SW" && pwd)"
CAPTURE_SCRIPT="$SW_ROOT/scripts/device_test_capture.sh"
AUDIT_SCRIPT="$SCRIPT_DIR/demo_app_quality_smoke_audit.py"
ANDROID_DEMO_DIR="$FIRMWARE_ROOT/tools/android_demo"

usage() {
  cat <<'EOF'
Usage:
  tools/demo_app_quality_smoke_capture.sh devices
  tools/demo_app_quality_smoke_capture.sh install [--android-serial SERIAL]
  tools/demo_app_quality_smoke_capture.sh start [--android-serial SERIAL] [--esp32-port PORT] [--capture-profile PROFILE] [--esp32-mode MODE]
  tools/demo_app_quality_smoke_capture.sh mark --note TEXT
  tools/demo_app_quality_smoke_capture.sh stop --result pass|fail|observe --summary TEXT
  tools/demo_app_quality_smoke_capture.sh stop "SUMMARY"
  tools/demo_app_quality_smoke_capture.sh audit [CAPTURE_DIR]
  tools/demo_app_quality_smoke_capture.sh status
  tools/demo_app_quality_smoke_capture.sh reveal
  tools/demo_app_quality_smoke_capture.sh guide

Purpose:
  Capture the Demo APP quality baseline smoke after the B2-B8 refactor series.

Smoke scope:
  - Fixed top tabs stay visible while scrolling.
  - Default page is Model, then Calibration, Sampling, Run, Logs.
  - Tab content is separated without repeated main sections.
  - Zero / CAL:ZERO cancel path does not write to the device.
  - Demo APP opens and connects to ESP32.
  - Realtime stream / measurement display remains visible.
  - Start -> stop still reaches firmware.
  - Calibration tools remain reachable; optional point capture is user-observed.
  - Motion sampling mode can be enabled and disabled.
  - Model config write and protection switches are optional and should only be used when changing them is safe.
EOF
}

COMMAND="${1:-}"

require_capture_script() {
  if [[ ! -x "$CAPTURE_SCRIPT" ]]; then
    echo "[demo-app-quality-smoke-capture] missing SW capture script: $CAPTURE_SCRIPT" >&2
    exit 2
  fi
}

require_audit_script() {
  if [[ ! -f "$AUDIT_SCRIPT" ]]; then
    echo "[demo-app-quality-smoke-capture] missing audit script: $AUDIT_SCRIPT" >&2
    exit 2
  fi
}

require_option_value() {
  local option="$1"
  local value="${2:-}"
  if [[ -z "$value" || "$value" == --* ]]; then
    echo "[demo-app-quality-smoke-capture] $option requires a value" >&2
    exit 1
  fi
}

install_demo_app() {
  local android_serial=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --android-serial)
        require_option_value "$1" "${2:-}"
        android_serial="$2"
        shift 2
        ;;
      -h|--help) usage; exit 0 ;;
      *) echo "[demo-app-quality-smoke-capture] Unknown install option: $1" >&2; exit 1 ;;
    esac
  done

  cd "$ANDROID_DEMO_DIR"
  if [[ -n "$android_serial" ]]; then
    ANDROID_SERIAL="$android_serial" ./gradlew :app-demo:installDebug --no-daemon --stacktrace
  else
    ./gradlew :app-demo:installDebug --no-daemon --stacktrace
  fi
}

start_capture() {
  local capture_profile="esp32_plus_normal"
  local forwarded=()
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --capture-profile)
        require_option_value "$1" "${2:-}"
        capture_profile="$2"
        shift 2
        ;;
      --android-serial|--esp32-port|--esp32-baud|--esp32-mode)
        require_option_value "$1" "${2:-}"
        forwarded+=("$1" "$2")
        shift 2
        ;;
      -h|--help) usage; exit 0 ;;
      *) forwarded+=("$1"); shift ;;
    esac
  done

  require_capture_script
  local command=(
    "$CAPTURE_SCRIPT" start
    --profile "$capture_profile"
    --accessory-model "ESP32-demo-quality-smoke"
    --mode manual
    --strategy ESP32_RUNTIME
    --scenario demo_app_quality_smoke
    --backend-mode skip
    --esp32-mode platformio
    --no-full-logcat
  )
  if ((${#forwarded[@]} > 0)); then
    command+=("${forwarded[@]}")
  fi
  exec "${command[@]}"
}

normalize_stop_args() {
  if (($# == 0)); then
    return 0
  fi

  case "$1" in
    --*)
      printf '%s\0' "$@"
      return 0
      ;;
    pass|fail|observe)
      local result="$1"
      shift
      printf '%s\0' --result "$result"
      if (($# > 0)); then
        printf '%s\0' --summary "$*"
      fi
      return 0
      ;;
    *)
      printf '%s\0' --result pass --summary "$*"
      return 0
      ;;
  esac
}

case "$COMMAND" in
  install)
    shift
    install_demo_app "$@"
    ;;
  start)
    shift
    start_capture "$@"
    ;;
  stop)
    shift
    if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
      usage
      exit 0
    fi
    require_capture_script
    stop_args=()
    if (($# > 0)); then
      while IFS= read -r -d '' arg; do
        stop_args+=("$arg")
      done < <(normalize_stop_args "$@")
    fi
    stop_output="$("$CAPTURE_SCRIPT" stop "${stop_args[@]}")"
    printf '%s\n' "$stop_output"
    capture_dir="$(printf '%s\n' "$stop_output" | awk 'NF { line=$0 } END { print line }')"
    if [[ -z "$capture_dir" || ! -d "$capture_dir" ]]; then
      echo "[demo-app-quality-smoke-capture] stop did not return a valid capture directory" >&2
      exit 2
    fi
    if [[ -f "$AUDIT_SCRIPT" ]]; then
      python3 "$AUDIT_SCRIPT" "$capture_dir" || true
    fi
    ;;
  audit)
    shift
    require_audit_script
    python3 "$AUDIT_SCRIPT" "$@"
    ;;
  mark|status|reveal|devices)
    shift
    require_capture_script
    exec "$CAPTURE_SCRIPT" "$COMMAND" "$@"
    ;;
  guide)
    cat <<'EOF'
Demo APP quality baseline smoke steps

Directory:
  /Users/r.w.hui/Desktop/SW_ESP3_Firmware

Current test gate:
  This is real-device smoke. Do not call it "passed" until capture artifacts are reviewed.

Prerequisites:
  APP: install the current Demo APP build before testing.
  ESP32: no firmware flashing is needed unless you changed firmware after the last known-good burn.
  Backend: not used.
  Phone: connect with ADB over USB or wireless.
  ESP32: connect over USB if you want strict serial evidence; if serial is unavailable, the audit can only be partial.

Prepare:
  tools/demo_app_quality_smoke_capture.sh devices

Install current Demo APP:
  tools/demo_app_quality_smoke_capture.sh install

Start capture:
  tools/demo_app_quality_smoke_capture.sh start

Notes:
  This wrapper defaults to PlatformIO serial monitor for ESP32 evidence, because it preserves readable ESP32 text logs more reliably than raw stty capture on the current macOS setup.

If there are multiple phones or ESP32 serial ports:
  tools/demo_app_quality_smoke_capture.sh start --android-serial <phone_serial> --esp32-port <esp32_port>

Phone actions:
  1. Open SonicWave Demo.
  2. Confirm the top bar shows SW调试, 搜索, and 断开 after connection.
  3. Confirm the default page is 型号.
  4. Scroll the content up and down; confirm 型号 / 校准 / 采样 / 运行 / 日志 stays fixed at the top.
  5. On 型号, confirm the main actions are 设置型号 and 保护开关.
     - Base shows 无距离传感器.
     - Plus shows 有距离传感器.
     - Pro and Ultra are gray and cannot be selected.
     - The connection status card is not shown in this page.
     - Extra status text is hidden until 查看详情 is opened.
  6. Open each tab and confirm the main content is separated:
     - 型号: model setting / protection switches.
     - 校准: calibration tools.
     - 采样: motion sampling.
     - 运行: system status / telemetry / run session.
     - 日志: raw device log.
  7. In 校准, tap Zero or CAL:ZERO, then tap Cancel. Confirm it returns without sending or changing the device.

After the UI structure and cancel path are confirmed, return to the computer:
  tools/demo_app_quality_smoke_capture.sh mark --note "用户确认：默认型号页，顶部 Tab 固定，内容不重复；型号设置精简，保护开关只保留摔倒保护和律动离开；归零确认弹窗取消后未写入设备"

Phone actions:
  8. Connect the ESP32 device.
  9. Wait until live distance / weight / realtime stream is visibly updating.

After live data is visible, return to the computer:
  tools/demo_app_quality_smoke_capture.sh mark --note "用户确认：Demo APP 已连接 ESP32，实时距离/重量/曲线可见"

Phone actions:
  10. Open 运行.
  11. Press Start.
  12. Wait 2 to 3 seconds.
  13. Press Stop.
  14. Confirm the wave has stopped and the UI is stable.

After Start -> Stop is stable, return to the computer:
  tools/demo_app_quality_smoke_capture.sh mark --note "用户确认：Demo APP Start 后 Stop 成功，体感和界面稳定"

Phone actions:
  15. Open 校准; confirm the section is visible and fields/buttons still respond.
  16. Open 采样; enable motion sampling mode, start sampling, wait a few seconds, stop sampling, then disable sampling mode.
  17. Open 型号; only if it is safe for the current bench, write model config once and toggle protection switches once, then confirm the status returns success or a clear failure message.

After tools smoke is done, return to the computer:
  tools/demo_app_quality_smoke_capture.sh mark --note "用户确认：校准工具可操作，motion sampling 启停正常，型号确认和保护开关未测或已按现场安全条件完成"

If everything passed on the phone:
  tools/demo_app_quality_smoke_capture.sh stop --result pass --summary "Demo APP quality baseline smoke passed by user; awaiting evidence review"

If any step failed or looked wrong:
  tools/demo_app_quality_smoke_capture.sh mark --note "用户观察：<用人话写具体失败现象>"
  tools/demo_app_quality_smoke_capture.sh stop --result fail --summary "Demo APP quality baseline smoke failed; see user marker"

After stop:
  Send the capture directory path or say "已 stop". The assistant must review the generated audit and capture files before closing.
EOF
    ;;
  -h|--help|"")
    usage
    ;;
  *)
    echo "[demo-app-quality-smoke-capture] Unknown command: $COMMAND" >&2
    usage >&2
    exit 1
    ;;
esac
