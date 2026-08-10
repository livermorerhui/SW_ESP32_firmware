#!/usr/bin/env python3
"""Focused host tests for the compatibility release-build boundary."""

from __future__ import annotations

import unittest

from release_build_contract import (
    HARDWARE_SECURITY_ENV,
    ReleaseBuildContractError,
    validate_compatibility_environment,
    validate_compatibility_sdkconfig,
    validate_release_identity,
)


COMPATIBILITY_SDKCONFIG = """\
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set
"""


class ReleaseBuildContractTests(unittest.TestCase):
    def test_accepts_identity_with_compatibility_kconfig(self) -> None:
        validate_release_identity("SW-ESP32-1.1.0", "identity-scan-r2")
        validate_compatibility_environment({})
        validate_compatibility_sdkconfig(COMPATIBILITY_SDKCONFIG)

    def test_rejects_partial_or_invalid_identity(self) -> None:
        with self.assertRaises(ReleaseBuildContractError):
            validate_release_identity("SW-ESP32-1.1.0", "")
        with self.assertRaises(ReleaseBuildContractError):
            validate_release_identity("1.1.0", "identity-scan-r2")

    def test_rejects_hardware_security_environment(self) -> None:
        with self.assertRaisesRegex(ReleaseBuildContractError, HARDWARE_SECURITY_ENV):
            validate_compatibility_environment({HARDWARE_SECURITY_ENV: "1"})

    def test_rejects_anti_rollback_or_nonzero_image_security_version(self) -> None:
        with self.assertRaisesRegex(ReleaseBuildContractError, "ANTI_ROLLBACK"):
            validate_compatibility_sdkconfig(
                COMPATIBILITY_SDKCONFIG.replace(
                    "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set",
                    "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y",
                )
            )
        with self.assertRaisesRegex(ReleaseBuildContractError, "secure version"):
            validate_compatibility_sdkconfig(
                COMPATIBILITY_SDKCONFIG + "CONFIG_BOOTLOADER_APP_SECURE_VERSION=1\n"
            )


if __name__ == "__main__":
    unittest.main()
