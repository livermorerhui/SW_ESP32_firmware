#!/usr/bin/env python3
"""Verify the complete, pinned input set for an ESP32 rollback baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
PLATFORMIO_CORE_DIR = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise AssertionError(f"missing required artifact: {path}")
    return path


def run(*args: str) -> str:
    return subprocess.run(args, check=True, text=True, capture_output=True).stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=PROJECT_ROOT / ".pio" / "build" / "esp32s3")
    parser.add_argument("--output", type=Path, help="Write the verified build closure as JSON.")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    esptool = require_file(PLATFORMIO_CORE_DIR / "packages" / "tool-esptoolpy" / "esptool.py")
    partition_tool = require_file(
        PLATFORMIO_CORE_DIR / "packages" / "framework-espidf" / "components" / "partition_table" / "gen_esp32part.py"
    )
    artifacts = {
        "bootloader": require_file(build_dir / "bootloader.bin"),
        "partitions": require_file(build_dir / "partitions.bin"),
        "boot_app0": require_file(
            PLATFORMIO_CORE_DIR
            / "packages"
            / "framework-arduinoespressif32"
            / "tools"
            / "partitions"
            / "boot_app0.bin"
        ),
        "firmware": require_file(build_dir / "firmware.bin"),
    }
    sdkconfig = require_file(PROJECT_ROOT / "sdkconfig.esp32s3").read_text(encoding="utf-8")
    for expected in (
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set",
        "CONFIG_BT_BLUEDROID_ENABLED=y",
        "CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y",
    ):
        if expected not in sdkconfig:
            raise AssertionError(f"generated sdkconfig missing: {expected}")

    image_info = {}
    for name in ("bootloader", "firmware"):
        image_info[name] = run(sys.executable, str(esptool), "--chip", "esp32s3", "image_info", str(artifacts[name]))

    partition_table = run(sys.executable, str(partition_tool), str(artifacts["partitions"]))
    for expected in ("otadata,data,ota,0xe000,8K", "app0,app,ota_0,0x10000,6400K", "app1,app,ota_1,0x650000,6400K"):
        if expected not in partition_table:
            raise AssertionError(f"partition table missing expected entry: {expected}")
    if artifacts["boot_app0"].stat().st_size != 8192:
        raise AssertionError("boot_app0 must match the 8K otadata initializer partition")

    result = {
        "status": "pass",
        "build_dir": str(build_dir),
        "artifacts": {
            name: {
                "path": str(path),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
                "validation": (
                    "esptool_image_info"
                    if name in {"bootloader", "firmware"}
                    else "partition_table_parse"
                    if name == "partitions"
                    else "raw_otadata_initializer_size"
                ),
            }
            for name, path in artifacts.items()
        },
        "partition_table": partition_table.strip(),
        "image_info": {name: value.strip() for name, value in image_info.items()},
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
