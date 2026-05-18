import sys
from pathlib import Path
import numpy as np
import pandas as pd


# =========================================================
# Detection Strategy v0.3
# 目标:
# - 主判别: NORMAL vs FALL_DANGER
# - 保留深低承重路径（适合更深的危险状态）
# - 放宽“异常迁移未恢复”路径，补强 B 型：
#   外部支撑分担型危险倒伏
# =========================================================

# ===== 基础参数 =====
BASELINE_FRAMES = 10
TAIL_FRAMES = 10
MA_WINDOW = 5

# ===== 明显迁移阈值 =====
WEIGHT_DROP_THRESHOLD_KG = 12.0
DIST_DROP_THRESHOLD_MM = 1.5

# ===== 恢复 / 持续低承重阈值 =====
LOW_WEIGHT_RATIO = 0.60
RECOVERY_RATIO = 0.85
TAIL_LOW_RATIO = 0.75
LOW_WEIGHT_MIN_DURATION_FRAMES = 10

# =========================================================
# v0.3 关键调参
# v0.2:
#   delta_w >= 20
#   recovery_ratio <= 0.70
#   delta_d >= 2.5
#
# v0.3:
#   为了补强 B 型（外部支撑分担型危险倒伏）
#   放宽为:
#   delta_w >= 20
#   recovery_ratio <= 0.75
#   delta_d >= 2.0
# =========================================================
FALL_ON_CANDIDATE_DELTA_W = 20.0
FALL_ON_CANDIDATE_RECOVERY_MAX = 0.75
FALL_ON_CANDIDATE_DELTA_D = 2.0

# ===== 导数仅作记录，不做主判 =====


def moving_average(series: pd.Series, window: int) -> pd.Series:
    return series.rolling(window=window, min_periods=1).mean()


def longest_consecutive_true(mask: np.ndarray) -> int:
    longest = 0
    current = 0
    for v in mask:
        if bool(v):
            current += 1
            if current > longest:
                longest = current
        else:
            current = 0
    return longest


def safe_mean(series: pd.Series) -> float:
    s = pd.to_numeric(series, errors="coerce").dropna()
    if len(s) == 0:
        return np.nan
    return float(s.mean())


def safe_min(series: pd.Series) -> float:
    s = pd.to_numeric(series, errors="coerce").dropna()
    if len(s) == 0:
        return np.nan
    return float(s.min())


def safe_max_abs(series: pd.Series) -> float:
    s = pd.to_numeric(series, errors="coerce").dropna()
    if len(s) == 0:
        return np.nan
    return float(np.max(np.abs(s.to_numpy())))


def load_csv(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path)

    required_cols = ["distanceMm", "liveWeightKg"]
    missing = [c for c in required_cols if c not in df.columns]
    if missing:
        raise ValueError(f"{csv_path.name} 缺少列: {missing}")

    numeric_cols = ["distanceMm", "liveWeightKg", "stableWeightKg", "ddDt", "dwDt"]
    for col in numeric_cols:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")

    # 主轴必须可用
    df = df.dropna(subset=["distanceMm", "liveWeightKg"]).reset_index(drop=True)

    if len(df) < max(BASELINE_FRAMES, TAIL_FRAMES):
        raise ValueError(f"{csv_path.name} 行数过少，无法稳定分析: {len(df)}")

    return df


