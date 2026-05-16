#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing file for token check: {rel}")
        return
    text = read(rel)
    for token in tokens:
        if token not in text:
            failures.append(f"{rel} missing token: {token}")


def main() -> int:
    failures: list[str] = []

    for rel in [
        "modules/product_composition/include/product_composition/target_profile.h",
        "modules/product_composition/src/target_profile.cpp",
        "modules/product_composition/tests/test_target_profile.cpp",
        "modules/product_composition/include/product_composition/target_build_binding.h",
        "modules/product_composition/src/target_build_binding.cpp",
        "modules/product_composition/tests/test_target_build_binding.cpp",
        "docs/targets/TARGET_MATRIX.md",
    ]:
        require_file(rel, failures)

    for rel in [
        "modules/product_composition/src/target_profile.cpp",
        "modules/product_composition/src/target_build_binding.cpp",
        "modules/product_composition/tests/test_target_profile.cpp",
        "modules/product_composition/tests/test_target_build_binding.cpp",
    ]:
        require_tokens(rel, ["tab5", "tdisplayp4_tft", "tdisplayp4_amoled"], failures)

    require_tokens(
        "modules/product_composition/include/product_composition/target_profile.h",
        [
            "TargetProfile",
            "TargetPlatform",
            "TargetRenderer",
            "target_id",
            "board_id",
            "build_entrypoint",
            "app_shell",
            "ux_pack_id",
            "findTargetProfile",
        ],
        failures,
    )
    require_tokens(
        "modules/product_composition/include/product_composition/target_build_binding.h",
        [
            "TargetBuildBinding",
            "sdkconfig_defaults",
            "findTargetBuildBinding",
        ],
        failures,
    )
    require_tokens(
        "docs/targets/TARGET_MATRIX.md",
        [
            "tab5",
            "tdisplayp4_tft",
            "tdisplayp4_amoled",
            "migrated_foundation",
            "tdeck",
            "tlora_pager",
            "twatch",
            "uconsole",
            "cardputerzero",
            "gat562_mesh_evb_pro",
            "pending_final_profile",
            "Final owner",
        ],
        failures,
    )

    if failures:
        for failure in failures:
            print(f"[target-architecture-foundation] {failure}")
        return 1

    print("[target-architecture-foundation] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
