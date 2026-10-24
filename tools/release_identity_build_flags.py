"""Bind product identity to the actual Arduino/ESP-IDF component compilation."""

import os
from pathlib import Path

from SCons.Script import COMMAND_LINE_TARGETS, Import  # type: ignore[import-not-found]

from product_build_contract import (
    BUILD_METADATA, BUILD_PENDING, PRODUCT_ENV,
    current_product_identity, prepare_build_header, render_build_header,
    source_fingerprint, write_if_changed,
)

from release_build_contract import (
    ReleaseBuildContractError,
    validate_compatibility_environment,
    validate_compatibility_sdkconfig,
    validate_release_identity,
)


Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
platformio_env = env.subst("$PIOENV")
VERSION = os.environ.get("SW_FIRMWARE_RELEASE_VERSION", "").strip()
BUILD_ID = os.environ.get("SW_FIRMWARE_RELEASE_BUILD_ID", "").strip()

try:
    validate_compatibility_environment(os.environ)
    validate_release_identity(VERSION, BUILD_ID)
except ReleaseBuildContractError as error:
    raise ValueError(str(error)) from error

identity = current_product_identity(project_dir)
identity_mode = "explicit" if VERSION else "product" if platformio_env == PRODUCT_ENV else "legacy"
if not VERSION:
    VERSION = identity["version"]
    BUILD_ID = identity["build_id"]
    if identity_mode == "legacy":
        digest = source_fingerprint(project_dir, platformio_env)["source_sha256"]
        BUILD_ID = "legacy-" + digest[:12]

backend = {"esp32s3_unified": "UNIFIED", "esp32s3_cyd3s1612_iic": "CYD3S1612_IIC"}.get(platformio_env, "485")
prepare_build_header(build_dir, render_build_header(VERSION, BUILD_ID, backend))

# Always start from the committed configuration. An ignored local sdkconfig
# must not silently select a different bootchain for VSCode Upload.
source_sdkconfig = (project_dir / "sdkconfig.esp32s3").read_text(encoding="utf-8")
validate_compatibility_sdkconfig(source_sdkconfig)
sdkconfig = build_dir / "sdkconfig.product"
write_if_changed(sdkconfig, source_sdkconfig)
env.BoardConfig().update("build.esp-idf.sdkconfig_path", str(sdkconfig))

is_build = not COMMAND_LINE_TARGETS or any(
    name in ("buildprog", "upload", "program", "size") for name in COMMAND_LINE_TARGETS
)
if is_build:
    # A failed/cancelled attempt must not leave old evidence apparently valid,
    # even if the user later restores source to the previous fingerprint.
    write_if_changed(build_dir / BUILD_PENDING, identity_mode + "\n" + identity["source_sha256"] + "\n")
    (build_dir / BUILD_METADATA).unlink(missing_ok=True)


# The post hook adds dependencies only after PlatformIO has created the real
# application objects and target aliases. Retain this pre-build input snapshot
# so source changes during compilation cannot receive successful evidence.
env["SW_PRODUCT_BUILD_IDENTITY"] = identity
env["SW_PRODUCT_BUILD_MODE"] = identity_mode
env["SW_PRODUCT_BUILD_REQUESTED"] = is_build

print(f"[release-identity] version={VERSION} build_id={BUILD_ID} mode={identity_mode} "
      f"environment={platformio_env} backend={backend} rollback=enabled anti_rollback=disabled")
