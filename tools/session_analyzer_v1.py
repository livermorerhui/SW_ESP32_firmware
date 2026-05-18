from __future__ import annotations

import argparse
import csv
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import pandas as pd


FILENAME_RE = re.compile(
    r"""^
    (?P<primary>[^_]+)_
    (?P<secondary>[^_]+)_
    (?P<timestamp>\d{8}_\d{6})_
    (?P<hz_intensity>[^_]+)
    (?:_(?P<remark>.+))?
    \.(?P<ext>csv|json)$
    """,
    re.VERBOSE,
)


@dataclass
class SessionFile:
    path: Path
    file_type: str
    filename_info: Dict[str, Any]
    meta: Dict[str, Any]
    samples: pd.DataFrame


def parse_filename(path: Path) -> Dict[str, Any]:
    match = FILENAME_RE.match(path.name)
    if not match:
        return {
            "filename_valid": False,
            "primary_label": None,
            "secondary_label": None,
            "timestamp_str": None,
            "hz_intensity": None,
            "remark": None,
            "ext": path.suffix.lower().lstrip("."),
        }

    data = match.groupdict()
    return {
        "filename_valid": True,
        "primary_label": data["primary"],
        "secondary_label": data["secondary"],
        "timestamp_str": data["timestamp"],
        "hz_intensity": data["hz_intensity"],
        "remark": data.get("remark") or "",
        "ext": data["ext"],
    }


def coerce_value(value: str) -> Any:
    text = value.strip()
    if text == "":
        return ""

    lower = text.lower()
    if lower == "true":
        return True
    if lower == "false":
        return False
    if lower == "none":
        return None

    try:
        if "." in text:
            return float(text)
        return int(text)
    except ValueError:
        return text


def read_csv_session(path: Path) -> Tuple[Dict[str, Any], pd.DataFrame]:
    header: Dict[str, Any] = {}
    data_lines: List[str] = []

    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for line in f:
            stripped = line.rstrip("\n")
            if stripped.startswith("#"):
                content = stripped[1:].strip()
                if ":" in content:
                    key, value = content.split(":", 1)
                    header[key.strip()] = coerce_value(value)
            else:
                data_lines.append(line)

    if not data_lines:
        raise ValueError(f"No tabular data found in CSV: {path}")

    from io import StringIO

    csv_buffer = StringIO("".join(data_lines))
    df = pd.read_csv(csv_buffer)

    return header, normalize_samples_df(df)


def read_json_session(path: Path) -> Tuple[Dict[str, Any], pd.DataFrame]:
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)

    meta = payload.get("meta", {})
    samples = payload.get("samples", [])
    df = pd.DataFrame(samples)

    return meta, normalize_samples_df(df)