def detect_file(csv_path: Path) -> dict:
    df = load_csv(csv_path)

    # ===== 原始信号 =====
    raw_weight = df["liveWeightKg"]
    raw_dist = df["distanceMm"]

    # ===== MA 平滑信号 =====
    ma_weight = moving_average(raw_weight, MA_WINDOW)
    ma_dist = moving_average(raw_dist, MA_WINDOW)

    # ===== baseline =====
    baseline_weight = safe_mean(ma_weight.iloc[:BASELINE_FRAMES])
    baseline_distance = safe_mean(ma_dist.iloc[:BASELINE_FRAMES])

    if np.isnan(baseline_weight) or np.isnan(baseline_distance):
        raise ValueError(f"{csv_path.name} baseline 计算失败")

    # ===== 迁移量 =====
    min_weight = safe_min(ma_weight)
    min_dist = safe_min(ma_dist)

    delta_w = baseline_weight - min_weight
    delta_d = baseline_distance - min_dist

    # ===== 尾段恢复 =====
    tail_weight_mean = safe_mean(ma_weight.iloc[-TAIL_FRAMES:])
    tail_dist_mean = safe_mean(ma_dist.iloc[-TAIL_FRAMES:])

    if baseline_weight == 0:
        recovery_ratio = np.nan
    else:
        recovery_ratio = tail_weight_mean / baseline_weight

    # ===== 低承重持续 =====
    low_weight_threshold = baseline_weight * LOW_WEIGHT_RATIO
    low_weight_mask = (ma_weight < low_weight_threshold).to_numpy()
    longest_low_weight_run = longest_consecutive_true(low_weight_mask)

    # ===== 导数辅助（只记录） =====
    max_abs_dwdt = safe_max_abs(df["dwDt"]) if "dwDt" in df.columns else np.nan
    max_abs_dddt = safe_max_abs(df["ddDt"]) if "ddDt" in df.columns else np.nan

    # =========================================================
    # v0.3 判定逻辑
    # =========================================================

    # Rule A: 无明显迁移 -> NORMAL
    if delta_w < WEIGHT_DROP_THRESHOLD_KG and delta_d < DIST_DROP_THRESHOLD_MM:
        final_label = "NORMAL"
        reason = "no_significant_migration"

    # Rule B: 已恢复 -> NORMAL
    elif not np.isnan(recovery_ratio) and recovery_ratio >= RECOVERY_RATIO:
        final_label = "NORMAL"
        reason = "recovered_to_baseline"

    # Rule C: 持续低承重 + 尾段仍低 -> FALL_DANGER
    elif (
        longest_low_weight_run >= LOW_WEIGHT_MIN_DURATION_FRAMES
        and tail_weight_mean <= baseline_weight * TAIL_LOW_RATIO
    ):
        final_label = "FALL_DANGER"
        reason = "persistent_low_weight_no_recovery"

    # Rule D: v0.3 放宽后的“异常迁移未恢复”路径
    # 用于补强 B 型：外部支撑分担型危险倒伏
    elif (
        delta_w >= FALL_ON_CANDIDATE_DELTA_W
        and not np.isnan(recovery_ratio)
        and recovery_ratio <= FALL_ON_CANDIDATE_RECOVERY_MAX
        and delta_d >= FALL_ON_CANDIDATE_DELTA_D
    ):
        final_label = "FALL_DANGER"
        reason = "large_migration_no_recovery_v03"

    # Rule E: 其余 -> NORMAL
    else:
        final_label = "NORMAL"
        reason = "migration_but_not_confirmed_as_fall"

    return {
        "file": csv_path.name,
        "rows": len(df),

        "baseline_weight": round(float(baseline_weight), 3),
        "baseline_distance": round(float(baseline_distance), 3),

        "min_weight": round(float(min_weight), 3),
        "min_distance": round(float(min_dist), 3),

        "delta_w": round(float(delta_w), 3),
        "delta_d": round(float(delta_d), 3),

        "tail_weight_mean": round(float(tail_weight_mean), 3),
        "tail_distance_mean": round(float(tail_dist_mean), 3),
        "recovery_ratio": None if np.isnan(recovery_ratio) else round(float(recovery_ratio), 3),

        "low_weight_threshold": round(float(low_weight_threshold), 3),
        "longest_low_weight_run": int(longest_low_weight_run),

        "max_abs_dwdt": None if np.isnan(max_abs_dwdt) else round(float(max_abs_dwdt), 3),
        "max_abs_dddt": None if np.isnan(max_abs_dddt) else round(float(max_abs_dddt), 3),

        "final_label": final_label,
        "reason": reason,
    }


def main():
    if len(sys.argv) < 2:
        print("用法:")
        print("  python3 detect_v0.py <csv文件或目录>")
        sys.exit(1)

    target = Path(sys.argv[1])

    if target.is_file():
        files = [target]
    elif target.is_dir():
        files = sorted(target.glob("*.csv"))
    else:
        print(f"路径不存在: {target}")
        sys.exit(1)

    if not files:
        print("没有找到 CSV 文件")
        sys.exit(1)

    results = []
    for f in files:
        try:
            result = detect_file(f)
            results.append(result)
        except Exception as e:
            results.append({
                "file": f.name,
                "final_label": "ERROR",
                "reason": str(e),
            })

    out_df = pd.DataFrame(results)

    pd.set_option("display.max_columns", None)
    pd.set_option("display.width", 200)
    print(out_df.to_string(index=False))

    out_path = Path("detect_v0_results_v03.csv")
    out_df.to_csv(out_path, index=False, encoding="utf-8-sig")
    print(f"\n结果已写出: {out_path.resolve()}")


if __name__ == "__main__":
    main()