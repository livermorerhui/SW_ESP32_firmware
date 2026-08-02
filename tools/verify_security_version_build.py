#!/usr/bin/env python3
"""Verify that a release image embeds the same security version as its Kconfig input."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware-bin", required=True)
    parser.add_argument("--sdkconfig", required=True)
    parser.add_argument("--expected-security-version", required=True, type=int)
    parser.add_argument("--esptool", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 0 <= args.expected_security_version <= 16:
        raise SystemExit("expected security version must be in 0..16")
    firmware = Path(args.firmware_bin)
    sdkconfig = Path(args.sdkconfig)
    esptool = Path(args.esptool)
    if not firmware.is_file() or not sdkconfig.is_file() or not esptool.is_file():
        raise SystemExit("firmware, generated sdkconfig, or esptool is missing")

    config = sdkconfig.read_text(encoding="utf-8")
    expected = str(args.expected_security_version)
    required = {
        "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y",
        f"CONFIG_BOOTLOADER_APP_SECURE_VERSION={expected}",
    }
    missing = sorted(item for item in required if item not in config)
    if missing:
        raise SystemExit(f"generated sdkconfig lacks release anti-rollback inputs: {', '.join(missing)}")

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
    actual = int(match.group(1))
    if actual != args.expected_security_version:
        raise SystemExit(f"image secure version mismatch: expected={args.expected_security_version} actual={actual}")
    print(f"[verify-security-version-build] PASS: security_version={actual}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
