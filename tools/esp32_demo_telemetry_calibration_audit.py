#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SW_ROOT = ROOT.parent / "SW"
CAPTURE_BASES = [
    ROOT / ".artifacts" / "device-test-captures",
    SW_ROOT / ".artifacts" / "device-test-captures",
]
OUTPUT_NAME = "esp32_demo_telemetry_calibration_audit.md"
STREAM_RESTORED_MIN_LINES = 10


@dataclass(frozen=True)
class Hit:
    label: str
    count: int
    examples: list[str]


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def latest_capture_dir() -> Path | None:
    candidates: list[Path] = []
    for base in CAPTURE_BASES:
        if not base.exists():
            continue
        candidates.extend(
            path
            for path in base.iterdir()
            if path.is_dir() and "esp32_demo_telemetry_calibration" in path.name
        )
    return max(candidates, key=lambda path: path.stat().st_mtime, default=None)


def compact(line: str) -> str:
    return re.sub(r"\s+", " ", line).strip()


def grep_lines(text: str, pattern: str, flags: int = re.IGNORECASE) -> list[str]:
    rx = re.compile(pattern, flags)
    return [compact(line) for line in text.splitlines() if rx.search(line)]


def has(text: str, pattern: str) -> bool:
    return bool(re.search(pattern, text, re.IGNORECASE | re.DOTALL))


def hit(label: str, text: str, pattern: str, limit: int = 12) -> Hit:
    lines = grep_lines(text, pattern)
    return Hit(label=label, count=len(lines), examples=lines[:limit])


def section_hits(title: str, hits: list[Hit]) -> list[str]:
    lines = [f"## {title}", ""]
    for item in hits:
        lines.append(f"- {item.label}: `{item.count}`")
        for example in item.examples:
            lines.append(f"  - `{example}`")
    lines.append("")
    return lines


def parse_measurement_diag(esp32: str) -> tuple[int, int, int, int]:
    ok_total = 0
    fail_total = 0
    max_read_ms = 0
    windows = 0
    for line in esp32.splitlines():
        if "[MEASUREMENT_DIAG]" not in line:
            continue
        windows += 1
        ok_match = re.search(r"\bok=(\d+)", line)
        fail_match = re.search(r"\bfail=(\d+)", line)
        max_match = re.search(r"\bmax_read_ms=(\d+)", line)
        if ok_match:
            ok_total += int(ok_match.group(1))
        if fail_match:
            fail_total += int(fail_match.group(1))
        if max_match:
            max_read_ms = max(max_read_ms, int(max_match.group(1)))
    return windows, ok_total, fail_total, max_read_ms


def parse_last_total(pattern: str, text: str) -> int | None:
    values = [int(item) for item in re.findall(pattern, text, re.IGNORECASE)]
    return values[-1] if values else None


def fatal_runtime_lines(text: str) -> list[str]:
    lines = grep_lines(
        text,
        r"Guru|panic|Brownout|watchdog|WDT|FATAL EXCEPTION|ANR in",
        flags=re.IGNORECASE,
    )
    return [
        line
        for line in lines
        if "adbd" not in line.lower() and "watchdogd" not in line.lower()
    ]


