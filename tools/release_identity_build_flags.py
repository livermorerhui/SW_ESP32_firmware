"""Inject a ledger-approved firmware identity only for release builds.

Normal developer builds deliberately keep FirmwareIdentity.h defaults.  The
release orchestrator supplies both environment variables, so source identity is
never edited as a build side effect.
"""

import hashlib
import os
import re
from pathlib import Path

from SCons.Script import Import  # type: ignore[import-not-found]


Import("env")

VERSION = os.environ.get("SW_FIRMWARE_RELEASE_VERSION", "").strip()
BUILD_ID = os.environ.get("SW_FIRMWARE_RELEASE_BUILD_ID", "").strip()
SECURITY_VERSION = os.environ.get("SW_FIRMWARE_RELEASE_SECURITY_VERSION", "").strip()
VERSION_PATTERN = re.compile(r"^SW-ESP32-\d+\.\d+\.\d+$")
BUILD_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")

if bool(VERSION) != bool(BUILD_ID):
    raise ValueError(
        "SW_FIRMWARE_RELEASE_VERSION and SW_FIRMWARE_RELEASE_BUILD_ID must be provided together"
    )

if bool(VERSION) != bool(SECURITY_VERSION):
    raise ValueError(
        "SW_FIRMWARE_RELEASE_VERSION and SW_FIRMWARE_RELEASE_SECURITY_VERSION must be provided together"
    )

if VERSION:
    if not VERSION_PATTERN.fullmatch(VERSION):
        raise ValueError(f"invalid SW release firmware version: {VERSION}")
    if not BUILD_ID_PATTERN.fullmatch(BUILD_ID):
        raise ValueError(f"invalid SW release firmware build id: {BUILD_ID}")
    if not SECURITY_VERSION.isdecimal() or not 0 <= int(SECURITY_VERSION) <= 16:
        raise ValueError("SW_FIRMWARE_RELEASE_SECURITY_VERSION must be an integer in 0..16")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    base_sdkconfig = project_dir / "sdkconfig.esp32s3"
    if not base_sdkconfig.is_file():
        raise ValueError(f"base sdkconfig is missing: {base_sdkconfig}")
    generated_dir = project_dir / ".pio" / "release-sdkconfig" / env.subst("$PIOENV")
    generated_dir.mkdir(parents=True, exist_ok=True)
    generated = generated_dir / f"secure-version-{SECURITY_VERSION}.sdkconfig"
    content = base_sdkconfig.read_text(encoding="utf-8")
    content += (
        "\n# Generated only for a ledger-approved release build.\n"
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n"
        "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y\n"
        f"CONFIG_BOOTLOADER_APP_SECURE_VERSION={SECURITY_VERSION}\n"
    )
    generated.write_text(content, encoding="utf-8")
    env.BoardConfig().update("build.esp-idf.sdkconfig_path", str(generated))
    env.Append(
        CPPDEFINES=[
            ("FW_VER", f'\\"{VERSION}\\"'),
            ("FW_BUILD_ID", f'\\"{BUILD_ID}\\"'),
        ]
    )
    print(
        "[release-identity] injecting "
        f"FW_VER={VERSION} FW_BUILD_ID={BUILD_ID} secure_version={SECURITY_VERSION} "
        f"sdkconfig={generated} sha256={hashlib.sha256(content.encode('utf-8')).hexdigest()}"
    )
