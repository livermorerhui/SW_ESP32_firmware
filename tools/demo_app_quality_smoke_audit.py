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
OUTPUT_NAME = "demo_app_quality_smoke_audit.md"


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
            if path.is_dir() and "demo_app_quality_smoke" in path.name
        )
    return max(candidates, key=lambda path: path.stat().st_mtime, default=None)


def compact(line: str) -> str:
    return re.sub(r"\s+", " ", line).strip()


def grep_lines(text: str, pattern: str, flags: int = re.IGNORECASE) -> list[str]:
    rx = re.compile(pattern, flags)
    return [compact(line) for line in text.splitlines() if rx.search(line)]


def has(text: str, pattern: str) -> bool:
    return bool(re.search(pattern, text, re.IGNORECASE | re.DOTALL))


def meta_value(meta: str, key: str) -> str:
    match = re.search(rf"^{re.escape(key)}=(.*)$", meta, re.MULTILINE)
    return match.group(1).strip() if match else ""


def hit(label: str, text: str, pattern: str, limit: int = 10) -> Hit:
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
        and not re.search(r"\b(GURU|PANIC|BROWNOUT)_COUNT=0\b", line, re.IGNORECASE)
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
    visual_dir = capture_dir / "visual_evidence"
    visual_file_count = (
        len([path for path in visual_dir.iterdir() if path.is_file()])
        if visual_dir.exists()
        else 0
    )

    android = "\n".join([android_focus, android_full, android_stop_snapshot, runtime_events])
    esp32 = "\n".join([esp32_focus, esp32_raw])
    combined = "\n".join([android, esp32, notes, meta, warnings, summary])

    fatal_lines = fatal_runtime_lines(combined)
    result = meta_value(meta, "RESULT").lower()
    user_passed = result == "pass" or has(
        notes,
        r"体感通过|整体.*通过|smoke.*passed|passed by user",
    )
    user_failed = result == "fail" or has(
        notes,
        r"用户观察.*(失败|异常|卡住|无数据|没.*数据|没有.*数据|failed)|体感.*(失败|异常)|failed by user",
    )

    marker_connected = has(notes, r"已连接.*实时|实时距离|实时.*可见|live")
    marker_start_stop = has(notes, r"Start.*Stop|start.*stop|启动.*停止|Stop 成功|停止成功")
    marker_tools = has(notes, r"校准工具|motion sampling|采样|device config|型号确认|保护开关")
    marker_information_architecture = has(notes, r"默认(设备|型号)页|顶部 Tab 固定|内容不重复|归零.*取消")
    marker_count = len(grep_lines(notes, r"^\[[^\]]+\].*(用户确认|用户观察)"))

    transport_lines = len(grep_lines(android, r"SonicWaveTransport"))
    snapshot_lines = len(grep_lines(android, r"RX line emitted=SNAPSHOT:|SNAPSHOT:"))
    stream_ack_lines = len(grep_lines(android, r"RX line emitted=ACK:STREAM .*enabled=1|ACK:STREAM .*enabled=1"))
    stream_lines = len(grep_lines(android, r"RX line emitted=EVT:STREAM|RX chunk .*EVT:STREAM"))
    wave_rx_lines = len(grep_lines(android, r"RX line emitted=.*ACK:WAVE|RX line emitted=.*EVT:WAVE|RX line emitted=.*WAVE"))
    android_marker_lines = len(grep_lines(android, r"SW_TEST_MARKER"))

    esp32_start_lines = len(grep_lines(esp32, r"WAVE_START|WAVE:START|cmd=WAVE:START|START ALLOW"))
    esp32_stop_lines = len(grep_lines(esp32, r"WAVE_STOP|WAVE:STOP|cmd=WAVE:STOP|STOP REQUEST|wave\.stopSoft|i2s_stop"))
    esp32_stream_lines = len(grep_lines(esp32, r"\[STREAM_CONTROL\]|EVT:STREAM|\[LAYER:MEASUREMENT_PLANE\]"))
    esp32_motion_lines = len(grep_lines(esp32, r"MOTION_SAMPLE_MODE|DEBUG:MOTION_SAMPLING|ACK:MOTION_SAMPLING"))
    esp32_config_lines = len(grep_lines(esp32, r"DEVICE:SET_CONFIG|ACK:DEVICE_CONFIG"))
    leave_protection_lines = len(
        grep_lines(combined, r"SAFETY:LEAVE_PROTECTION|ACK:LEAVE_PROTECTION|\[LEAVE_PROTECTION_UI\]")
    )
    zero_write_lines = grep_lines(combined, r"SCALE:ZERO|CAL:ZERO")
    esp32_available = bool(esp32.strip())

    findings: list[str] = []
    evidence_gaps: list[str] = []
    observations: list[str] = []

    if fatal_lines:
        findings.append("发现致命异常或设备 reset 证据（FATAL_RUNTIME_OR_DEVICE_RESET）。")
    if user_failed:
        findings.append("用户体感或人工 marker 标记失败（USER_REPORTED_FAILURE）。")
    if not notes.strip():
        evidence_gaps.append("notes.md 为空，缺少用户体感和操作标记。")
    if not marker_connected:
        evidence_gaps.append("缺少“连接后实时数据可见”的人工 marker。")
    if not marker_start_stop:
        evidence_gaps.append("缺少 Start -> Stop 完成后的人工 marker。")
    if not marker_tools:
        evidence_gaps.append("缺少 calibration / motion sampling / device config 工具区 smoke marker。")
    if not marker_information_architecture:
        observations.append("未采到信息架构 smoke marker：默认型号页、顶部 Tab 固定、内容不重复、归零取消路径。旧 capture 可接受；B10 第二阶段后建议补测。")
    if marker_information_architecture and has(notes, r"归零.*取消.*未写入") and zero_write_lines:
        findings.append("用户标记归零取消未写入，但日志中出现 SCALE:ZERO / CAL:ZERO 发送或接收证据（ZERO_CANCEL_CONFLICT）。")
    if android_marker_lines == 0:
        evidence_gaps.append("android_logcat_focus.log 没有 SW_TEST_MARKER，用户动作无法和日志精确对齐。")
    elif marker_count > android_marker_lines:
        if visual_file_count > 0:
            observations.append(
                "部分人工 marker 未进入 Android focus logcat，但已采到 notes.md 与 visual_evidence；本次用于 UI 可达性旁证，后续排查 UI 瞬态问题时应补强 marker 对齐。"
            )
        else:
            evidence_gaps.append("部分人工 marker 未进入 Android logcat，且缺少 visual_evidence 旁证。")
    if transport_lines == 0:
        evidence_gaps.append("Android logcat 未采到 SonicWaveTransport，无法证明 Demo APP BLE 传输层活跃。")
    if snapshot_lines == 0:
        evidence_gaps.append("Android 侧未采到 SNAPSHOT，连接后设备 truth 证据不足。")
    if stream_ack_lines == 0 and stream_lines == 0:
        evidence_gaps.append("Android 侧未采到 ACK:STREAM 或 EVT:STREAM，实时测量 smoke 证据不足。")
    if not esp32_available:
        evidence_gaps.append("ESP32 串口日志为空或未采集；start/stop 只能靠 APP 侧旁证。")
    else:
        if esp32_start_lines == 0:
            evidence_gaps.append("ESP32 串口未采到 WAVE:START / START ALLOW。")
        if esp32_stop_lines == 0:
            evidence_gaps.append("ESP32 串口未采到 WAVE:STOP / STOP REQUEST / i2s_stop。")
    if esp32_motion_lines == 0:
        observations.append("未采到 motion sampling 串口证据；如果用户未执行该步骤可接受，否则需补测。")
    if esp32_config_lines == 0:
        observations.append("未采到 device config 写入证据；该步骤按现场安全条件可选。")
    if "runtime_events_capture_unavailable" in warnings:
        observations.append(
            "Demo APP 没有正式 SW APP runtime_events 文件；本专项不把 runtime_events 缺失作为 blocker，改看 logcat / ESP32 / marker。"
        )
    logcat_stale_warning = "logcat_capture_stale_at_stop" in warnings
    logcat_stopped_warning = "logcat_capture_process_not_running_at_stop" in warnings
    if logcat_stale_warning or logcat_stopped_warning:
        if transport_lines > 0 and (snapshot_lines > 0 or stream_lines > 0):
            observations.append(
                "Android focus logcat 在 stop 时已停止或变 stale；本次仍有 Demo transport、SNAPSHOT/STREAM、ESP32 串口和 visual_evidence 旁证，所以不作为功能 blocker。后续若复现 UI 瞬态异常，优先补采集底座或启用更完整 logcat。"
            )
        else:
            evidence_gaps.append(
                "Android focus logcat 在 stop 时已停止或变 stale，且缺少足够 Demo transport 证据。"
            )

    if fatal_lines:
        verdict = "FAIL_FATAL_RUNTIME_OR_DEVICE_RESET"
    elif user_failed:
        verdict = "FAIL_USER_REPORTED"
    elif evidence_gaps and user_passed:
        verdict = "PARTIAL_PASS_EVIDENCE_GAP"
    elif evidence_gaps:
        verdict = "NEEDS_REVIEW_EVIDENCE_GAP"
    elif user_passed:
        verdict = "PASS_CANDIDATE"
    else:
        verdict = "NEEDS_REVIEW"

    android_hits = [
        hit("人工 marker", android, r"SW_TEST_MARKER", limit=12),
        hit("Demo BLE transport 活跃", android, r"SonicWaveTransport", limit=16),
        hit("SNAPSHOT truth", android, r"RX line emitted=SNAPSHOT:|SNAPSHOT:", limit=12),
        hit("实时流 ACK", android, r"RX line emitted=ACK:STREAM .*enabled=1|ACK:STREAM .*enabled=1", limit=12),
        hit("实时 EVT:STREAM", android, r"RX line emitted=EVT:STREAM|RX chunk .*EVT:STREAM", limit=16),
        hit("WAVE 相关回包", android, r"RX line emitted=.*ACK:WAVE|RX line emitted=.*EVT:WAVE|RX line emitted=.*WAVE", limit=12),
    ]
    esp32_hits = [
        hit("ESP32 Start", esp32, r"WAVE_START|WAVE:START|cmd=WAVE:START|START ALLOW", limit=12),
        hit("ESP32 Stop", esp32, r"WAVE_STOP|WAVE:STOP|cmd=WAVE:STOP|STOP REQUEST|wave\.stopSoft|i2s_stop", limit=16),
        hit("ESP32 stream / measurement", esp32, r"\[STREAM_CONTROL\]|EVT:STREAM|\[LAYER:MEASUREMENT_PLANE\]", limit=16),
        hit("Motion sampling", esp32, r"MOTION_SAMPLE_MODE|DEBUG:MOTION_SAMPLING|ACK:MOTION_SAMPLING", limit=12),
        hit("Device config", esp32, r"DEVICE:SET_CONFIG|ACK:DEVICE_CONFIG", limit=12),
        hit(
            "Leave protection",
            combined,
            r"SAFETY:LEAVE_PROTECTION|ACK:LEAVE_PROTECTION|\[LEAVE_PROTECTION_UI\]",
            limit=12,
        ),
    ]

    lines: list[str] = [
        "# Demo APP Quality Smoke Audit",
        "",
        f"- Capture: `{capture_dir}`",
        f"- Verdict: `{verdict}`",
        f"- marker_count: `{marker_count}`",
        f"- marker_connected_live: `{marker_connected}`",
        f"- marker_start_stop: `{marker_start_stop}`",
        f"- marker_tools: `{marker_tools}`",
        f"- marker_information_architecture: `{marker_information_architecture}`",
        f"- visual_evidence_files: `{visual_file_count}`",
        f"- android_marker_lines: `{android_marker_lines}`",
        f"- android_transport_lines: `{transport_lines}`",
        f"- android_snapshot_lines: `{snapshot_lines}`",
        f"- android_stream_ack_lines: `{stream_ack_lines}`",
        f"- android_stream_lines: `{stream_lines}`",
        f"- android_wave_rx_lines: `{wave_rx_lines}`",
        f"- esp32_start_lines: `{esp32_start_lines}`",
        f"- esp32_stop_lines: `{esp32_stop_lines}`",
        f"- esp32_stream_or_measurement_lines: `{esp32_stream_lines}`",
        f"- esp32_motion_lines: `{esp32_motion_lines}`",
        f"- esp32_config_lines: `{esp32_config_lines}`",
        f"- leave_protection_lines: `{leave_protection_lines}`",
        f"- zero_write_lines: `{len(zero_write_lines)}`",
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
        print("[demo-app-quality-smoke-audit] no matching capture found", file=sys.stderr)
        return 2
    if not capture_dir.exists() or not capture_dir.is_dir():
        print(f"[demo-app-quality-smoke-audit] capture dir not found: {capture_dir}", file=sys.stderr)
        return 2
    verdict, report = build_report(capture_dir)
    output = capture_dir / OUTPUT_NAME
    output.write_text(report, encoding="utf-8")
    print(output)
    return 1 if verdict.startswith("FAIL") else 0


if __name__ == "__main__":
    raise SystemExit(main())
