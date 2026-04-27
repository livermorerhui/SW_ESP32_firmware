#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


BASELINE_SAMPLES = 10
MOVING_AVERAGE_WINDOW = 5
DEFAULT_SAMPLE_INTERVAL_MS = 200.0
MIN_RELIABLE_SESSION_MS = 10000.0

LEAVE_LOW_WEIGHT_RATIO = 0.25
LEAVE_LOW_WEIGHT_KG = 5.0
LEAVE_DISTANCE_MIGRATION = 4.0
LEAVE_CONFIRM_SAMPLES = 5

FALL_DEEP_LOW_WEIGHT_RATIO = 0.60
FALL_TAIL_LOW_RATIO = 0.75
FALL_LOW_WEIGHT_MIN_SAMPLES = 5
FALL_MIGRATION_DELTA_W_KG = 20.0
FALL_MIGRATION_DELTA_D = 2.0
FALL_MIGRATION_RECOVERY_MAX = 0.75
FALL_SUPPORT_DELTA_RATIO = 0.25
FALL_SUPPORT_MIGRATION_DELTA_D = 1.8
FALL_SUPPORT_RECOVERY_MAX = 0.90
FALL_SUPPORT_DEVIATION_MIN = 0.25
FALL_FAST_DROP_RATE_KG_PER_SEC = 25.0
FALL_FAST_DROP_CONFIRM_SAMPLES = 2
FALL_FAST_DROP_RECOVERY_MAX = 0.96

FORMAL_VALIDATION_EXCLUDED_QUALITY_BUCKETS = {
    "leave_export_truncated_before_formal_evidence",
    "leave_export_missing_formal_evidence",
    "weak_fall_label_without_formal_evidence",
    "conflicting_label_and_stop_reason",
    "no_valid_samples",
}


@dataclass
class ReplaySample:
    timestamp_ms: float
    distance: float
    weight_kg: float
    sample_valid: bool
    runtime_state: str
    wave_state: str
    raw: dict[str, Any]


@dataclass
class ReplaySession:
    path: Path
    source: str
    file_type: str
    primary_label: str
    secondary_label: str
    meta: dict[str, Any]
    samples: list[ReplaySample]


def parse_number(value: Any) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        number = float(value)
        return None if math.isnan(number) else number
    text = str(value).strip()
    if text == "" or text.lower() in {"nan", "none", "null"}:
        return None
    try:
        number = float(text)
    except ValueError:
        return None
    return None if math.isnan(number) else number


def parse_bool(value: Any, default: bool = True) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "y", "valid"}:
        return True
    if text in {"0", "false", "no", "n", "invalid"}:
        return False
    return default


def coerce_meta_value(value: str) -> Any:
    text = value.strip().strip(",").strip()
    if text == "":
        return ""
    lower = text.lower()
    if lower == "true":
        return True
    if lower == "false":
        return False
    if lower in {"none", "null"}:
        return None
    number = parse_number(text)
    if number is not None:
        return int(number) if number.is_integer() else number
    return text


