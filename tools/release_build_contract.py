"""Pure contract checks for compatibility OTA release builds.

The signed manifest's security_version is an APP release-policy field. It is
not an ESP32 hardware anti-rollback instruction. Compatibility builds keep
those two concerns separate so a release cannot accidentally bind itself to an
unknown eFuse security floor.
"""

from __future__ import annotations

import re
from collections.abc import Mapping


VERSION_PATTERN = re.compile(r"^SW-ESP32-\d+\.\d+\.\d+$")
BUILD_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
HARDWARE_SECURITY_ENV = "SW_FIRMWARE_RELEASE_SECURITY_VERSION"


class ReleaseBuildContractError(ValueError):
    """Raised when a compatibility release would cross a hardware boundary."""


def validate_release_identity(version: str, build_id: str) -> None:
    if bool(version) != bool(build_id):
        raise ReleaseBuildContractError(
            "SW_FIRMWARE_RELEASE_VERSION and SW_FIRMWARE_RELEASE_BUILD_ID must be provided together"
        )
    if not version:
        return
    if not VERSION_PATTERN.fullmatch(version):
        raise ReleaseBuildContractError(f"invalid SW release firmware version: {version}")
    if not BUILD_ID_PATTERN.fullmatch(build_id):
        raise ReleaseBuildContractError(f"invalid SW release firmware build id: {build_id}")


def validate_compatibility_environment(environment: Mapping[str, str]) -> None:
    requested = str(environment.get(HARDWARE_SECURITY_ENV, "")).strip()
    if requested:
        raise ReleaseBuildContractError(
            f"{HARDWARE_SECURITY_ENV} is forbidden for compatibility releases; "
            "hardware anti-rollback requires a separately attested eFuse release profile"
        )


def kconfig_is_enabled(content: str, symbol: str) -> bool:
    return re.search(rf"^{re.escape(symbol)}=y$", content, re.MULTILINE) is not None


def validate_compatibility_sdkconfig(content: str) -> None:
    if not kconfig_is_enabled(content, "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"):
        raise ReleaseBuildContractError(
            "compatibility release requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y"
        )
    if kconfig_is_enabled(content, "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK"):
        raise ReleaseBuildContractError(
            "compatibility release forbids CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y"
        )
    secure_version = re.search(
        r"^CONFIG_BOOTLOADER_APP_SECURE_VERSION=(\d+)$", content, re.MULTILINE
    )
    if secure_version and int(secure_version.group(1)) != 0:
        raise ReleaseBuildContractError(
            "compatibility release requires the base image secure version to remain 0"
        )
