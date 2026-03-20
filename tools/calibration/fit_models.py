#!/usr/bin/env python3

import argparse
import csv
import math
import sys


def parse_bool(value):
    return str(value).strip().lower() in {"1", "true", "yes", "y"}


def load_dataset(path, distance_divisor):
    rows = []
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required = {
            "distance_mm",
            "reference_weight_kg",
            "stable_flag",
            "valid_flag",
        }
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"missing columns: {', '.join(sorted(missing))}")

        for index, row in enumerate(reader, start=1):
            try:
                distance_mm = float(row["distance_mm"])
                reference_weight_kg = float(row["reference_weight_kg"])
            except ValueError as exc:
                raise ValueError(f"row {index}: invalid numeric field") from exc

            rows.append(
                {
                    "index": int(row.get("index", index) or index),
                    "timestamp_ms": int(row.get("timestamp_ms", 0) or 0),
                    "distance_mm": distance_mm,
                    "distance_runtime": distance_mm / distance_divisor,
                    "reference_weight_kg": reference_weight_kg,
                    "stable_flag": parse_bool(row["stable_flag"]),
                    "valid_flag": parse_bool(row["valid_flag"]),
                }
            )
    return rows


def filter_dataset(rows):
    return [row for row in rows if row["stable_flag"] and row["valid_flag"]]


def gaussian_solve(matrix, vector):
    size = len(vector)
    augmented = [matrix[i][:] + [vector[i]] for i in range(size)]

    for pivot in range(size):
        best = max(range(pivot, size), key=lambda r: abs(augmented[r][pivot]))
        if abs(augmented[best][pivot]) < 1e-12:
            raise ValueError("singular matrix")
        augmented[pivot], augmented[best] = augmented[best], augmented[pivot]

        pivot_value = augmented[pivot][pivot]
        for col in range(pivot, size + 1):
            augmented[pivot][col] /= pivot_value

        for row in range(size):
            if row == pivot:
                continue
            factor = augmented[row][pivot]
            if factor == 0.0:
                continue
            for col in range(pivot, size + 1):
                augmented[row][col] -= factor * augmented[pivot][col]

    return [augmented[i][size] for i in range(size)]


def fit_linear(points):
    count = len(points)
    sx = sum(point[0] for point in points)
    sy = sum(point[1] for point in points)
    sxx = sum(point[0] * point[0] for point in points)
    sxy = sum(point[0] * point[1] for point in points)
    denom = count * sxx - sx * sx
    if abs(denom) < 1e-12:
        raise ValueError("linear fit is singular")
    slope = (count * sxy - sx * sy) / denom
    intercept = (sy - slope * sx) / count
    return [0.0, slope, intercept]


def fit_quadratic(points):
    sx = sum(point[0] for point in points)
    sx2 = sum(point[0] ** 2 for point in points)
    sx3 = sum(point[0] ** 3 for point in points)
    sx4 = sum(point[0] ** 4 for point in points)
    sy = sum(point[1] for point in points)
    sxy = sum(point[0] * point[1] for point in points)
    sx2y = sum((point[0] ** 2) * point[1] for point in points)
    count = float(len(points))

    matrix = [
        [sx4, sx3, sx2],
        [sx3, sx2, sx],
        [sx2, sx, count],
    ]
    vector = [sx2y, sxy, sy]
    return gaussian_solve(matrix, vector)


def predict(coefficients, shifted_distance):
    return (
        coefficients[0] * shifted_distance * shifted_distance
        + coefficients[1] * shifted_distance
        + coefficients[2]
    )


def calculate_metrics(rows, coefficients, reference_distance):
    if not rows:
        return None

    actual = [row["reference_weight_kg"] for row in rows]
    predicted = [
        predict(coefficients, row["distance_runtime"] - reference_distance)
        for row in rows
    ]
    errors = [predicted[i] - actual[i] for i in range(len(rows))]
    abs_errors = [abs(error) for error in errors]

    mae = sum(abs_errors) / len(abs_errors)
    rmse = math.sqrt(sum(error * error for error in errors) / len(errors))
    max_abs = max(abs_errors)

    mean_actual = sum(actual) / len(actual)
    ss_res = sum(error * error for error in errors)
    ss_tot = sum((value - mean_actual) ** 2 for value in actual)
    r_squared = 1.0 if ss_tot == 0.0 else 1.0 - (ss_res / ss_tot)

    return {
        "count": len(rows),
        "mae": mae,
        "rmse": rmse,
        "max_abs": max_abs,
        "r2": r_squared,
    }


def is_monotonic(coefficients, reference_distance, min_distance, max_distance):
    x_min = min_distance - reference_distance
    x_max = max_distance - reference_distance
    d_min = 2.0 * coefficients[0] * x_min + coefficients[1]
    d_max = 2.0 * coefficients[0] * x_max + coefficients[1]
    return d_min >= -1e-6 and d_max >= -1e-6


