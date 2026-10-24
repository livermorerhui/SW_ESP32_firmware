#!/usr/bin/env python3
"""Fail fast when the reproducible ESP32 rollback build contract drifts."""

from __future__ import annotations

from pathlib import Path

from product_build_contract import render_build_header


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def require(text: str, expected: str, source: Path) -> None:
    if expected not in text:
        raise AssertionError(f"{source.relative_to(PROJECT_ROOT)} missing: {expected}")


def main() -> None:
    platformio = (PROJECT_ROOT / "platformio.ini").read_text(encoding="utf-8")
    sdkconfig = (PROJECT_ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    root_cmake = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    app_cmake = (PROJECT_ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
    main_cpp = (PROJECT_ROOT / "src" / "main.cpp").read_text(encoding="utf-8")

    for expected in (
        "platform = espressif32@6.13.0",
        "framework = arduino, espidf",
        "platformio/framework-arduinoespressif32@3.20017.241212+sha.dcc1105b",
        "platformio/framework-espidf@3.40407.240606",
        "board_build.partitions = partitions/sonicwave_esp32s3_n16r8_ota_16mb.csv",
        "-Wno-error=uninitialized",
    ):
        require(platformio, expected, PROJECT_ROOT / "platformio.ini")

    require(platformio, "[env:esp32s3_rollback_self_check_fail]", PROJECT_ROOT / "platformio.ini")
    require(
        platformio,
        "-DSW_OTA_ROLLBACK_TEST_INJECT_SELF_CHECK_FAILURE=1",
        PROJECT_ROOT / "platformio.ini",
    )
    require(platformio, "[env:esp32s3_rollback_usb_baseline]", PROJECT_ROOT / "platformio.ini")
    require(
        platformio,
        "pre:tools/test_bootchain_baseline_build_flags.py",
        PROJECT_ROOT / "platformio.ini",
    )

    baseline_script = (PROJECT_ROOT / "tools" / "test_bootchain_baseline_build_flags.py").read_text(encoding="utf-8")
    for expected in (
        "SW_FIRMWARE_TEST_VERSION",
        "SW_FIRMWARE_TEST_BUILD_ID",
        "SW_FIRMWARE_RELEASE_SECURITY_VERSION",
        "prepare_build_header(",
        "usb_diagnostics=True",
        'fail_self_check=env.subst("$PIOENV") == "esp32s3_rollback_self_check_fail"',
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set",
    ):
        require(baseline_script, expected, PROJECT_ROOT / "tools" / "test_bootchain_baseline_build_flags.py")

    require(app_cmake, "product_build_config.h", PROJECT_ROOT / "src" / "CMakeLists.txt")
    require(app_cmake, '"-include${sonicwave_build_header}"', PROJECT_ROOT / "src" / "CMakeLists.txt")
    baseline_header = render_build_header("SW-ESP32-1.1.1", "rollback-audit", "485", usb_diagnostics=True)
    failed_header = render_build_header("SW-ESP32-1.1.1", "rollback-audit", "485", usb_diagnostics=True,
                                        fail_self_check=True)
    product_header = render_build_header("SW-ESP32-1.1.3", "product-audit", "UNIFIED")
    for expected in (
        "#define SW_ESP32_ROLLBACK_USB_DIAGNOSTICS 1",
        "#define ARDUINO_USB_MODE 1",
        "#define ARDUINO_USB_CDC_ON_BOOT 1",
    ):
        require(baseline_header, expected, PROJECT_ROOT / "tools" / "product_build_contract.py")
        if expected in product_header:
            raise AssertionError("product build must not inherit rollback USB test switches")
    injection = "#define SW_OTA_ROLLBACK_TEST_INJECT_SELF_CHECK_FAILURE 1"
    require(failed_header, injection, PROJECT_ROOT / "tools" / "product_build_contract.py")
    if injection in baseline_header or injection in product_header:
        raise AssertionError("self-check failure injection must stay in its explicit test profile")

    require(main_cpp, "#if defined(SW_ESP32_ROLLBACK_USB_DIAGNOSTICS)", PROJECT_ROOT / "src" / "main.cpp")
    require(main_cpp, "FirmwareDiagnostics::bootDiagnostic(", PROJECT_ROOT / "src" / "main.cpp")
    diagnostics_source = PROJECT_ROOT / "src" / "core" / "FirmwareDiagnostics.cpp"
    require(diagnostics_source.read_text(encoding="utf-8"), "event.kind = EventKind::BootDiagnostic; event.critical = true;",
            diagnostics_source)

    for expected in (
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        "# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set",
        "CONFIG_AUTOSTART_ARDUINO=y",
        "CONFIG_ARDUINO_RUN_CORE1=y",
        "CONFIG_FREERTOS_HZ=1000",
        "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y",
        "CONFIG_ESPTOOLPY_FLASHFREQ_80M=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
        "CONFIG_SPIRAM_MODE_OCT=y",
        "CONFIG_SPIRAM_SPEED_80M=y",
        "CONFIG_BT_ENABLED=y",
        "CONFIG_BT_BLUEDROID_ENABLED=y",
        "CONFIG_BT_BLE_ENABLED=y",
        "CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y",
        "CONFIG_BT_GATTS_ENABLE=y",
    ):
        require(sdkconfig, expected, PROJECT_ROOT / "sdkconfig.defaults")

    require(root_cmake, "include($ENV{IDF_PATH}/tools/cmake/project.cmake)", PROJECT_ROOT / "CMakeLists.txt")
    require(app_cmake, "idf_component_register(", PROJECT_ROOT / "src/CMakeLists.txt")
    require(app_cmake, "REQUIRES framework-arduinoespressif32", PROJECT_ROOT / "src/CMakeLists.txt")

    print("PASS: rollback build configuration is pinned and complete")


if __name__ == "__main__":
    main()
