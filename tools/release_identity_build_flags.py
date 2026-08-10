"""Inject a compatibility-release firmware identity without hardware security state."""

import os
from pathlib import Path

from SCons.Script import Import  # type: ignore[import-not-found]

from release_build_contract import (
    ReleaseBuildContractError,
    validate_compatibility_environment,
    validate_compatibility_sdkconfig,
    validate_release_identity,
)


Import("env")

VERSION = os.environ.get("SW_FIRMWARE_RELEASE_VERSION", "").strip()
BUILD_ID = os.environ.get("SW_FIRMWARE_RELEASE_BUILD_ID", "").strip()

try:
    validate_compatibility_environment(os.environ)
    validate_release_identity(VERSION, BUILD_ID)
except ReleaseBuildContractError as error:
    raise ValueError(str(error)) from error

if VERSION:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    base_sdkconfig = project_dir / "sdkconfig.esp32s3"
    if not base_sdkconfig.is_file():
        raise ValueError(f"base sdkconfig is missing: {base_sdkconfig}")
    try:
        validate_compatibility_sdkconfig(base_sdkconfig.read_text(encoding="utf-8"))
    except ReleaseBuildContractError as error:
        raise ValueError(str(error)) from error
    env.Append(
        CPPDEFINES=[
            ("FW_VER", f'\\"{VERSION}\\"'),
            ("FW_BUILD_ID", f'\\"{BUILD_ID}\\"'),
        ]
    )
    print(
        "[release-identity] injecting "
        f"FW_VER={VERSION} FW_BUILD_ID={BUILD_ID} compatibility_release=1 "
        "rollback=enabled anti_rollback=disabled"
    )
