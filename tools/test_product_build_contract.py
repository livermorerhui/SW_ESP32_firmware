#!/usr/bin/env python3
"""Regression tests for stale images, missing real flags and failed builds."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from product_build_contract import (
    ASSET_NAMES, BUILD_HEADER, BUILD_METADATA, BUILD_PENDING, PRODUCT_ENV, SOURCE_FILES,
    ProductBuildContractError, build_evidence, current_product_identity,
    prepare_build_header, render_build_header, verify_product_build,
)


class ProductBuildContractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / ".pio" / "build" / PRODUCT_ENV
        self.build.mkdir(parents=True)
        for name in SOURCE_FILES:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n")
        (self.root / "firmware_release.json").write_text(json.dumps({
            "schema": 1, "version": "SW-ESP32-1.1.3", "environment": PRODUCT_ENV,
            "board": "sonicwave_esp32s3_n16r8",
        }))
        (self.root / "src").mkdir()
        (self.root / "src" / "main.cpp").write_text("int main() {}\n")
        (self.root / "src" / "CMakeLists.txt").write_text("# component\n")
        self.mock_commit = patch("product_build_contract.subprocess.check_output", return_value="a" * 40 + "\n")
        self.mock_commit.start()
        self.addCleanup(self.mock_commit.stop)

    def built_candidate(self):
        identity = current_product_identity(self.root)
        prepare_build_header(self.build, render_build_header(identity["version"], identity["build_id"], "UNIFIED"))
        (self.build / "compile_commands.json").write_text(json.dumps([{
            "directory": str(self.build), "file": str(self.root / "src" / "main.cpp"),
            "arguments": ["c++", "-include", str(self.build / BUILD_HEADER), "-c", str(self.root / "src" / "main.cpp")],
        }]))
        sdkconfig = self.build / "sdkconfig.product"
        sdkconfig.write_text("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set\n")
        (self.build / "CMakeCache.txt").write_text(f"SDKCONFIG:UNINITIALIZED={sdkconfig}\n")
        for name in ASSET_NAMES:
            (self.build / name).write_bytes(b"fixture" + identity["version"].encode() + b"\0" + identity["build_id"].encode() + b"\0")
        metadata = build_evidence(self.root, self.build, identity, sdkconfig)
        (self.build / BUILD_METADATA).write_text(json.dumps(metadata))
        return metadata

    def test_accepts_current_product_and_all_four_unchanged_images(self):
        metadata = self.built_candidate()
        self.assertEqual(metadata, verify_product_build(self.root))
        self.assertEqual("SW-ESP32-1.1.3", metadata["version"])
        self.assertRegex(metadata["build_id"], r"^u113-[a-f0-9]{12}$")

    def test_docs_and_git_only_changes_do_not_change_content_identity(self):
        before = current_product_identity(self.root)
        (self.root / "docs").mkdir()
        (self.root / "docs" / "notes.md").write_text("new acceptance notes")
        (self.root / ".git").mkdir()
        (self.root / ".git" / "HEAD").write_text("different commit")
        self.assertEqual(before, current_product_identity(self.root))

    def test_source_config_version_and_new_untracked_source_change_identity(self):
        for name in ("src/main.cpp", "src/other.cpp", "src/CMakeLists.txt", "platformio.ini", "sdkconfig.esp32s3"):
            with self.subTest(name=name):
                before = current_product_identity(self.root)["source_sha256"]
                with (self.root / name).open("a") as stream:
                    stream.write("change\n")
                self.assertNotEqual(before, current_product_identity(self.root)["source_sha256"])

    def test_old_binary_is_rejected_after_source_edit(self):
        self.built_candidate()
        (self.root / "src" / "main.cpp").write_text("int changed() {}\n")
        with self.assertRaisesRegex(ProductBuildContractError, "stale"):
            verify_product_build(self.root)

    def test_cancelled_build_cannot_reuse_previous_success(self):
        self.built_candidate()
        (self.build / BUILD_PENDING).write_text("product\n")
        with self.assertRaisesRegex(ProductBuildContractError, "not completed"):
            verify_product_build(self.root)

    def test_tampered_assets_and_missing_assets_are_rejected(self):
        for name in ASSET_NAMES:
            with self.subTest(name=name):
                self.built_candidate()
                with (self.build / name).open("ab") as stream:
                    stream.write(b"tampered")
                with self.assertRaisesRegex(ProductBuildContractError, "assets"):
                    verify_product_build(self.root)
                (self.build / name).unlink()
                with self.assertRaises(ProductBuildContractError):
                    verify_product_build(self.root)

    def test_environment_name_does_not_replace_actual_compile_flags(self):
        self.built_candidate()
        commands = json.loads((self.build / "compile_commands.json").read_text())
        commands[0]["arguments"] = ["c++", "-c", commands[0]["file"]]
        (self.build / "compile_commands.json").write_text(json.dumps(commands))
        with self.assertRaisesRegex(ProductBuildContractError, "omitted product header"):
            verify_product_build(self.root)

    def test_single_token_force_include_and_matching_unified_flag_are_accepted(self):
        self.built_candidate()
        commands = json.loads((self.build / "compile_commands.json").read_text())
        commands[0]["arguments"] = ["c++", "-include" + str(self.build / BUILD_HEADER),
                                   "-DSW_DISTANCE_BACKEND_UNIFIED=1", "-c", commands[0]["file"]]
        (self.build / "compile_commands.json").write_text(json.dumps(commands))
        self.assertEqual("UNIFIED", verify_product_build(self.root)["compile_contract"]["backend"])

    def test_conflicting_actual_flags_are_rejected(self):
        self.built_candidate()
        commands = json.loads((self.build / "compile_commands.json").read_text())
        commands[0]["arguments"].append("-DSW_DISTANCE_BACKEND_485=1")
        (self.build / "compile_commands.json").write_text(json.dumps(commands))
        with self.assertRaisesRegex(ProductBuildContractError, "conflicting"):
            verify_product_build(self.root)

    def test_stale_cmake_source_inventory_is_rejected(self):
        (self.root / "src" / "new.cpp").write_text("int added() {}")
        with self.assertRaisesRegex(ProductBuildContractError, "source inventory"):
            self.built_candidate()

    def test_altered_generated_header_and_explicit_identity_are_rejected(self):
        metadata = self.built_candidate()
        metadata["identity_mode"] = "explicit"
        (self.build / BUILD_METADATA).write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ProductBuildContractError, "identity_mode"):
            verify_product_build(self.root)
        self.built_candidate()
        (self.build / BUILD_HEADER).write_text(render_build_header("SW-ESP32-1.1.3", "legacy", "485"))
        with self.assertRaisesRegex(ProductBuildContractError, "header"):
            verify_product_build(self.root)

    def test_actual_sdkconfig_must_be_the_recorded_product_file(self):
        self.built_candidate()
        (self.build / "CMakeCache.txt").write_text("SDKCONFIG:UNINITIALIZED=/wrong/sdkconfig\n")
        with self.assertRaisesRegex(ProductBuildContractError, "actual CMake SDKCONFIG"):
            verify_product_build(self.root)

    def test_binary_identity_is_verified_not_only_manifest_text(self):
        self.built_candidate()
        (self.build / "firmware.bin").write_bytes(b"old SW-ESP32-1.0.0\0ota-test-a\0")
        with self.assertRaisesRegex(ProductBuildContractError, "compiled firmware"):
            verify_product_build(self.root)

    def test_same_header_preserves_cmake_cache_but_change_invalidates_it(self):
        header = render_build_header("SW-ESP32-1.1.3", "test", "UNIFIED")
        prepare_build_header(self.build, header)
        (self.build / "CMakeCache.txt").write_text("cache")
        prepare_build_header(self.build, header)
        self.assertTrue((self.build / "CMakeCache.txt").is_file())
        prepare_build_header(self.build, render_build_header("SW-ESP32-1.1.3", "new", "UNIFIED"))
        self.assertFalse((self.build / "CMakeCache.txt").exists())

    def test_test_only_switches_are_explicit_and_not_in_product_header(self):
        header = render_build_header("SW-ESP32-1.1.1", "test", "485", usb_diagnostics=True, fail_self_check=True)
        self.assertIn("#define SW_OTA_ROLLBACK_TEST_INJECT_SELF_CHECK_FAILURE 1", header)
        self.assertNotIn("SELF_CHECK_FAILURE", render_build_header("SW-ESP32-1.1.3", "product", "UNIFIED"))


if __name__ == "__main__":
    unittest.main()
