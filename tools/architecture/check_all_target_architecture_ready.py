#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

TARGETS = [
    "tab5",
    "tdisplayp4_tft",
    "tdisplayp4_amoled",
    "tlora_pager",
    "tdeck",
    "twatch",
    "uconsole",
    "cardputerzero",
    "gat562_mesh_evb_pro",
]

BOARD_DIRS = [
    "boards/tab5",
    "boards/tdisplayp4",
    "boards/tlora_pager",
    "boards/tdeck",
    "boards/twatch",
    "boards/uconsole",
    "boards/cardputerzero",
    "boards/gat562_mesh_evb_pro",
]

UI_PROFILES = [
    "tab5_touch_ui",
    "tdisplayp4_touch_ui",
    "pager_compact_ui",
    "deck_wide_ui",
    "watch_compact_ui",
    "uconsole_desktop_ui",
    "cardputer_compact_ui",
    "node_headless_ui",
]

PAGE_MANIFESTS = [
    "tab5_touch_manifest",
    "tdisplayp4_touch_manifest",
    "pager_compact_manifest",
    "deck_full_manifest",
    "watch_compact_manifest",
    "uconsole_desktop_manifest",
    "cardputer_compact_manifest",
    "node_headless_manifest",
]

LAYOUT_PROFILES = [
    "tab5_large_touch",
    "tdisplayp4_touch",
    "pager_compact",
    "deck_wide",
    "watch_compact",
    "uconsole_desktop",
    "cardputer_compact",
    "headless_node",
]


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

    required_files = [
        "modules/product_composition/include/product_composition/target_profile.h",
        "modules/product_composition/src/target_profile.cpp",
        "modules/product_composition/tests/test_target_profile.cpp",
        "modules/product_composition/include/product_composition/target_build_binding.h",
        "modules/product_composition/src/target_build_binding.cpp",
        "modules/product_composition/tests/test_target_build_binding.cpp",
        "modules/product_composition/include/product_composition/target_ux_binding.h",
        "modules/product_composition/src/target_ux_binding.cpp",
        "modules/product_composition/tests/test_target_ux_binding.cpp",
        "modules/ui_presentation/include/ui_presentation/target_ui_profile.h",
        "modules/ui_presentation/src/target_ui_profile.cpp",
        "modules/ui_presentation/tests/test_target_ui_profile.cpp",
        "modules/ui_presentation/include/ui_presentation/page/page_manifest.h",
        "modules/ui_presentation/src/page/page_manifest.cpp",
        "modules/ui_presentation/tests/test_page_manifest.cpp",
        "modules/ui_presentation/include/ui_presentation/layout/layout_profile.h",
        "modules/ui_presentation/src/layout/layout_profile.cpp",
        "modules/ui_presentation/tests/test_layout_profile.cpp",
        "docs/targets/TARGET_MATRIX.md",
        "docs/audits/TARGET_UI_FINAL_OWNERSHIP_REPORT.md",
        "docs/audits/TARGET_UX_PACK_GAP_REPORT.md",
    ]
    for rel in required_files:
        require_file(rel, failures)

    for rel in [
        "modules/product_composition/src/target_profile.cpp",
        "modules/product_composition/src/target_build_binding.cpp",
        "modules/product_composition/src/target_ux_binding.cpp",
        "docs/targets/TARGET_MATRIX.md",
    ]:
        require_tokens(rel, TARGETS, failures)

    require_tokens(
        "modules/ui_presentation/src/target_ui_profile.cpp",
        UI_PROFILES,
        failures,
    )
    require_tokens(
        "modules/ui_presentation/src/page/page_manifest.cpp",
        PAGE_MANIFESTS,
        failures,
    )
    require_tokens(
        "modules/ui_presentation/src/layout/layout_profile.cpp",
        LAYOUT_PROFILES,
        failures,
    )

    for board_dir in BOARD_DIRS:
        require_file(f"{board_dir}/BOARD.md", failures)
        require_file(f"{board_dir}/board_facts.h", failures)

    require_tokens(
        "docs/audits/TARGET_UI_FINAL_OWNERSHIP_REPORT.md",
        [
            "No intermediate UI layer introduced.",
            "No transitional UI adapter introduced.",
            "No legacy UI extraction layer introduced.",
            "TargetProfile",
            "TargetBuildBinding",
            "TargetUxBinding",
            "PageManifest",
            "LayoutProfile",
            "root",
        ],
        failures,
    )

    for rel in [
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.h",
        "apps/linux_sim_shell/src/linux_sim_app_shell.h",
        "apps/nrf52_node/src/nrf52_node_app_shell.h",
    ]:
        require_tokens(
            rel,
            ["targetId", "targetProfile", "activeUxPackId", "TargetProfile"],
            failures,
        )

    if failures:
        for failure in failures:
            print(f"[all-target-architecture] {failure}")
        return 1

    print("[all-target-architecture] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