def normalize_samples_df(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()

    rename_map = {
        "baselineReady": "baseline_ready",
        "stableWeight": "stable_weight",
        "stableWeightKg": "stable_weight",
        "liveWeightKg": "weight",
        "distanceMm": "distance",
        "distance_mm": "distance",
        "ma_3": "ma3",
        "ma_5": "ma5",
        "ma_7": "ma7",
        "abnormalDurationMs": "abnormal_duration_ms",
        "dangerDurationMs": "danger_duration_ms",
        "stopSource": "stop_source",
        "stopReason": "stop_reason",
    }
    df = df.rename(columns=rename_map)

    numeric_candidates = [
        "timestamp_ms",
        "baseline_ready",
        "stable_weight",
        "weight",
        "distance",
        "ma3",
        "ma5",
        "ma7",
        "deviation",
        "ratio",
        "abnormal_duration_ms",
        "danger_duration_ms",
    ]
    for col in numeric_candidates:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")

    for col in ["main_state", "risk_advisory", "event_aux", "stop_reason", "stop_source"]:
        if col in df.columns:
            df[col] = df[col].astype(str)

    if "event_aux" in df.columns:
        df["event_aux"] = df["event_aux"].replace({"nan": "NONE"}).fillna("NONE")

    return df


def load_session(path: Path) -> SessionFile:
    filename_info = parse_filename(path)
    ext = path.suffix.lower()

    if ext == ".csv":
        meta, samples = read_csv_session(path)
        file_type = "csv"
    elif ext == ".json":
        meta, samples = read_json_session(path)
        file_type = "json"
    else:
        raise ValueError(f"Unsupported file type: {path}")

    return SessionFile(
        path=path,
        file_type=file_type,
        filename_info=filename_info,
        meta=meta,
        samples=samples,
    )


def safe_float(value: Any) -> Optional[float]:
    try:
        if value is None or value == "":
            return None
        val = float(value)
        if math.isnan(val):
            return None
        return val
    except Exception:
        return None


def summarize_session(session: SessionFile) -> Dict[str, Any]:
    meta = session.meta
    df = session.samples
    fn = session.filename_info

    ratio_peak_from_samples = safe_float(df["ratio"].max()) if "ratio" in df.columns and not df.empty else None
    ratio_mean = safe_float(df["ratio"].mean()) if "ratio" in df.columns and not df.empty else None
    sample_count_actual = int(len(df))

    main_states = []
    if "main_state" in df.columns and not df.empty:
        main_states = [s for s in df["main_state"].dropna().astype(str).tolist() if s and s != "nan"]

    sample_last_main_state = main_states[-1] if main_states else None
    main_state_unique_count = len(set(main_states))

    abnormal_state_names = {"ABNORMAL_RECOVERABLE"}
    danger_state_names = {"DANGER", "STOPPED_BY_DANGER"}

    abnormal_count = sum(1 for s in main_states if s in abnormal_state_names)
    danger_count = sum(1 for s in main_states if s in danger_state_names)

    risk_advisory_count = 0
    if "risk_advisory" in df.columns and not df.empty:
        risk_advisory_count = int((df["risk_advisory"].astype(str).str.upper() != "NONE").sum())

    event_aux_count = 0
    if "event_aux" in df.columns and not df.empty:
        event_aux_count = int((df["event_aux"].astype(str).str.upper() != "NONE").sum())

    meta_ratio_max = safe_float(meta.get("ratio_max"))
    final_main_state = meta.get("final_main_state")
    result = meta.get("result")
    stop_reason = meta.get("stop_reason")
    stop_source = meta.get("stop_source")

    state_consistency_ok = None
    if final_main_state is not None and sample_last_main_state is not None:
        state_consistency_ok = str(final_main_state) == str(sample_last_main_state)

    summary = {
        "file_path": str(session.path),
        "file_name": session.path.name,
        "file_type": session.file_type,

        "filename_valid": fn.get("filename_valid"),
        "primary_label": fn.get("primary_label"),
        "secondary_label": fn.get("secondary_label"),
        "timestamp_str": fn.get("timestamp_str"),
        "hz_intensity": fn.get("hz_intensity"),
        "remark": fn.get("remark"),

        "test_id": meta.get("test_id"),
        "freq_hz": meta.get("freq_hz"),
        "intensity": meta.get("intensity"),
        "intensity_norm": meta.get("intensity_norm"),
        "result": result,
        "stop_reason": stop_reason,
        "stop_source": stop_source,
        "stable_weight": meta.get("stable_weight"),
        "baseline_ready_meta": meta.get("baseline_ready"),
        "duration_ms": meta.get("duration_ms"),
        "ratio_max_meta": meta_ratio_max,
        "final_main_state_meta": final_main_state,
        "final_abnormal_duration_ms": meta.get("final_abnormal_duration_ms"),
        "final_danger_duration_ms": meta.get("final_danger_duration_ms"),
        "sample_count_meta": meta.get("sample_count"),
        "fall_stop_enabled": meta.get("fall_stop_enabled"),

        "sample_count_actual": sample_count_actual,
        "ratio_peak_from_samples": ratio_peak_from_samples,
        "ratio_mean": ratio_mean,
        "sample_last_main_state": sample_last_main_state,
        "main_state_unique_count": main_state_unique_count,
        "abnormal_count": abnormal_count,
        "danger_count": danger_count,
        "risk_advisory_count": risk_advisory_count,
        "event_aux_count": event_aux_count,
        "state_consistency_ok": state_consistency_ok,
    }

    return summary


def summarize_directory(folder: Path) -> pd.DataFrame:
    session_files: List[Path] = []
    for ext in ("*.csv", "*.json"):
        session_files.extend(folder.glob(ext))

    rows: List[Dict[str, Any]] = []
    for path in sorted(session_files):
        try:
            session = load_session(path)
            rows.append(summarize_session(session))
        except Exception as exc:
            rows.append({
                "file_path": str(path),
                "file_name": path.name,
                "file_type": path.suffix.lower().lstrip("."),
                "load_error": str(exc),
            })

    return pd.DataFrame(rows)


def build_group_statistics(summary_df: pd.DataFrame) -> pd.DataFrame:
    if summary_df.empty:
        return pd.DataFrame()

    valid_df = summary_df.copy()
    if "load_error" in valid_df.columns:
        valid_df = valid_df[valid_df["load_error"].isna()]

    if valid_df.empty:
        return pd.DataFrame()

    valid_df["is_abnormal_stop"] = valid_df["result"].astype(str).eq("ABNORMAL_STOP")
    valid_df["is_auto_stop"] = valid_df["stop_reason"].astype(str).isin(
        ["FALL_SUSPECTED", "AUTO_STOP_BY_DANGER", "FAULT_STOP"]
    )

    group_cols = ["primary_label", "secondary_label", "hz_intensity"]

    agg = (
        valid_df.groupby(group_cols, dropna=False)
        .agg(
            session_count=("file_name", "count"),
            abnormal_stop_count=("is_abnormal_stop", "sum"),
            auto_stop_count=("is_auto_stop", "sum"),
            ratio_max_meta_mean=("ratio_max_meta", "mean"),
            ratio_max_meta_max=("ratio_max_meta", "max"),
            ratio_peak_mean=("ratio_peak_from_samples", "mean"),
            ratio_peak_max=("ratio_peak_from_samples", "max"),
            risk_advisory_count_mean=("risk_advisory_count", "mean"),
            danger_count_mean=("danger_count", "mean"),
            duration_ms_mean=("duration_ms", "mean"),
        )
        .reset_index()
    )

    agg["abnormal_stop_rate"] = agg["abnormal_stop_count"] / agg["session_count"]
    agg["auto_stop_rate"] = agg["auto_stop_count"] / agg["session_count"]

    return agg


def build_abnormal_sessions(summary_df: pd.DataFrame) -> pd.DataFrame:
    if summary_df.empty:
        return pd.DataFrame()

    valid_df = summary_df.copy()
    if "load_error" in valid_df.columns:
        valid_df = valid_df[valid_df["load_error"].isna()]

    mask = (
        valid_df["result"].astype(str).eq("ABNORMAL_STOP")
        | valid_df["risk_advisory_count"].fillna(0).gt(0)
        | valid_df["danger_count"].fillna(0).gt(0)
    )
    return valid_df.loc[mask].sort_values(
        by=["primary_label", "secondary_label", "timestamp_str", "file_name"],
        na_position="last"
    )


def print_brief_report(summary_df: pd.DataFrame, group_df: pd.DataFrame, abnormal_df: pd.DataFrame) -> None:
    total = len(summary_df)
    ok = len(summary_df[summary_df.get("load_error").isna()]) if "load_error" in summary_df.columns else total
    errors = total - ok

    print(f"总文件数: {total}")
    print(f"成功解析: {ok}")
    print(f"解析失败: {errors}")

    if not summary_df.empty and "result" in summary_df.columns:
        print("\n结果分布:")
        print(summary_df["result"].fillna("UNKNOWN").value_counts(dropna=False).to_string())

    if not group_df.empty:
        print("\n分组统计（前 10 行）:")
        print(group_df.head(10).to_string(index=False))

    if not abnormal_df.empty:
        print("\n异常/重点会话（前 10 行）:")
        cols = [
            "file_name",
            "primary_label",
            "secondary_label",
            "hz_intensity",
            "result",
            "stop_reason",
            "ratio_max_meta",
            "risk_advisory_count",
            "danger_count",
        ]
        existing = [c for c in cols if c in abnormal_df.columns]
        print(abnormal_df[existing].head(10).to_string(index=False))


def export_reports(output_dir: Path, summary_df: pd.DataFrame, group_df: pd.DataFrame, abnormal_df: pd.DataFrame) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    summary_path = output_dir / "session_summary.csv"
    group_path = output_dir / "group_statistics.csv"
    abnormal_path = output_dir / "abnormal_sessions.csv"

    summary_df.to_csv(summary_path, index=False, encoding="utf-8-sig")
    group_df.to_csv(group_path, index=False, encoding="utf-8-sig")
    abnormal_df.to_csv(abnormal_path, index=False, encoding="utf-8-sig")

    print(f"\n已导出:")
    print(f"- {summary_path}")
    print(f"- {group_path}")
    print(f"- {abnormal_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze exported session CSV/JSON files.")
    parser.add_argument("input_dir", type=str, help="Directory containing exported session files")
    parser.add_argument(
        "--output-dir",
        type=str,
        default="analysis_output",
        help="Directory to save analysis outputs",
    )
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)

    if not input_dir.exists() or not input_dir.is_dir():
        raise SystemExit(f"Input directory does not exist or is not a directory: {input_dir}")

    summary_df = summarize_directory(input_dir)
    group_df = build_group_statistics(summary_df)
    abnormal_df = build_abnormal_sessions(summary_df)

    print_brief_report(summary_df, group_df, abnormal_df)
    export_reports(output_dir, summary_df, group_df, abnormal_df)


if __name__ == "__main__":
    main()
