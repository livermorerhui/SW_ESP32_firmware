#!/usr/bin/env python3
"""Verify an ESP32 compatibility OTA image never carries anti-rollback state."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from release_build_contract import ReleaseBuildContractError, validate_compatibility_sdkconfig


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware-bin", required=True)
    parser.add_argument("--sdkconfig", required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--expected-build-id", required=True)
    parser.add_argument("--esptool", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    firmware = Path(args.firmware_bin)
    sdkconfig = Path(args.sdkconfig)
    esptool = Path(args.esptool)
    if not firmware.is_file() or not sdkconfig.is_file() or not esptool.is_file():
        raise SystemExit("firmware, sdkconfig, or esptool is missing")
    try:
        validate_compatibility_sdkconfig(sdkconfig.read_text(encoding="utf-8"))
    except ReleaseBuildContractError as error:
        raise SystemExit(str(error)) from error

    image = firmware.read_bytes()
    for value, label in ((args.expected_version, "version"), (args.expected_build_id, "build id")):
        if value.encode("utf-8") not in image:
            raise SystemExit(f"compiled firmware does not contain expected {label}: {value}")

    result = subprocess.run(
        [sys.executable, str(esptool), "image_info", "--version", "2", str(firmware)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        raise SystemExit(f"esptool image_info failed:\n{result.stdout}")
    match = re.search(r"^Secure version:\s*(\d+)\s*$", result.stdout, re.MULTILINE)
    if not match:
        raise SystemExit(f"esptool image_info did not report image secure version:\n{result.stdout}")
    if int(match.group(1)) != 0:
        raise SystemExit(
            "compatibility release image secure version must be 0; "
            f"actual={match.group(1)}"
        )
    print("[verify-compatibility-release-build] PASS: rollback=enabled anti_rollback=disabled secure_version=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