def build_report(capture_dir: Path) -> tuple[str, str]:
    android_focus = read_text(capture_dir / "android_logcat_focus.log")
    android_full = read_text(capture_dir / "android_logcat_full.log")
    android_stop_snapshot = read_text(capture_dir / "android_logcat_stop_snapshot.log")
    runtime_events = read_text(capture_dir / "runtime_events_session.jsonl")
    esp32_focus = read_text(capture_dir / "esp32_serial_focus.log")
    esp32_raw = read_text(capture_dir / "esp32_serial.log")
    notes = read_text(capture_dir / "notes.md")
    meta = read_text(capture_dir / "session_meta.env")
    warnings = read_text(capture_dir / "warnings.log")
    summary = read_text(capture_dir / "capture_evidence_summary.txt")

    android = "\n".join([android_focus, android_full, android_stop_snapshot, runtime_events])
    esp32 = "\n".join([esp32_focus, esp32_raw])
    runtime_combined = "\n".join([android, esp32])
    combined = "\n".join([runtime_combined, notes, summary])

    diag_windows, diag_ok, diag_fail, diag_max_read_ms = parse_measurement_diag(esp32)
    esp32_ready = has(combined, r"\[MEASUREMENT_HEALTH\].*state=READY|measurement_health=READY")
    esp32_plane_valid = len(grep_lines(esp32, r"\[LAYER:MEASUREMENT_PLANE\].*valid=1"))
    esp32_plane_invalid = len(grep_lines(esp32, r"\[LAYER:MEASUREMENT_PLANE\].*valid=0"))
    modbus_fail = len(grep_lines(esp32, r"Modbus read fail|READ_FAIL"))
    transient_fail = len(grep_lines(esp32, r"\[MEASUREMENT_TRANSIENT\].*event=fail_start"))
    transient_recovered = len(grep_lines(esp32, r"\[MEASUREMENT_TRANSIENT\].*event=recovered"))
    stream_suppressed = len(grep_lines(esp32, r"queue=stream suppressed_for_control"))
    stream_suppressed_total = parse_last_total(r"queue=stream suppressed_for_control.*\btotal=(\d+)", esp32)
    baseline_fragmented = len(grep_lines(esp32, r"notify_fragmented.*prefix=EVT:BASELINE"))
    fatal_lines = fatal_runtime_lines(runtime_combined)

    android_stream_lines = len(grep_lines(android, r"RX line emitted=EVT:STREAM"))
    android_stream_chunks = len(grep_lines(android, r"RX chunk .*EVT:STREAM"))
    app_stream_restored = android_stream_lines >= STREAM_RESTORED_MIN_LINES
    android_snapshot_ready = len(grep_lines(android, r"RX line emitted=SNAPSHOT:.*measurement_health=READY"))
    android_baseline_lines = len(grep_lines(android, r"RX line emitted=EVT:BASELINE"))
    consume_valid = len(grep_lines(android, r"\[LAYER:MEASUREMENT_CONSUME\].*valid=1"))
    consume_invalid = len(grep_lines(android, r"\[LAYER:MEASUREMENT_CONSUME\].*valid=0"))
    consume_ignored = len(grep_lines(android, r"\[LAYER:MEASUREMENT_CONSUME\].*ignored"))
    cal_preconditions = len(grep_lines(android, r"\[CAL_APP\] preconditions|\[CAL_APP\] record clicked"))
    cal_points = len(grep_lines(android, r"\[CAL_APP\] point appended|\[CAL_UI\] pointRecorded|ACK:CAL_POINT"))
    cal_no_distance = len(grep_lines(android, r"distancePresent=false|no live distance|没有实时距离|capture_unavailable_no_live_distance"))
    user_failed = has(notes, r"result=fail|异常|没有数据|无法校准|曲线.*没有|无数据|不是实时")
    user_passed = has(notes, r"result=pass|通过|曲线.*有数据|校准.*成功|校准点已记录")

    findings: list[str] = []
    evidence_gaps: list[str] = []
    observations: list[str] = []

    if not esp32_raw.strip() and not esp32_focus.strip():
        evidence_gaps.append("ESP32 串口日志为空，无法判断 MAX485 / 固件测量真相源。")
    if not android.strip():
        evidence_gaps.append("Android / Demo APP logcat 为空，无法判断 APP 是否收到遥测。")
    if not notes.strip():
        evidence_gaps.append("notes.md 为空，缺少用户体感结论。")

    if fatal_lines:
        findings.append("发现致命异常（FATAL_RUNTIME_EXCEPTION），先处理崩溃 / reset / WDT。")
    if diag_windows == 0:
        evidence_gaps.append("未看到 MEASUREMENT_DIAG，无法证明 RS485 读取窗口在运行。")
    elif diag_ok == 0 and diag_fail > 0:
        findings.append("RS485 读取窗口只有失败没有成功（RS485_NO_VALID_READ）。")
    elif diag_fail > 0:
        observations.append(
            f"RS485 出现瞬时失败：fail={diag_fail}，恢复事件={transient_recovered}，最大读耗时={diag_max_read_ms}ms。"
        )
    if transient_fail > transient_recovered:
        findings.append("MEASUREMENT_TRANSIENT 有 fail_start 但缺 recovered（RS485_TRANSIENT_UNRECOVERED）。")
    if not esp32_ready:
        findings.append("未看到 measurement_health=READY（MEASUREMENT_HEALTH_NOT_READY）。")
    if esp32_plane_valid > 0 and android_stream_lines == 0 and stream_suppressed > 0:
        findings.append("固件已有有效测量样本，但 BLE stream 被 control 队列压制（STREAM_SUPPRESSED_FOR_CONTROL）。")
    elif esp32_plane_valid > 0 and android_stream_lines == 0:
        findings.append("固件已有有效测量样本，但 APP 侧未收到 EVT:STREAM（STREAM_NOT_DELIVERED_TO_APP）。")
    elif android_snapshot_ready > 0 and android_baseline_lines > 0 and android_stream_lines == 0:
        findings.append("APP 已收到 READY snapshot / baseline，但没有实时 EVT:STREAM（APP_READY_WITHOUT_STREAM）。")
    elif android_stream_lines > 0 and consume_valid == 0:
        if user_passed and app_stream_restored and cal_no_distance == 0:
            evidence_gaps.append(
                "Demo APP 缺少 MEASUREMENT_CONSUME / 校准录点结构化日志；"
                "但用户确认曲线、实时体重、实时距离和校准录点通过，且 Android 连续收到 EVT:STREAM。"
            )
            observations.append("Demo APP 传输层实时 EVT:STREAM 已恢复，消费层调试日志缺失不再单独判失败。")
        else:
            findings.append("APP 收到 EVT:STREAM，但消费层未形成有效测量点（APP_STREAM_CONSUME_GAP）。")
    elif consume_valid > 0 and cal_points == 0 and cal_preconditions > 0:
        findings.append("APP 已消费遥测，但校准录点未完成，优先看录制 / 参考重量 / distancePresent 条件（CALIBRATION_PRECONDITION_GAP）。")
    elif consume_valid > 0 and cal_points > 0:
        observations.append("Demo APP 已消费有效遥测且校准点有记录证据。")

    if cal_no_distance > 0:
        findings.append("校准工具明确记录实时距离缺失（CALIBRATION_NO_LIVE_DISTANCE）。")
    if user_failed and not findings:
        findings.append("用户体感失败，但自动证据未指向明确 owner（USER_FAIL_NEEDS_MORE_EVIDENCE）。")
    if modbus_fail > 0:
        observations.append(f"日志中出现 Modbus/READ_FAIL 相关行：{modbus_fail}。")

    if fatal_lines:
        verdict = "FAIL_FATAL_RUNTIME_EXCEPTION"
    elif any("RS485_NO_VALID_READ" in item or "MEASUREMENT_HEALTH_NOT_READY" in item for item in findings):
        verdict = "FAIL_RS485_OR_MEASUREMENT_HEALTH"
    elif any("STREAM_SUPPRESSED_FOR_CONTROL" in item for item in findings):
        verdict = "FAIL_STREAM_SUPPRESSED_FOR_CONTROL"
    elif any("STREAM_NOT_DELIVERED_TO_APP" in item or "APP_READY_WITHOUT_STREAM" in item for item in findings):
        verdict = "FAIL_STREAM_NOT_DELIVERED_TO_APP"
    elif any("APP_STREAM_CONSUME_GAP" in item for item in findings):
        verdict = "FAIL_APP_STREAM_CONSUME_GAP"
    elif any("CALIBRATION_" in item for item in findings):
        verdict = "FAIL_CALIBRATION_INPUT_GAP"
    elif user_passed and app_stream_restored and evidence_gaps and not findings:
        verdict = "PASS_APP_STREAM_RESTORED_WITH_EVIDENCE_GAP"
    elif evidence_gaps:
        verdict = "NEEDS_REVIEW_EVIDENCE_GAP"
    elif user_passed and (consume_valid > 0 or app_stream_restored):
        verdict = "PASS_CANDIDATE"
    else:
        verdict = "NEEDS_REVIEW"

    android_hits = [
        hit("Demo/Transport 收到 SNAPSHOT READY", android, r"RX line emitted=SNAPSHOT:.*measurement_health=READY", limit=16),
        hit("Demo/Transport 收到 EVT:BASELINE", android, r"RX line emitted=EVT:BASELINE", limit=16),
        hit("Demo/Transport 收到 EVT:STREAM", android, r"RX line emitted=EVT:STREAM", limit=20),
        hit("Demo/Transport 原始 chunk 含 EVT:STREAM", android, r"RX chunk .*EVT:STREAM", limit=12),
        hit("APP 消费有效遥测", android, r"\[LAYER:MEASUREMENT_CONSUME\].*valid=1", limit=16),
        hit("APP 消费无效遥测", android, r"\[LAYER:MEASUREMENT_CONSUME\].*valid=0", limit=16),
        hit("APP 忽略遥测 carrier", android, r"\[LAYER:MEASUREMENT_CONSUME\].*ignored", limit=12),
        hit("校准前置 / 录点", android, r"\[CAL_APP\] preconditions|\[CAL_APP\] record clicked|\[CAL_APP\] point appended|\[CAL_UI\] pointRecorded|ACK:CAL_POINT", limit=24),
        hit("校准无实时距离", android, r"distancePresent=false|no live distance|没有实时距离|capture_unavailable_no_live_distance", limit=12),
    ]
    esp32_hits = [
        hit("传感器 profile", esp32, r"\[LASER_SENSOR_PROFILE\]", limit=8),
        hit("测量诊断", esp32, r"\[MEASUREMENT_DIAG\]", limit=18),
        hit("测量健康", esp32, r"\[MEASUREMENT_HEALTH\]|measurement_health=", limit=18),
        hit("测量平面有效样本", esp32, r"\[LAYER:MEASUREMENT_PLANE\].*valid=1", limit=18),
        hit("测量平面无效样本", esp32, r"\[LAYER:MEASUREMENT_PLANE\].*valid=0", limit=18),
        hit("RS485 / Modbus 失败", esp32, r"Modbus read fail|READ_FAIL|\[MEASUREMENT_TRANSIENT\]", limit=24),
        hit("BLE stream 压制", esp32, r"queue=stream suppressed_for_control|queue=stream high_watermark", limit=24),
        hit("BLE baseline 分片", esp32, r"notify_fragmented.*prefix=EVT:BASELINE", limit=12),
    ]

    lines: list[str] = [
        "# ESP32 Demo Telemetry / Calibration Audit",
        "",
        f"- Capture: `{capture_dir}`",
        f"- Verdict: `{verdict}`",
        f"- measurement_diag_windows: `{diag_windows}`",
        f"- measurement_diag_ok_total: `{diag_ok}`",
        f"- measurement_diag_fail_total: `{diag_fail}`",
        f"- measurement_diag_max_read_ms: `{diag_max_read_ms}`",
        f"- esp32_measurement_plane_valid: `{esp32_plane_valid}`",
        f"- esp32_measurement_plane_invalid: `{esp32_plane_invalid}`",
        f"- android_snapshot_ready_lines: `{android_snapshot_ready}`",
        f"- android_baseline_lines: `{android_baseline_lines}`",
        f"- android_evt_stream_lines: `{android_stream_lines}`",
        f"- android_evt_stream_chunks: `{android_stream_chunks}`",
        f"- app_consume_valid: `{consume_valid}`",
        f"- app_consume_invalid: `{consume_invalid}`",
        f"- app_consume_ignored: `{consume_ignored}`",
        f"- stream_suppressed_for_control_lines: `{stream_suppressed}`",
        f"- stream_suppressed_for_control_last_total: `{stream_suppressed_total if stream_suppressed_total is not None else 'unknown'}`",
        f"- baseline_fragmented_lines: `{baseline_fragmented}`",
        f"- calibration_precondition_lines: `{cal_preconditions}`",
        f"- calibration_point_lines: `{cal_points}`",
        f"- calibration_no_distance_lines: `{cal_no_distance}`",
        "",
        "## Findings",
        "",
    ]
    lines.extend(f"- {item}" for item in findings) if findings else lines.append("- none")
    lines.extend(["", "## Evidence Gaps", ""])
    lines.extend(f"- {item}" for item in evidence_gaps) if evidence_gaps else lines.append("- 暂无关键证据缺口。")
    lines.extend(["", "## Observations", ""])
    lines.extend(f"- {item}" for item in observations) if observations else lines.append("- none")
    lines.append("")
    lines.extend(section_hits("Android / Demo APP Evidence", android_hits))
    lines.extend(section_hits("ESP32 Evidence", esp32_hits))
    lines.extend(section_hits("Fatal Runtime Evidence", [Hit("致命异常", len(fatal_lines), fatal_lines[:8])]))
    lines.extend(
        [
            "## Human Notes",
            "",
            "```text",
            notes.strip()[:5000] if notes.strip() else "missing",
            "```",
            "",
            "## Session Meta",
            "",
            "```text",
            meta.strip()[:5000] if meta.strip() else "missing",
            "```",
            "",
            "## Warnings",
            "",
            "```text",
            warnings.strip()[:5000] if warnings.strip() else "none",
            "```",
            "",
            "## Capture Summary",
            "",
            "```text",
            summary.strip()[:7000] if summary.strip() else "missing",
            "```",
            "",
        ]
    )
    return verdict, "\n".join(lines)


def main() -> int:
    capture_dir = Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else latest_capture_dir()
    if capture_dir is None:
        print("[esp32-demo-telemetry-calibration-audit] no matching capture found", file=sys.stderr)
        return 2
    if not capture_dir.exists() or not capture_dir.is_dir():
        print(f"[esp32-demo-telemetry-calibration-audit] capture dir not found: {capture_dir}", file=sys.stderr)
        return 2
    verdict, report = build_report(capture_dir)
    output = capture_dir / OUTPUT_NAME
    output.write_text(report, encoding="utf-8")
    print(output)
    return 1 if verdict.startswith("FAIL") else 0


if __name__ == "__main__":
    raise SystemExit(main())
