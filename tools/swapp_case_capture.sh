#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  tools/swapp_case_capture.sh start <case_name> [log_dir]
  tools/swapp_case_capture.sh stop <case_name> [log_dir]
  tools/swapp_case_capture.sh status <case_name> [log_dir]

Examples:
  tools/swapp_case_capture.sh start base_connect_start_stop
  tools/swapp_case_capture.sh stop base_connect_start_stop
  tools/swapp_case_capture.sh status base_connect_start_stop
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 1
fi

ACTION="$1"
CASE_NAME="$2"
LOG_DIR="${3:-$HOME/swapp-logs}"
STATE_DIR="$LOG_DIR/.capture_state"
LOG_FILE="$LOG_DIR/${CASE_NAME}.log"
RAW_LOG_FILE="$LOG_DIR/${CASE_NAME}.full.log"
STATE_FILE="$STATE_DIR/${CASE_NAME}.state"
FILTER_SPEC=(SW_CASE SW_CONNECT SW_EVENT SW_STATE SW_SESSION SW_STOP)

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing command: $1" >&2
    exit 1
  fi
}

print_steps() {
  case "$CASE_NAME" in
    base_connect_start_stop)
      cat <<'EOF'
手机操作:
1. 打开 formal SW APP
2. 连接 BASE 设备
3. 等待可开始
4. 设置一组合法参数
5. 点击 Start
6. 等 3-5 秒
7. 点击 Stop
8. 等界面稳定
EOF
      ;;
    base_reconnect)
      cat <<'EOF'
手机操作:
1. 打开 formal SW APP
2. 连接 BASE 设备
3. 等待 ready
4. 让 ESP32 断开一次
5. 等 APP 显示断连
6. 恢复设备并重新连接
7. 等界面重新稳定
EOF
      ;;
    plus_degraded_first_connect)
      cat <<'EOF'
手机操作:
1. 打开 formal SW APP
2. 连接 PLUS degraded-start bench
3. 等待 degraded 提示出现
4. 不要点 degraded-start confirm
5. 不要点 Start
6. 只观察 Start 是否被拦住
EOF
      ;;
    plus_degraded_start_stop)
      cat <<'EOF'
手机操作:
1. 打开 formal SW APP
2. 连接 PLUS degraded-start bench
3. 等待 degraded 提示
4. 点击 degraded-start confirm
5. 等待可以开始
6. 设置一组合法参数
7. 点击 Start
8. 等 3-5 秒
9. 点击 Stop
10. 等界面稳定
EOF
      ;;
    plus_degraded_reconnect)
      cat <<'EOF'
手机操作:
1. 打开 formal SW APP
2. 连接 PLUS degraded-start bench
3. 点击 degraded-start confirm
4. 等待界面稳定
5. 让 ESP32 断开一次
6. 等 APP 显示断连
7. 恢复设备并重新连接
8. 观察是否重新同步 degraded truth
EOF
      ;;
    *)
      cat <<EOF
手机操作:
1. 运行场景: $CASE_NAME
2. 完成后执行 stop
EOF
      ;;
  esac
}

require_command adb
mkdir -p "$LOG_DIR" "$STATE_DIR"

if ! adb get-state >/dev/null 2>&1; then
  echo "adb device not ready" >&2
  exit 1
fi

case "$ACTION" in
  start)
    if [[ -f "$STATE_FILE" ]]; then
      echo "Capture already started: $CASE_NAME" >&2
      echo "If previous run is stale, remove: $STATE_FILE" >&2
      exit 1
    fi

    adb logcat -c
    adb shell log -t SW_CASE "CASE_BEGIN name=${CASE_NAME}" >/dev/null
    printf '%s\n' "$(date +%s)" > "$STATE_FILE"

    echo "Started: $CASE_NAME"
    echo "Log file: $LOG_FILE"
    echo
    print_steps
    echo
    echo "完成手机操作后，执行:"
    echo "./tools/swapp_case_capture.sh stop $CASE_NAME"
    ;;

  stop)
    if [[ ! -f "$STATE_FILE" ]]; then
      echo "Capture not started: $CASE_NAME" >&2
      echo "Run start first." >&2
      exit 1
    fi

    adb shell log -t SW_CASE "CASE_END name=${CASE_NAME}" >/dev/null
    sleep 1
    adb logcat -d -v threadtime > "$RAW_LOG_FILE"
    adb logcat -d -v threadtime -s "${FILTER_SPEC[@]}" > "$LOG_FILE"
    rm -f "$STATE_FILE"

    echo "Saved: $LOG_FILE"
    echo "Saved: $RAW_LOG_FILE"
    ;;

  status)
    if [[ -f "$STATE_FILE" ]]; then
      echo "RUNNING $CASE_NAME"
      echo "Log file: $LOG_FILE"
    else
      echo "IDLE $CASE_NAME"
      echo "Log file: $LOG_FILE"
    fi
    ;;

  *)
    usage
    exit 1
    ;;
esac