def read_csv_with_optional_comments(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    meta: dict[str, Any] = {}
    data_lines: list[str] = []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for line in handle:
            stripped = line.rstrip("\n")
            if stripped.startswith("#"):
                content = stripped[1:].strip()
                if ":" in content:
                    key, value = content.split(":", 1)
                    meta[key.strip()] = coerce_meta_value(value)
            else:
                data_lines.append(line)

    if not data_lines:
        return meta, []

    reader = csv.DictReader(data_lines)
    return meta, list(reader)


def read_json_session(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    meta = payload.get("meta", {})
    samples = payload.get("samples", [])
    return meta if isinstance(meta, dict) else {}, samples if isinstance(samples, list) else []


def first_value(row: dict[str, Any], keys: Iterable[str]) -> Any:
    for key in keys:
        if key in row and row[key] not in (None, ""):
            return row[key]
    return None


def parse_filename_labels(path: Path) -> tuple[str, str]:
    stem = path.stem
    parts = stem.split("_")
    if len(parts) >= 2:
        return parts[0], parts[1]
    return "UNKNOWN", "UNKNOWN"


def labels_from_meta_or_filename(path: Path, meta: dict[str, Any]) -> tuple[str, str]:
    primary = (
        meta.get("一级标签")
        or meta.get("primary_label")
        or meta.get("primaryLabel")
    )
    secondary = (
        meta.get("二级标签")
        or meta.get("secondary_label")
        or meta.get("secondaryLabel")
    )
    if primary or secondary:
        return str(primary or "UNKNOWN"), str(secondary or "UNKNOWN")
    return parse_filename_labels(path)


def meta_string(meta: dict[str, Any], *keys: str) -> str:
    for key in keys:
        value = meta.get(key)
        if value is not None:
            return str(value).strip().strip(",").strip().upper()
    return ""


def normalize_sample(row: dict[str, Any], index: int) -> ReplaySample | None:
    timestamp = parse_number(first_value(row, ["timestamp_ms", "timestampMs", "elapsedMs", "elapsed_ms"]))
    distance = parse_number(first_value(row, ["distance", "distanceMm", "distance_mm"]))
    weight = parse_number(first_value(row, ["weight", "liveWeightKg", "weightKg", "stable_weight"]))
    if distance is None or weight is None:
        return None
    if timestamp is None:
        timestamp = float(index) * DEFAULT_SAMPLE_INTERVAL_MS
    return ReplaySample(
        timestamp_ms=timestamp,
        distance=distance,
        weight_kg=weight,
        sample_valid=parse_bool(first_value(row, ["sampleValid", "measurementValid", "valid"]), default=True),
        runtime_state=str(first_value(row, ["runtimeStateCode", "runtime_state", "topState"]) or "RUNNING").upper(),
        wave_state=str(first_value(row, ["waveStateCode", "wave_state"]) or "UNKNOWN").upper(),
        raw=row,
    )


def load_session_file(path: Path, source: str) -> ReplaySession:
    if path.suffix.lower() == ".json":
        meta, rows = read_json_session(path)
        file_type = "json"
    else:
        meta, rows = read_csv_with_optional_comments(path)
        file_type = "csv"

    primary, secondary = labels_from_meta_or_filename(path, meta)
    samples = [
        sample
        for idx, row in enumerate(rows)
        if (sample := normalize_sample(row, idx)) is not None
    ]
    return ReplaySession(
        path=path,
        source=source,
        file_type=file_type,
        primary_label=primary,
        secondary_label=secondary,
        meta=meta,
        samples=samples,
    )


def moving_average(values: list[float], window: int) -> list[float]:
    out: list[float] = []
    current_sum = 0.0
    queue: list[float] = []
    for value in values:
        queue.append(value)
        current_sum += value
        if len(queue) > window:
            current_sum -= queue.pop(0)
        out.append(current_sum / len(queue))
    return out


def longest_true_run(mask: list[bool]) -> int:
    longest = 0
    current = 0
    for value in mask:
        if value:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def median_interval_ms(samples: list[ReplaySample]) -> float:
    intervals = [
        samples[i].timestamp_ms - samples[i - 1].timestamp_ms
        for i in range(1, len(samples))
        if samples[i].timestamp_ms > samples[i - 1].timestamp_ms
    ]
    if not intervals:
        return DEFAULT_SAMPLE_INTERVAL_MS
    intervals.sort()
    return intervals[len(intervals) // 2]


def first_true_timestamp(samples: list[ReplaySample], mask: list[bool]) -> float | None:
    for sample, value in zip(samples, mask):
        if value:
            return sample.timestamp_ms
    return None


def max_drop_rate_kg_per_sec(samples: list[ReplaySample], weights: list[float]) -> float:
    max_rate = 0.0
    for idx in range(1, len(samples)):
        dt = (samples[idx].timestamp_ms - samples[idx - 1].timestamp_ms) / 1000.0
        if dt <= 0:
            continue
        rate = (weights[idx - 1] - weights[idx]) / dt
        max_rate = max(max_rate, rate)
    return max_rate


def consecutive_fast_drop_count(samples: list[ReplaySample], weights: list[float]) -> int:
    longest = 0
    current = 0
    for idx in range(1, len(samples)):
        dt = (samples[idx].timestamp_ms - samples[idx - 1].timestamp_ms) / 1000.0
        if dt <= 0:
            continue
        rate = (weights[idx - 1] - weights[idx]) / dt
        if rate >= FALL_FAST_DROP_RATE_KG_PER_SEC:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def expected_bucket(primary: str, secondary: str, filename: str) -> str:
    text = f"{primary}_{secondary}_{filename}"
    if "平台外摔倒" in text:
        return "leave"
    if "律动离开" in text or "离开平台" in text:
        return "leave"
    if "摔倒" in text or "危险状态" in text:
        return "fall_danger"
    if "正常" in text or "站立" in text or "摇摆" in text or "下蹲" in text or "手部" in text or "调整站姿" in text:
        return "normal"
    return "unknown"


def matrix_id(primary: str, secondary: str, filename: str, bucket: str) -> str:
    text = f"{primary}_{secondary}_{filename}"
    if bucket == "leave":
        return "MS-11" if "平台外摔倒" in text else "MS-07"
    if bucket == "fall_danger":
        if "抓住" in text or "屁股" in text:
            return "MS-10"
        return "MS-09"
    if "站立" in text:
        return "MS-01"
    if "下蹲" in text:
        return "MS-03"
    if "摇摆" in text or "手部" in text or "调整站姿" in text:
        return "MS-04"
    if bucket == "normal":
        return "MS-02"
    return "MS-UNKNOWN"


def derive_presence_evidence(primary: str, secondary: str, filename: str) -> tuple[str, str, str]:
    text = f"{primary}_{secondary}_{filename}"
    if "平台外摔倒" in text or "离开平台" in text or "律动离开" in text:
        return "ABSENT_LABEL_HINT", "LABEL_ONLY", "label indicates user left platform"
    if "平台上摔倒" in text or "腿在平台" in text or "屁股在平台" in text or "抓住" in text:
        return "PRESENT_LABEL_HINT", "LABEL_ONLY", "label indicates user remains on or supported by platform"
    return "UNKNOWN", "NONE", ""


def has_non_none_raw_value(samples: list[ReplaySample], key: str) -> bool:
    for sample in samples:
        value = str(sample.raw.get(key, "")).strip().upper()
        if value and value != "NONE":
            return True
    return False


def data_quality_bucket_for(
    session: ReplaySession,
    samples: list[ReplaySample],
    expected: str,
    leave_confirmed: bool,
    weight_drop_kg: float,
    baseline_weight: float,
    recovery_ratio: float,
) -> tuple[str, str]:
    if not samples:
        return "no_valid_samples", "no valid sample rows"
    duration_ms = samples[-1].timestamp_ms - samples[0].timestamp_ms
    drop_ratio = weight_drop_kg / baseline_weight if baseline_weight > 0 else 0.0
    raw_main_states = {
        str(sample.raw.get("main_state", "")).strip().upper()
        for sample in samples
        if str(sample.raw.get("main_state", "")).strip()
    }
    has_runtime_state = any(state not in {"", "BASELINE_PENDING"} for state in raw_main_states)
    has_event_evidence = has_non_none_raw_value(samples, "event_aux") or has_non_none_raw_value(samples, "risk_advisory")
    meta_stop_reason = meta_string(session.meta, "stop_reason", "stopReason")

    if expected == "fall_danger" and meta_stop_reason == "USER_LEFT_PLATFORM":
        return (
            "conflicting_label_and_stop_reason",
            "label says fall/danger but exported formal stop reason says USER_LEFT_PLATFORM",
        )
    if expected == "fall_danger" and not has_runtime_state and not has_event_evidence:
        if drop_ratio < FALL_SUPPORT_DELTA_RATIO or recovery_ratio > FALL_SUPPORT_RECOVERY_MAX:
            return (
                "weak_fall_label_without_formal_evidence",
                "label says fall/danger but export has no formal event evidence and recovers close to baseline",
            )
    if expected == "leave" and session.source == "session_exports" and not leave_confirmed:
        if meta_stop_reason == "USER_LEFT_PLATFORM":
            return "usable_for_shadow_replay", ""
        if not has_runtime_state and not has_event_evidence:
            if duration_ms < MIN_RELIABLE_SESSION_MS:
                return (
                    "leave_export_truncated_before_formal_evidence",
                    "session export ends before formal runtime/stop evidence is visible",
                )
            return (
                "leave_export_missing_formal_evidence",
                "session export has no runtime/stop/event evidence for leave validation",
            )
        if drop_ratio < 0.35 or recovery_ratio > 0.90:
            return (
                "leave_signal_not_visible",
                "label says leave but weight/distance window does not show confirmed leave",
            )
    return "usable_for_shadow_replay", ""


def classify_review_bucket(
    expected: str,
    mapped_reason: str,
    fall_detail: str,
    weight_drop_kg: float,
    baseline_weight: float,
    distance_migration: float,
    recovery_ratio: float,
    fall_candidate: bool,
    leave_confirmed: bool,
    meta_stop_reason: str,
) -> str:
    drop_ratio = weight_drop_kg / baseline_weight if baseline_weight > 0 else 0.0
    if expected == "normal" and mapped_reason != "NONE":
        if fall_detail == "DANGER_FAST_DROP_CONFIRMED" and recovery_ratio > FALL_FAST_DROP_RECOVERY_MAX:
            return "detector_false_positive_recovered_fast_drop"
        return "detector_false_positive"
    if expected == "leave" and mapped_reason == "NONE":
        if drop_ratio < 0.35 or recovery_ratio > 0.90:
            return "data_or_label_gap_leave_signal_not_visible"
        return "detector_recall_gap_leave"
    if expected == "fall_danger" and mapped_reason == "USER_LEFT_PLATFORM":
        if meta_stop_reason == "USER_LEFT_PLATFORM":
            return "data_or_label_gap_conflicting_stop_reason"
        if fall_candidate and leave_confirmed:
            return "arbitration_gap_needs_user_presence_evidence"
        return "arbitration_gap"
    if expected == "fall_danger" and mapped_reason == "NONE":
        if drop_ratio < FALL_SUPPORT_DELTA_RATIO or distance_migration < FALL_SUPPORT_MIGRATION_DELTA_D:
            return "data_or_label_gap_weak_fall_signal"
        return "detector_recall_gap_support_or_partial_load"
    return ""


def evaluate_session(session: ReplaySession) -> dict[str, Any]:
    samples = [sample for sample in session.samples if sample.sample_valid]
    filename = session.path.name
    bucket = expected_bucket(session.primary_label, session.secondary_label, filename)
    ms_id = matrix_id(session.primary_label, session.secondary_label, filename, bucket)
    presence_state, presence_source, presence_note = derive_presence_evidence(
        session.primary_label,
        session.secondary_label,
        filename,
    )

    if len(samples) < BASELINE_SAMPLES:
        return {
            "file_name": filename,
            "file_path": str(session.path),
            "source": session.source,
            "file_type": session.file_type,
            "primary_label": session.primary_label,
            "secondary_label": session.secondary_label,
            "scenario_label": f"{session.primary_label}/{session.secondary_label}",
            "expected_bucket": bucket,
            "matrix_id": ms_id,
            "sample_count": len(samples),
            "matrix_result": "ERROR",
            "mismatch_reason": "not_enough_valid_samples",
        }

    weights = [sample.weight_kg for sample in samples]
    distances = [sample.distance for sample in samples]
    ma_weight = moving_average(weights, MOVING_AVERAGE_WINDOW)
    ma_distance = moving_average(distances, MOVING_AVERAGE_WINDOW)
    baseline_weight = parse_number(session.meta.get("stable_weight"))
    if baseline_weight is None:
        baseline_weight = sum(ma_weight[:BASELINE_SAMPLES]) / BASELINE_SAMPLES
    baseline_distance = sum(ma_distance[:BASELINE_SAMPLES]) / BASELINE_SAMPLES

    min_weight = min(ma_weight)
    max_weight = max(ma_weight)
    min_distance = min(ma_distance)
    max_distance = max(ma_distance)
    tail_weight_mean = sum(ma_weight[-BASELINE_SAMPLES:]) / min(BASELINE_SAMPLES, len(ma_weight))
    weight_drop_kg = baseline_weight - min_weight
    distance_migration = baseline_distance - min_distance
    recovery_ratio = tail_weight_mean / baseline_weight if baseline_weight > 0 else 1.0
    baseline_deviation_ratio = max(
        abs(value - baseline_weight) / baseline_weight
        for value in ma_weight
    ) if baseline_weight > 0 else 0.0

    interval_ms = median_interval_ms(samples)
    meta_stop_reason = meta_string(session.meta, "stop_reason", "stopReason")
    meta_stop_source = meta_string(session.meta, "stop_source", "stopSource")
    formal_stop_confirms_leave = meta_stop_reason == "USER_LEFT_PLATFORM"
    leave_low_mask = [
        value <= LEAVE_LOW_WEIGHT_KG or value <= baseline_weight * LEAVE_LOW_WEIGHT_RATIO
        for value in ma_weight
    ]
    fall_low_mask = [value <= baseline_weight * FALL_DEEP_LOW_WEIGHT_RATIO for value in ma_weight]
    leave_low_run = longest_true_run(leave_low_mask)
    fall_low_run = longest_true_run(fall_low_mask)
    low_weight_run_ms = fall_low_run * interval_ms
    sensor_leave_confirmed = (
        leave_low_run >= LEAVE_CONFIRM_SAMPLES
        or (
            weight_drop_kg >= baseline_weight * 0.55
            and distance_migration >= LEAVE_DISTANCE_MIGRATION
            and recovery_ratio <= FALL_MIGRATION_RECOVERY_MAX
        )
    )
    leave_confirmed = sensor_leave_confirmed or formal_stop_confirms_leave
    leave_candidate = any(leave_low_mask) or (
        weight_drop_kg >= baseline_weight * 0.35 and distance_migration >= LEAVE_DISTANCE_MIGRATION * 0.5
    )

    deep_low = (
        fall_low_run >= FALL_LOW_WEIGHT_MIN_SAMPLES
        and tail_weight_mean <= baseline_weight * FALL_TAIL_LOW_RATIO
    )
    migration_unrecovered = (
        weight_drop_kg >= FALL_MIGRATION_DELTA_W_KG
        and recovery_ratio <= FALL_MIGRATION_RECOVERY_MAX
        and distance_migration >= FALL_MIGRATION_DELTA_D
    )
    partial_support_migration = (
        weight_drop_kg >= baseline_weight * FALL_SUPPORT_DELTA_RATIO
        and distance_migration >= FALL_SUPPORT_MIGRATION_DELTA_D
        and recovery_ratio <= FALL_SUPPORT_RECOVERY_MAX
        and baseline_deviation_ratio >= FALL_SUPPORT_DEVIATION_MIN
    )
    fast_drop_count = consecutive_fast_drop_count(samples, ma_weight)
    fast_drop_recovery_confirmed = recovery_ratio <= FALL_FAST_DROP_RECOVERY_MAX
    fast_drop = (
        fast_drop_count >= FALL_FAST_DROP_CONFIRM_SAMPLES
        and (
            deep_low
            or migration_unrecovered
            or partial_support_migration
            or fast_drop_recovery_confirmed
        )
    )
    max_drop_rate = max_drop_rate_kg_per_sec(samples, ma_weight)
    fall_candidate = deep_low or migration_unrecovered or partial_support_migration or fast_drop
    fall_detail = "NONE"
    if deep_low:
        fall_detail = "DANGER_DEEP_LOW_LOAD"
    elif migration_unrecovered:
        fall_detail = "DANGER_MIGRATION_UNRECOVERED"
    elif partial_support_migration:
        fall_detail = "DANGER_MIGRATION_PARTIAL_SUPPORT"
    elif fast_drop:
        fall_detail = "DANGER_FAST_DROP_CONFIRMED"

    label_presence_arbitrates_to_fall = (
        fall_candidate
        and leave_confirmed
        and presence_state == "PRESENT_LABEL_HINT"
    )
    fall_confirmed = fall_candidate and (not leave_confirmed or label_presence_arbitrates_to_fall)
    mapped_reason = "NONE"
    mapped_effect_if_enabled = "NONE"
    mapped_effect_if_disabled = "NONE"
    mapped_reason_source = "SENSOR_ONLY"
    if fall_confirmed:
        mapped_reason = "FALL_SUSPECTED"
        mapped_effect_if_enabled = "ABNORMAL_STOP"
        mapped_effect_if_disabled = "WARNING_ONLY"
        if label_presence_arbitrates_to_fall:
            mapped_reason_source = "OFFLINE_LABEL_PRESENCE_HINT"
    elif leave_confirmed:
        mapped_reason = "USER_LEFT_PLATFORM"
        mapped_effect_if_enabled = "RECOVERABLE_PAUSE"
        mapped_effect_if_disabled = "RECOVERABLE_PAUSE"
        if formal_stop_confirms_leave and not sensor_leave_confirmed:
            mapped_reason_source = "META_STOP_REASON"

    if bucket == "normal":
        passed = not leave_confirmed and not fall_confirmed
        mismatch_reason = "" if passed else f"expected_none_got_{mapped_reason}"
    elif bucket == "leave":
        passed = leave_confirmed and mapped_reason == "USER_LEFT_PLATFORM"
        mismatch_reason = "" if passed else f"expected_leave_got_{mapped_reason}"
    elif bucket == "fall_danger":
        passed = fall_confirmed and mapped_reason == "FALL_SUSPECTED"
        mismatch_reason = "" if passed else f"expected_fall_got_{mapped_reason}"
    else:
        passed = True
        mismatch_reason = "unknown_expected_bucket"
    review_bucket = "" if passed else classify_review_bucket(
        bucket,
        mapped_reason,
        fall_detail,
        weight_drop_kg,
        baseline_weight,
        distance_migration,
        recovery_ratio,
        fall_candidate,
        leave_confirmed,
        meta_stop_reason,
    )
    data_quality_bucket, data_quality_note = data_quality_bucket_for(
        session,
        samples,
        bucket,
        leave_confirmed,
        weight_drop_kg,
        baseline_weight,
        recovery_ratio,
    )
    duration_ms = samples[-1].timestamp_ms - samples[0].timestamp_ms

    return {
        "file_name": filename,
        "file_path": str(session.path),
        "source": session.source,
        "file_type": session.file_type,
        "primary_label": session.primary_label,
        "secondary_label": session.secondary_label,
        "scenario_label": f"{session.primary_label}/{session.secondary_label}",
        "expected_bucket": bucket,
        "matrix_id": ms_id,
        "sample_count": len(samples),
        "session_duration_ms": round(duration_ms, 2),
        "data_quality_bucket": data_quality_bucket,
        "data_quality_note": data_quality_note,
        "baseline_weight": round(baseline_weight, 4),
        "baseline_distance": round(baseline_distance, 4),
        "min_weight_kg": round(min_weight, 4),
        "max_weight_kg": round(max_weight, 4),
        "min_distance": round(min_distance, 4),
        "max_distance": round(max_distance, 4),
        "tail_weight_mean": round(tail_weight_mean, 4),
        "recovery_ratio": round(recovery_ratio, 4),
        "baseline_deviation_ratio": round(baseline_deviation_ratio, 4),
        "leave_candidate": leave_candidate,
        "leave_confirmed": leave_confirmed,
        "sensor_leave_confirmed": sensor_leave_confirmed,
        "formal_stop_confirms_leave": formal_stop_confirms_leave,
        "fall_candidate": fall_candidate,
        "fall_confirmed": fall_confirmed,
        "fall_detail": fall_detail,
        "fall_partial_support_migration": partial_support_migration,
        "fall_fast_drop_recovery_confirmed": fast_drop_recovery_confirmed,
        "presence_state": presence_state,
        "presence_evidence_source": presence_source,
        "presence_evidence_note": presence_note,
        "presence_used_for_arbitration": label_presence_arbitrates_to_fall,
        "meta_stop_reason": meta_stop_reason,
        "meta_stop_source": meta_stop_source,
        "mapped_reason": mapped_reason,
        "mapped_reason_source": mapped_reason_source,
        "mapped_effect_if_enabled": mapped_effect_if_enabled,
        "mapped_effect_if_disabled": mapped_effect_if_disabled,
        "first_candidate_ms": first_true_timestamp(samples, leave_low_mask) if leave_candidate else "",
        "confirmed_at_ms": first_true_timestamp(samples, leave_low_mask) if leave_confirmed else "",
        "evidence_weight_drop_kg": round(weight_drop_kg, 4),
        "evidence_distance_migration": round(distance_migration, 4),
        "evidence_low_weight_run_ms": round(low_weight_run_ms, 2),
        "evidence_unrecovered_ms": "",
        "evidence_max_drop_rate_kg_per_sec": round(max_drop_rate, 4),
        "evidence_fast_drop_count": fast_drop_count,
        "matrix_result": "PASS" if passed else "MISMATCH",
        "mismatch_reason": mismatch_reason,
        "review_bucket": review_bucket,
    }


def discover_sessions(
    session_dir: Path,
    detect_dir: Path,
    include_json: bool,
    include_duplicates: bool,
) -> list[ReplaySession]:
    sessions: list[ReplaySession] = []
    if session_dir.exists():
        paths = sorted(session_dir.glob("*.csv"))
        if include_json:
            paths.extend(sorted(session_dir.glob("*.json")))
        seen_stems: set[str] = set()
        for path in paths:
            if not include_duplicates and path.stem in seen_stems:
                continue
            seen_stems.add(path.stem)
            sessions.append(load_session_file(path, "session_exports"))
    if detect_dir.exists():
        for path in sorted(detect_dir.glob("*.csv")):
            sessions.append(load_session_file(path, "detect_esports"))
    return sessions


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if fieldnames is None:
        fields: list[str] = []
        for row in rows:
            for key in row.keys():
                if key not in fields:
                    fields.append(key)
        fieldnames = fields
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def build_matrix(summary_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[str, list[dict[str, Any]]] = {}
    for row in summary_rows:
        groups.setdefault(str(row.get("matrix_id", "MS-UNKNOWN")), []).append(row)
    matrix_rows: list[dict[str, Any]] = []
    for matrix, rows in sorted(groups.items()):
        total = len(rows)
        passed = sum(1 for row in rows if row.get("matrix_result") == "PASS")
        mismatches = sum(1 for row in rows if row.get("matrix_result") == "MISMATCH")
        errors = sum(1 for row in rows if row.get("matrix_result") == "ERROR")
        expected_buckets = sorted({str(row.get("expected_bucket", "unknown")) for row in rows})
        matrix_rows.append({
            "matrix_id": matrix,
            "expected_buckets": "|".join(expected_buckets),
            "total": total,
            "pass": passed,
            "mismatch": mismatches,
            "error": errors,
            "pass_rate": round(passed / total, 4) if total else 0.0,
        })
    return matrix_rows


def formal_validation_exclusion_reason(row: dict[str, Any]) -> str:
    quality_bucket = str(row.get("data_quality_bucket", ""))
    if quality_bucket in FORMAL_VALIDATION_EXCLUDED_QUALITY_BUCKETS:
        return quality_bucket
    return ""


def build_formal_validation_rows(
    summary_rows: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    included: list[dict[str, Any]] = []
    excluded: list[dict[str, Any]] = []
    for row in summary_rows:
        reason = formal_validation_exclusion_reason(row)
        out = dict(row)
        out["formal_validation_scope"] = "excluded" if reason else "included"
        out["formal_validation_exclusion_reason"] = reason
        if reason:
            excluded.append(out)
        else:
            included.append(out)
    return included, excluded


def print_report(
    summary_rows: list[dict[str, Any]],
    matrix_rows: list[dict[str, Any]],
    formal_rows: list[dict[str, Any]],
    formal_matrix_rows: list[dict[str, Any]],
    formal_excluded_rows: list[dict[str, Any]],
    output_dir: Path,
) -> None:
    total = len(summary_rows)
    passed = sum(1 for row in summary_rows if row.get("matrix_result") == "PASS")
    mismatches = sum(1 for row in summary_rows if row.get("matrix_result") == "MISMATCH")
    errors = sum(1 for row in summary_rows if row.get("matrix_result") == "ERROR")
    formal_total = len(formal_rows)
    formal_passed = sum(1 for row in formal_rows if row.get("matrix_result") == "PASS")
    formal_mismatches = sum(1 for row in formal_rows if row.get("matrix_result") == "MISMATCH")
    formal_errors = sum(1 for row in formal_rows if row.get("matrix_result") == "ERROR")
    print("Motion safety shadow replay")
    print(f"- sessions: {total}")
    print(f"- pass: {passed}")
    print(f"- mismatch: {mismatches}")
    print(f"- error: {errors}")
    print(f"- formal_validation_sessions: {formal_total}")
    print(f"- formal_validation_pass: {formal_passed}")
    print(f"- formal_validation_mismatch: {formal_mismatches}")
    print(f"- formal_validation_error: {formal_errors}")
    print(f"- formal_validation_excluded: {len(formal_excluded_rows)}")
    print(f"- output_dir: {output_dir}")
    print("\nMatrix:")
    for row in matrix_rows:
        print(
            f"- {row['matrix_id']}: total={row['total']} pass={row['pass']} "
            f"mismatch={row['mismatch']} error={row['error']} pass_rate={row['pass_rate']}"
        )
    print("\nFormal validation matrix:")
    for row in formal_matrix_rows:
        print(
            f"- {row['matrix_id']}: total={row['total']} pass={row['pass']} "
            f"mismatch={row['mismatch']} error={row['error']} pass_rate={row['pass_rate']}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run motion-safety shadow detector replay on exported CSV/JSON samples.",
    )
    parser.add_argument("--session-dir", default="data/session_exports")
    parser.add_argument("--detect-dir", default="data/detect_esports")
    parser.add_argument("--output-dir", default="analysis_output/motion_safety_shadow_replay")
    parser.add_argument("--csv-only", action="store_true", help="Skip JSON files in session_exports.")
    parser.add_argument(
        "--include-duplicates",
        action="store_true",
        help="Count duplicate session_exports CSV/JSON files separately for parser debugging.",
    )
    args = parser.parse_args()

    session_dir = Path(args.session_dir)
    detect_dir = Path(args.detect_dir)
    output_dir = Path(args.output_dir)

    sessions = discover_sessions(
        session_dir,
        detect_dir,
        include_json=not args.csv_only,
        include_duplicates=args.include_duplicates,
    )
    summary_rows = [evaluate_session(session) for session in sessions]
    matrix_rows = build_matrix(summary_rows)
    mismatch_rows = [
        row for row in summary_rows
        if row.get("matrix_result") in {"MISMATCH", "ERROR"}
    ]
    formal_rows, formal_excluded_rows = build_formal_validation_rows(summary_rows)
    formal_matrix_rows = build_matrix(formal_rows)
    formal_mismatch_rows = [
        row for row in formal_rows
        if row.get("matrix_result") in {"MISMATCH", "ERROR"}
    ]

    write_csv(output_dir / "shadow_replay_summary.csv", summary_rows)
    write_csv(output_dir / "shadow_replay_matrix.csv", matrix_rows)
    write_csv(output_dir / "shadow_replay_mismatches.csv", mismatch_rows)
    write_csv(output_dir / "shadow_replay_detail.csv", summary_rows)
    write_csv(output_dir / "formal_validation_summary.csv", formal_rows)
    write_csv(output_dir / "formal_validation_matrix.csv", formal_matrix_rows)
    write_csv(output_dir / "formal_validation_mismatches.csv", formal_mismatch_rows)
    write_csv(output_dir / "formal_validation_excluded.csv", formal_excluded_rows)
    print_report(
        summary_rows,
        matrix_rows,
        formal_rows,
        formal_matrix_rows,
        formal_excluded_rows,
        output_dir,
    )


if __name__ == "__main__":
    main()
