#!/usr/bin/env python3
"""Compile and run the Stage B NVS transaction tests without ESP32 hardware."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / "tools/tests/device_config_store_stubs"
TEST = ROOT / "tools/tests/device_config_store_test.cpp"
SOURCE = ROOT / "src/modules/laser/DeviceConfigStore.cpp"


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="device-config-store-") as temporary:
        binary = Path(temporary) / "device_config_store_test"
        subprocess.run(
            [
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(STUBS), "-I", str(ROOT / "src"),
                str(TEST), str(SOURCE), "-o", str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
