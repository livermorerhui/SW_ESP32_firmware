#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import shlex
import sys
from pathlib import Path


LOG_RE = re.compile(
    r"^\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d+\s+\d+\s+\d+\s+[A-Z]\s+([A-Z_]+)\s*:\s+(.*)$"
)


def parse_fields(field_text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in shlex.split(field_text):
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def parse_log(path: Path) -> dict:
    summary = {
        "case": path.stem,
        "file": str(path),
        "case_begin": False,
        "case_end": False,
        "connect_success": 0,
        "connect_protocol_partial": 0,
        "snapshot_synced": 0,
        "snapshot_latest": {},
        "start_enable_latest": {},
        "wave_confirmations": [],
        "session_states": [],
        "degraded_start_syncs": [],
        "degraded_start_acks": [],
        "interrupted_by_disconnect": False,
        "stale_disconnect_seen": False,
        "invalid_confirmation_source_seen": False,
        "notes": [],
    }

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LOG_RE.match(raw_line)
        if not match:
            continue
        tag, message = match.groups()
        event, _, field_text = message.partition(" ")
        fields = parse_fields(field_text)

        if tag == "SW_CASE" and event == "CASE_BEGIN":
            summary["case_begin"] = True
        elif tag == "SW_CASE" and event == "CASE_END":
            summary["case_end"] = True
        elif tag == "SW_CONNECT" and event == "CONNECT_SUCCESS":
            summary["connect_success"] += 1
        elif tag == "SW_CONNECT" and event == "CONNECT_PROTOCOL_PARTIAL":
            summary["connect_protocol_partial"] += 1
        elif tag == "SW_STATE" and event == "DEVICE_SNAPSHOT_SYNCED":
            summary["snapshot_synced"] += 1
            summary["snapshot_latest"] = fields
            if fields.get("truth_state") == "stale_diagnostic_only" and fields.get("stale_reason") == "disconnected":
                summary["stale_disconnect_seen"] = True
        elif tag == "SW_SESSION" and event == "START_ENABLE_EVALUATED":
            summary["start_enable_latest"] = fields
        elif tag == "SW_STATE" and event == "DEVICE_WAVE_OUTPUT_CONFIRMATION":
            summary["wave_confirmations"].append(fields)
            source = fields.get("source")
            is_fresh = fields.get("is_fresh")
            if is_fresh == "true" and source in {"state_event", "stop_event", "safety_event"}:
                summary["invalid_confirmation_source_seen"] = True
        elif tag == "SW_STATE" and event == "SESSION_STATE_CHANGED":
            new_state = fields.get("new_state")
            if new_state:
                summary["session_states"].append(new_state)
        elif tag == "SW_STATE" and event == "DEVICE_DEGRADED_START_SYNCED":
            summary["degraded_start_syncs"].append(fields)
        elif tag == "SW_STATE" and event == "DEVICE_DEGRADED_START_ACK":
            summary["degraded_start_acks"].append(fields)
        elif tag == "SW_STOP" and event == "SESSION_INTERRUPTED_BY_DISCONNECT":
            summary["interrupted_by_disconnect"] = True

    return summary


def latest_bool(fields: dict[str, str], key: str) -> bool | None:
    raw = fields.get(key)
    if raw is None:
        return None
    if raw.lower() == "true":
        return True
    if raw.lower() == "false":
        return False
    return None


def has_state_cycle(normalized_states: list[str], cycle: list[str]) -> bool:
    if not cycle:
        return False
    index = 0
    for state in normalized_states:
        if state == cycle[index]:
            index += 1
            if index == len(cycle):
                return True
    return False


def evaluate(summary: dict) -> dict:
    case = summary["case"]
    states = summary["session_states"]
    normalized_states = [state.upper() for state in states]
    latest_snapshot = summary["snapshot_latest"]
    latest_gate = summary["start_enable_latest"]
    result = {
        "case": case,
        "pass": False,
        "reason": "",
        "connect_success": summary["connect_success"] > 0,
        "snapshot_synced": summary["snapshot_synced"] > 0,
        "invalid_confirmation_source_seen": summary["invalid_confirmation_source_seen"],
        "latest_start_action_valid": latest_bool(latest_gate, "start_action_valid"),
        "latest_start_ready": latest_bool(latest_snapshot, "start_ready"),
        "latest_baseline_ready": latest_bool(latest_snapshot, "baseline_ready"),
        "latest_degraded_start_available": latest_bool(latest_snapshot, "degraded_start_available"),
        "latest_degraded_start_enabled": latest_bool(latest_snapshot, "degraded_start_enabled"),
        "session_states": states,
    }

    if not summary["case_begin"] or not summary["case_end"]:
        result["reason"] = "missing CASE_BEGIN/CASE_END"
        return result
    if summary["connect_protocol_partial"] > 0:
        result["reason"] = "connect protocol partial"
        return result
    if summary["invalid_confirmation_source_seen"]:
        result["reason"] = "invalid supporting confirmation source seen"
        return result

    if case == "base_connect_start_stop":
        ok = (
            result["connect_success"]
            and result["snapshot_synced"]
            and "RUNNING" in normalized_states
            and "STOPPED_BY_USER" in normalized_states
        )
        result["pass"] = ok
        result["reason"] = "ok" if ok else "expected RUNNING and STOPPED_BY_USER"
        return result

    if case == "base_reconnect":
        ok = (
            summary["connect_success"] >= 2
            and summary["snapshot_synced"] >= 2
            and (summary["interrupted_by_disconnect"] or summary["stale_disconnect_seen"])
        )
        result["pass"] = ok
        result["reason"] = "ok" if ok else "expected reconnect + fresh snapshot after disconnect"
        return result

    if case == "plus_degraded_first_connect":
        ok = (
            result["connect_success"]
            and result["snapshot_synced"]
            and result["latest_degraded_start_available"] is True
            and result["latest_degraded_start_enabled"] is False
        )
        result["pass"] = ok
        result["reason"] = "ok" if ok else "expected degraded_start_available=true and enabled=false"
        return result

    if case == "plus_degraded_start_stop":
        enabled_seen = any(latest_bool(item, "enabled") is True for item in summary["degraded_start_syncs"])
        ack_seen = any(latest_bool(item, "enabled") is True for item in summary["degraded_start_acks"])
        ok = (
            result["connect_success"]
            and result["snapshot_synced"]
            and (enabled_seen or ack_seen or result["latest_degraded_start_enabled"] is True)
            and "RUNNING" in normalized_states
            and "STOPPED_BY_USER" in normalized_states
        )
        result["pass"] = ok
        result["reason"] = "ok" if ok else "expected degraded-start enable + RUNNING + STOPPED_BY_USER"
        return result

    if case == "plus_degraded_reconnect":
        reconnect_state_cycle_seen = has_state_cycle(normalized_states, ["READY", "IDLE", "READY"])
        strong_reconnect_evidence = (
            summary["connect_success"] >= 2
            and summary["snapshot_synced"] >= 2
            and summary["interrupted_by_disconnect"]
        )
        soft_reconnect_evidence = (
            result["connect_success"]
            and result["snapshot_synced"]
            and reconnect_state_cycle_seen
        )
        ok = (
            (strong_reconnect_evidence or soft_reconnect_evidence)
            and result["latest_degraded_start_available"] is True
            and result["latest_degraded_start_enabled"] is True
        )
        result["pass"] = ok
        result["reason"] = "ok" if ok else "expected reconnect + fresh degraded snapshot truth"
        return result

    ok = result["connect_success"] and result["snapshot_synced"]
    result["pass"] = ok
    result["reason"] = "ok" if ok else "expected connect_success + snapshot_synced"
    return result


def main() -> int:
    if len(sys.argv) > 2:
        print("Usage: tools/swapp_log_report.py [log_dir]", file=sys.stderr)
        return 1

    log_dir = Path(sys.argv[1]).expanduser() if len(sys.argv) == 2 else Path.home() / "swapp-logs"
    if not log_dir.exists():
        print(f"log dir not found: {log_dir}", file=sys.stderr)
        return 1

    log_files = sorted(log_dir.glob("*.log"))
    if not log_files:
        print(f"no .log files in {log_dir}", file=sys.stderr)
        return 1

    parsed = [parse_log(path) for path in log_files]
    evaluated = [evaluate(item) for item in parsed]

    report = {
        "log_dir": str(log_dir),
        "total": len(evaluated),
        "passed": sum(1 for item in evaluated if item["pass"]),
        "results": evaluated,
    }

    report_path = log_dir / "swapp_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    for item in evaluated:
        status = "PASS" if item["pass"] else "FAIL"
        print(f"{status} {item['case']} - {item['reason']}")

    print(f"\nSaved: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
