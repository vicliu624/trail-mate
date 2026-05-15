#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_absent(rel: str, failures: list[str]) -> None:
    if (ROOT / rel).exists():
        failures.append(f"legacy runtime file must be moved out: {rel}")


def require_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    text = read(rel)
    for token in tokens:
        if token not in text:
            failures.append(f"{rel} missing token: {token}")


def forbid_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    text = read(rel)
    for token in tokens:
        if token in text:
            failures.append(f"{rel} contains forbidden token: {token}")


def main() -> int:
    failures: list[str] = []

    required_files = [
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_menu_runtime_adapter.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_host_adapter.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_graph_bridge.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_runtime_screen_graph_presenter.h",
        "modules/ui_ascii_runtime/src/ascii_menu_runtime_adapter.cpp",
        "modules/ui_ascii_runtime/src/ascii_screen_host_adapter.cpp",
        "modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp",
        "modules/ui_ascii_runtime/src/ascii_runtime_screen_graph_presenter.cpp",
        "modules/ui_ascii_runtime/tests/ascii_runtime_screen_graph_presenter_smoke.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_menu_runtime_adapter.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_screen_host_adapter.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_presenter.h",
        "modules/ui_gtk_runtime/src/gtk_menu_runtime_adapter.cpp",
        "modules/ui_gtk_runtime/src/gtk_screen_host_adapter.cpp",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_presenter.cpp",
        "modules/ui_gtk_runtime/tests/gtk_uconsole_screen_graph_presenter_smoke.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_runtime_screen_graph_presenter.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_screen_graph_presenter.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_screen_graph_presenter.cpp",
        "docs/audits/PHASE9_RUNTIME_ADOPTION_REPORT.md",
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
    ]
    for rel in required_files:
        require_file(rel, failures)

    old_legacy_runtime_files = [
        "legacy/app_implementations/linux_sim/src/ascii_menu_runtime_adapter.h",
        "legacy/app_implementations/linux_sim/src/ascii_menu_runtime_adapter.cpp",
        "legacy/app_implementations/linux_sim/src/ascii_screen_host_adapter.h",
        "legacy/app_implementations/linux_sim/src/ascii_screen_host_adapter.cpp",
        "legacy/app_implementations/linux_sim/src/ascii_screen_graph_bridge.h",
        "legacy/app_implementations/linux_sim/src/ascii_screen_graph_bridge.cpp",
        "legacy/app_implementations/linux_uconsole/src/gtk_menu_runtime_adapter.h",
        "legacy/app_implementations/linux_uconsole/src/gtk_menu_runtime_adapter.cpp",
        "legacy/app_implementations/linux_uconsole/src/gtk_screen_host_adapter.h",
        "legacy/app_implementations/linux_uconsole/src/gtk_screen_host_adapter.cpp",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_screen_graph_bridge.h",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_screen_graph_bridge.cpp",
    ]
    for rel in old_legacy_runtime_files:
        require_absent(rel, failures)

    presenter_contract_tokens = [
        "PresentationBundle",
        "hasUxMenu",
        "hasScreenBindings",
        "load",
        "menuCount",
        "screenCount",
    ]
    for rel in [
        "modules/ui_ascii_runtime/src/ascii_runtime_screen_graph_presenter.cpp",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_presenter.cpp",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_screen_graph_presenter.cpp",
    ]:
        require_tokens(rel, presenter_contract_tokens, failures)
        forbid_tokens(
            rel,
            [
                "findUxPackById",
                "UxPackRegistry",
                "activeUxPackId",
                "lvgl.h",
                "gtk/gtk.h",
                "GtkWidget",
                "lv_obj_t",
                "apps/",
                "boards/",
                "platform/",
            ],
            failures,
        )

    require_tokens(
        "legacy/app_implementations/linux_sim/CMakeLists.txt",
        [
            "modules/ui_ascii_runtime/src/ascii_runtime_screen_graph_presenter.cpp",
            "modules/ui_ascii_runtime/tests/ascii_runtime_screen_graph_presenter_smoke.cpp",
        ],
        failures,
    )
    require_tokens(
        "legacy/app_implementations/linux_uconsole/CMakeLists.txt",
        [
            "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_presenter.cpp",
            "modules/ui_gtk_runtime/tests/gtk_uconsole_screen_graph_presenter_smoke.cpp",
        ],
        failures,
    )
    require_tokens(
        "cmake/TrailMateUxPacks.cmake",
        ["lvgl_runtime_screen_graph_presenter.cpp"],
        failures,
    )
    require_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        ["include(CTest)", "lvgl_runtime_screen_graph_presenter"],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        ["include(CTest)"],
        failures,
    )

    require_tokens(
        "docs/audits/PHASE9_RUNTIME_ADOPTION_REPORT.md",
        [
            "Phase 9 Rule",
            "modules/ui_ascii_runtime",
            "modules/ui_gtk_runtime",
            "LvglRuntimeScreenGraphPresenter",
            "must not be added to `legacy/`",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        [
            "moved out of legacy",
            "LegacyChatDeliveryActionBridge",
            "LegacyChatDeliveryEventBridge",
            "Compilation",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/LEGACY_BURNDOWN_REGISTER.md",
        [
            "modules/ui_ascii_runtime",
            "modules/ui_gtk_runtime",
            "moved out of legacy",
        ],
        failures,
    )

    if failures:
        for failure in failures:
            print(f"[phase9-runtime-adoption] {failure}")
        return 1

    print("[phase9-runtime-adoption] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
