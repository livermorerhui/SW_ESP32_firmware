#!/usr/bin/env python3
"""Exercise the production unified reader with fake transports on the host."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / "tools/tests/laser_reader_stubs"
TEST = ROOT / "tools/tests/laser_reader_test.cpp"
READER = ROOT / "src/modules/laser/LaserMeasurementReader.cpp"
PROFILE = ROOT / "src/modules/laser/LaserSensorProfile.cpp"


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="laser-reader-") as temporary:
        binary = Path(temporary) / "laser_reader_test"
        subprocess.run(
            [
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-private-field", "-pthread",
                "-DSW_DISTANCE_BACKEND_UNIFIED=1",
                '-DFW_VER="SW-ESP32-0.0.0"', '-DFW_BUILD_ID="host-reader-test"',
                "-I", str(STUBS), "-I", str(ROOT / "src"),
                str(TEST), str(READER), str(PROFILE), "-o", str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
