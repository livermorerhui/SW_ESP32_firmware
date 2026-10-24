"""Configure the USB-only rollback baseline without release security state."""

from __future__ import annotations

import os
import re
from pathlib import Path

from SCons.Script import Import  # type: ignore[import-not-found]

from product_build_contract import prepare_build_header, render_build_header


Import("env")

VERSION = os.environ.get("SW_FIRMWARE_TEST_VERSION", "").strip()
BUILD_ID = os.environ.get("SW_FIRMWARE_TEST_BUILD_ID", "").strip()
RELEASE_VARIABLES = (
    "SW_FIRMWARE_RELEASE_VERSION",
    "SW_FIRMWARE_RELEASE_BUILD_ID",
    "SW_FIRMWARE_RELEASE_SECURITY_VERSION",
)
VERSION_PATTERN = re.compile(r"^SW-ESP32-\d+\.\d+\.\d+$")
BUILD_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


def fail(message: str) -> None:
    raise ValueError(f"rollback USB baseline: {message}")


if any(os.environ.get(name, "").strip() for name in RELEASE_VARIABLES):
    fail("release identity/security variables are forbidden in this test-only environment")
if not VERSION_PATTERN.fullmatch(VERSION):
    fail("SW_FIRMWARE_TEST_VERSION must be SW-ESP32-x.y.z")
if not BUILD_ID_PATTERN.fullmatch(BUILD_ID):
    fail("SW_FIRMWARE_TEST_BUILD_ID must be a safe build identifier")

project_dir = Path(env.subst("$PROJECT_DIR"))
source_sdkconfig = project_dir / "sdkconfig.esp32s3"
if not source_sdkconfig.is_file():
    fail("sdkconfig.esp32s3 is missing")
source_content = source_sdkconfig.read_text(encoding="utf-8")
for expected in (
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
    "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set",
):
    if expected not in source_content:
        fail(f"sdkconfig.esp32s3 is missing required test baseline setting: {expected}")

# Keep generated build configuration under .pio so a test build never rewrites
# the committed SDK configuration used as the reproducible source baseline.
generated_dir = project_dir / ".pio" / "rollback-usb-sdkconfig" / env.subst("$PIOENV")
generated_dir.mkdir(parents=True, exist_ok=True)
generated_sdkconfig = generated_dir / "rollback-usb.sdkconfig"
generated_sdkconfig.write_text(source_content, encoding="utf-8")

env.BoardConfig().update("build.esp-idf.sdkconfig_path", str(generated_sdkconfig))
# Bind the test-only USB mapping and identity through the same component
# configuration header, keeping it absent from the product header.
prepare_build_header(
    Path(env.subst("$BUILD_DIR")),
    render_build_header(
        VERSION, BUILD_ID, "485", usb_diagnostics=True,
        fail_self_check=env.subst("$PIOENV") == "esp32s3_rollback_self_check_fail",
    ),
)
print(
    "[rollback-usb-baseline] "
    f"FW_VER={VERSION} FW_BUILD_ID={BUILD_ID} usb_serial_jtag=1 "
    "rollback=enabled anti_rollback=disabled"
)
