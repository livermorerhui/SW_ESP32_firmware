"""Inject a ledger-approved firmware identity only for release builds.

Normal developer builds deliberately keep FirmwareIdentity.h defaults.  The
release orchestrator supplies both environment variables, so source identity is
never edited as a build side effect.
"""

import os
import re

from SCons.Script import Import  # type: ignore[import-not-found]


Import("env")

VERSION = os.environ.get("SW_FIRMWARE_RELEASE_VERSION", "").strip()
BUILD_ID = os.environ.get("SW_FIRMWARE_RELEASE_BUILD_ID", "").strip()
VERSION_PATTERN = re.compile(r"^SW-ESP32-\d+\.\d+\.\d+$")
BUILD_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")

if bool(VERSION) != bool(BUILD_ID):
    raise ValueError(
        "SW_FIRMWARE_RELEASE_VERSION and SW_FIRMWARE_RELEASE_BUILD_ID must be provided together"
    )

if VERSION:
    if not VERSION_PATTERN.fullmatch(VERSION):
        raise ValueError(f"invalid SW release firmware version: {VERSION}")
    if not BUILD_ID_PATTERN.fullmatch(BUILD_ID):
        raise ValueError(f"invalid SW release firmware build id: {BUILD_ID}")
    env.Append(
        CPPDEFINES=[
            ("FW_VER", f'\\"{VERSION}\\"'),
            ("FW_BUILD_ID", f'\\"{BUILD_ID}\\"'),
        ]
    )
    print(f"[release-identity] injecting FW_VER={VERSION} FW_BUILD_ID={BUILD_ID}")
