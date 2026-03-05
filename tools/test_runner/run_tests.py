#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path
from typing import Any, Dict, List

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from ble_client import BLEClient
from reporting import TestReporter
from test_cases import test_ble_protocol, test_laser_scale, test_safety_interlock, test_wave_output

SUITES = {
    "ble": test_ble_protocol,
    "wave": test_wave_output,
    "laser": test_laser_scale,
    "safety": test_safety_interlock,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="SonicWave BLE automation test runner")
    parser.add_argument(
        "--config",
        default="tools/test_runner/config/targets.yaml",
        help="Path to targets.yaml",
    )
    parser.add_argument(
        "--suite",
        default="all",
        choices=["all", "ble", "wave", "laser", "safety"],
        help="Run a specific suite or all",
    )
    return parser.parse_args()


def load_config(path: Path) -> Dict[str, Any]:
    try:
        import yaml  # type: ignore
    except Exception as exc:
        raise RuntimeError(
            "Missing dependency: PyYAML. Install with 'pip install -r tools/test_runner/requirements.txt'"
        ) from exc

    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("Invalid YAML: root must be mapping")
    return raw


async def run() -> int:
    args = parse_args()
    config_path = Path(args.config)

    reporter = TestReporter(ROOT / "reports")
    reporter.log("Starting SonicWave test runner")

    metadata: Dict[str, Any] = {
        "config_path": str(config_path),
        "suite": args.suite,
        "device_name": "unknown",
    }
    results: List[Dict[str, Any]] = []
    is_example_cfg = config_path.name.endswith("targets.example.yaml")

    if not config_path.exists():
        msg = (
            f"Config not found: {config_path}. "
            "Copy tools/test_runner/config/targets.example.yaml to targets.yaml and update values."
        )
        reporter.log(msg)
        results.append(
            {
                "id": "ENV-001",
                "suite": "env",
                "description": "Load config",
                "status": "FAIL",
                "details": msg,
                "duration_s": 0.0,
            }
        )
        reporter.write(metadata, results)
        reporter.log(f"Report generated at: {reporter.session_dir}")
        return 2

    try:
        cfg = load_config(config_path)
    except Exception as exc:
        msg = f"Failed to parse config: {exc}"
        reporter.log(msg)
        results.append(
            {
                "id": "ENV-002",
                "suite": "env",
                "description": "Parse config",
                "status": "FAIL",
                "details": msg,
                "duration_s": 0.0,
            }
        )
        reporter.write(metadata, results)
        reporter.log(f"Report generated at: {reporter.session_dir}")
        return 2

    metadata["device_name"] = cfg.get("device_name", "unknown")

    if is_example_cfg:
        msg = (
            "Example config detected. Running in dry-run mode (SKIPPED) for CI sanity check. "
            "Use targets.yaml for real hardware execution."
        )
        reporter.log(msg)
        results.append(
            {
                "id": "ENV-EXAMPLE",
                "suite": "env",
                "description": "Dry-run with example target config",
                "status": "SKIPPED",
                "details": msg,
                "duration_s": 0.0,
            }
        )
        reporter.write(metadata, results)
        reporter.log(f"Report generated at: {reporter.session_dir}")
        return 0

    client = BLEClient(cfg, logger=reporter.log)

    try:
        await client.connect()
        await client.subscribe_tx(lambda _msg: None)
    except Exception as exc:
        msg = (
            "BLE connect failed. Ensure the device is powered, advertising, and targets.yaml is correct. "
            f"Error: {exc}"
        )
        reporter.log(msg)
        results.append(
            {
                "id": "ENV-003",
                "suite": "env",
                "description": "Connect BLE target",
                "status": "FAIL",
                "details": msg,
                "duration_s": 0.0,
            }
        )
        reporter.write(metadata, results)
        reporter.log(f"Report generated at: {reporter.session_dir}")
        return 2

    selected = [args.suite] if args.suite != "all" else ["ble", "wave", "laser", "safety"]
    ctx = {"config": cfg, "reporter": reporter, "runtime": {}}

    for suite_name in selected:
        suite_mod = SUITES[suite_name]
        reporter.log(f"Running suite: {suite_name}")
        try:
            suite_results = await suite_mod.run(client, ctx)
            results.extend(suite_results)
        except Exception as exc:
            reporter.log(f"Suite {suite_name} crashed: {exc}")
            results.append(
                {
                    "id": f"{suite_name.upper()}-CRASH",
                    "suite": suite_name,
                    "description": "Suite runtime",
                    "status": "FAIL",
                    "details": f"Unhandled suite exception: {exc}",
                    "duration_s": 0.0,
                }
            )

    await client.disconnect()

    reporter.write(metadata, results)
    reporter.log(f"Report generated at: {reporter.session_dir}")

    failed = any(case.get("status") == "FAIL" for case in results)
    return 1 if failed else 0


def main() -> int:
    return asyncio.run(run())


if __name__ == "__main__":
    raise SystemExit(main())