def choose_model(linear_result, quadratic_result, improvement_ratio):
    if not linear_result["monotonic"] and quadratic_result["monotonic"]:
        return "quadratic", "linear model rejected by monotonic check"
    if linear_result["monotonic"] and not quadratic_result["monotonic"]:
        return "linear", "quadratic model rejected by monotonic check"
    if not linear_result["monotonic"] and not quadratic_result["monotonic"]:
        raise ValueError("both linear and quadratic models failed monotonic checks")

    baseline = linear_result["target_metrics"] or linear_result["full_metrics"]
    candidate = quadratic_result["target_metrics"] or quadratic_result["full_metrics"]
    if baseline is None or candidate is None:
        raise ValueError("insufficient metrics for model comparison")

    if baseline["rmse"] <= 1e-9:
        return "linear", "linear model already has near-zero RMSE"

    improvement = (baseline["rmse"] - candidate["rmse"]) / baseline["rmse"]
    if improvement >= improvement_ratio:
        return "quadratic", (
            f"quadratic target-range RMSE improvement {improvement * 100.0:.1f}% "
            f">= {improvement_ratio * 100.0:.1f}% threshold"
        )
    return "linear", (
        f"quadratic target-range RMSE improvement {improvement * 100.0:.1f}% "
        f"< {improvement_ratio * 100.0:.1f}% threshold"
    )


def describe_metrics(label, metrics):
    if metrics is None:
        return f"{label}: no points"
    return (
        f"{label}: n={metrics['count']} "
        f"MAE={metrics['mae']:.4f} "
        f"RMSE={metrics['rmse']:.4f} "
        f"MAX={metrics['max_abs']:.4f} "
        f"R2={metrics['r2']:.6f}"
    )


def make_result(name, coefficients, reference_distance, rows, target_rows, min_distance, max_distance):
    return {
        "name": name,
        "coefficients": coefficients,
        "reference_distance": reference_distance,
        "full_metrics": calculate_metrics(rows, coefficients, reference_distance),
        "target_metrics": calculate_metrics(target_rows, coefficients, reference_distance),
        "monotonic": is_monotonic(coefficients, reference_distance, min_distance, max_distance),
    }


def main():
    parser = argparse.ArgumentParser(description="Fit linear and quadratic calibration models.")
    parser.add_argument("dataset", help="CSV dataset with distance_mm/reference_weight_kg/stable_flag/valid_flag")
    parser.add_argument("--distance-divisor", type=float, default=100.0, help="distance_mm -> firmware runtime distance divisor")
    parser.add_argument("--reference-distance", type=float, help="firmware runtime reference distance used for x = distance - reference")
    parser.add_argument("--target-min-kg", type=float, default=40.0, help="target-range minimum reference weight")
    parser.add_argument("--target-max-kg", type=float, default=120.0, help="target-range maximum reference weight")
    parser.add_argument("--valid-min-mm", type=float, default=65.0, help="minimum valid sensor distance")
    parser.add_argument("--valid-max-mm", type=float, default=135.0, help="maximum valid sensor distance")
    parser.add_argument("--quadratic-improvement-ratio", type=float, default=0.15, help="minimum RMSE improvement needed to choose quadratic")
    args = parser.parse_args()

    try:
        rows = load_dataset(args.dataset, args.distance_divisor)
        filtered = filter_dataset(rows)
        if len(filtered) < 5:
            raise ValueError("need at least 5 stable+valid calibration rows")

        reference_distance = (
            args.reference_distance
            if args.reference_distance is not None
            else sum(row["distance_runtime"] for row in filtered) / len(filtered)
        )

        fit_points = [
            (row["distance_runtime"] - reference_distance, row["reference_weight_kg"])
            for row in filtered
        ]
        target_rows = [
            row for row in filtered
            if args.target_min_kg <= row["reference_weight_kg"] <= args.target_max_kg
        ]

        min_distance = args.valid_min_mm / args.distance_divisor
        max_distance = args.valid_max_mm / args.distance_divisor

        linear = make_result(
            "linear",
            fit_linear(fit_points),
            reference_distance,
            filtered,
            target_rows,
            min_distance,
            max_distance,
        )
        quadratic = make_result(
            "quadratic",
            fit_quadratic(fit_points),
            reference_distance,
            filtered,
            target_rows,
            min_distance,
            max_distance,
        )

        selected_name, rationale = choose_model(
            linear,
            quadratic,
            args.quadratic_improvement_ratio,
        )
        selected = linear if selected_name == "linear" else quadratic
    except Exception as exc:  # pylint: disable=broad-except
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"filtered_rows={len(filtered)}")
    print(f"reference_distance={reference_distance:.6f}")
    print(describe_metrics("linear/full", linear["full_metrics"]))
    print(describe_metrics("linear/target", linear["target_metrics"]))
    print(f"linear/monotonic={int(linear['monotonic'])}")
    print(describe_metrics("quadratic/full", quadratic["full_metrics"]))
    print(describe_metrics("quadratic/target", quadratic["target_metrics"]))
    print(f"quadratic/monotonic={int(quadratic['monotonic'])}")
    print(f"selected_model={selected['name']}")
    print(f"selection_reason={rationale}")
    print(
        "deploy_command="
        f"CAL:SET_MODEL type={selected['name'].upper()},"
        f"ref={selected['reference_distance']:.6f},"
        f"c0={selected['coefficients'][0]:.9f},"
        f"c1={selected['coefficients'][1]:.9f},"
        f"c2={selected['coefficients'][2]:.9f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
