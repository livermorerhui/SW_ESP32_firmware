#!/usr/bin/env python3
"""固件日志策略原生测试；仅使用已安装 C++ 编译器，不构建固件或下载依赖。"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ARDUINO_STUB = """#pragma once
#include <cstdint>
#include <cstddef>
using std::uint8_t; using std::uint16_t; using std::uint32_t;
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"),
                        help="已安装的 C++ 编译器路径；不会自动安装")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="sw_firmware_log_tests_") as directory:
        temp = Path(directory)
        (temp / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
        cases = [
            ("policy", "firmware_log_policy_test.cpp", []),
            ("verbose_off", "verbose_serial_gate_test.cpp", ["-DSERIAL_VERBOSE_DIAGNOSTICS=0"]),
            ("verbose_on", "verbose_serial_gate_test.cpp", ["-DSERIAL_VERBOSE_DIAGNOSTICS=1"]),
        ]
        for name, source, defines in cases:
            binary = temp / name
            command = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       "-I", str(temp), "-I", str(ROOT / "src"), *defines,
                       str(ROOT / "tools/tests" / source), "-o", str(binary)]
            print(f"编译并运行：{name}", flush=True)
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)
    print("固件日志策略测试通过：3 个程序，16 条断言；不代表固件构建或真机时序通过。")


if __name__ == "__main__":
    main()
