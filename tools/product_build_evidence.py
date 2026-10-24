"""Post-configuration SCons dependencies and always-run product evidence."""

import json
from pathlib import Path

from SCons.Script import Import  # type: ignore[import-not-found]

from product_build_contract import ASSET_NAMES, BUILD_HEADER, BUILD_METADATA, BUILD_PENDING, build_evidence


Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
identity = env.get("SW_PRODUCT_BUILD_IDENTITY")

# SCons' source scanner does not infer a dependency from the single-token
# compiler -include option. Explicit dependencies make a new identity/header
# rebuild the application, while leaving unchanged firmware incremental.
env.Depends(env.get("PIOBUILDFILES", []), env.File(str(build_dir / BUILD_HEADER)))


def complete_product_build(source, target, env):
    evidence = build_evidence(project_dir, build_dir, identity, build_dir / "sdkconfig.product")
    temporary = build_dir / (BUILD_METADATA + ".tmp")
    temporary.write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(build_dir / BUILD_METADATA)
    (build_dir / BUILD_PENDING).unlink(missing_ok=True)
    print(f"[product-build] verified {identity['version']} {identity['build_id']} "
          f"sha256={evidence['assets']['firmware.bin']['sha256']}")


if env.get("SW_PRODUCT_BUILD_REQUESTED") and env.get("SW_PRODUCT_BUILD_MODE") == "product":
    evidence_target = env.Alias(
        "product_build_evidence", [env.File(str(build_dir / name)) for name in ASSET_NAMES],
        complete_product_build,
    )
    env.AlwaysBuild(evidence_target)
    env.Depends(env.Alias("buildprog"), evidence_target)
    env.Depends(env.Alias("upload"), evidence_target)
