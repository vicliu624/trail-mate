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


def forbid_tokens(rel: str, tokens: list[str], failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        return
    text = read(rel)
    for token in tokens:
        if token in text:
            failures.append(f"{rel} contains forbidden token: {token}")


def check_renderer_file(
    rel: str,
    required: list[str],
    forbidden: list[str],
    failures: list[str],
) -> None:
    require_tokens(rel, required, failures)
    forbid_tokens(rel, forbidden, failures)


def main() -> int:
    failures: list[str] = []

    required_files = [
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_descriptor_renderer.h",
        "modules/ui_ascii_runtime/src/ascii_descriptor_renderer.cpp",
        "modules/ui_ascii_runtime/tests/test_ascii_descriptor_renderer.cpp",
        "apps/linux_sim_shell/src/linux_sim_runtime_renderer.h",
        "apps/linux_sim_shell/src/linux_sim_runtime_renderer.cpp",
        "apps/linux_sim_shell/tests/linux_sim_runtime_renderer_smoke.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_descriptor_page_registry.h",
        "modules/ui_gtk_runtime/src/gtk_descriptor_page_registry.cpp",
        "modules/ui_gtk_runtime/tests/test_gtk_descriptor_page_registry.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_renderer.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_renderer.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_renderer_smoke.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_descriptor_menu_model.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_menu_model.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_descriptor_menu_model.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_descriptor_renderer_probe.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_renderer_probe.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_descriptor_renderer_probe.cpp",
        "docs/audits/PHASE11_RENDERER_DESCRIPTOR_CONSUMPTION_REPORT.md",
    ]
    for rel in required_files:
        require_file(rel, failures)

    renderer_forbidden = [
        "UxPackRegistry",
        "findUxPackById",
        "buildMenuForUxPack",
        "activeUxPackId",
        "ui::menu::MenuModel menu",
        "GtkWidget",
        "gtk/gtk.h",
        "lv_obj_t",
        "lvgl.h",
        "BOARD_",
    ]

    check_renderer_file(
        "modules/ui_ascii_runtime/src/ascii_descriptor_renderer.cpp",
        [
            "AsciiDescriptorRenderer",
            "AsciiRenderLine",
            "AsciiRuntimeEntryAdoption",
            "adoption.presenter()",
            "presenter.menuLines()",
            "presenter.screens()",
            "lines()",
        ],
        renderer_forbidden,
        failures,
    )
    check_renderer_file(
        "apps/linux_sim_shell/src/linux_sim_runtime_renderer.cpp",
        [
            "LinuxSimRuntimeRenderer",
            "LinuxSimRuntimeEntry",
            "entry.usingPrimaryScreenGraph()",
            "entry.adoption()",
            "entry.fallbackUsed()",
            "usedPrimaryScreenGraph",
            "usedFallback",
            "renderFallback",
        ],
        renderer_forbidden + ["ui::menu::MenuModel"],
        failures,
    )
    require_tokens(
        "apps/linux_sim_shell/tests/linux_sim_runtime_renderer_smoke.cpp",
        [
            "LinuxSimRuntimeRenderer",
            "LinuxSimRuntimeEntry",
            "usingPrimaryScreenGraph",
            "usedPrimaryScreenGraph",
            "fallbackUsed",
            "usedFallback",
        ],
        failures,
    )

    check_renderer_file(
        "modules/ui_gtk_runtime/src/gtk_descriptor_page_registry.cpp",
        [
            "GtkDescriptorPageRegistry",
            "GtkRuntimeEntryAdoption",
            "adoption.presenter()",
            "presenter.menuDescriptors()",
            "presenter.screenDescriptors()",
        ],
        renderer_forbidden,
        failures,
    )
    check_renderer_file(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_renderer.cpp",
        [
            "LinuxUConsoleGtkPageRegistryRenderer",
            "LinuxUConsoleGtkPageRegistryAdoption",
            "adoption.usingPrimaryScreenGraph()",
            "adoption.adoption()",
            "adoption.fallbackUsed()",
            "usedPrimaryScreenGraph",
            "usedFallback",
            "renderFallback",
        ],
        renderer_forbidden + ["ui::menu::MenuModel"],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_renderer_smoke.cpp",
        [
            "LinuxUConsoleGtkPageRegistryRenderer",
            "LinuxUConsoleGtkPageRegistryAdoption",
            "usingPrimaryScreenGraph",
            "usedPrimaryScreenGraph",
            "fallbackUsed",
            "usedFallback",
        ],
        failures,
    )

    check_renderer_file(
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_menu_model.cpp",
        [
            "LvglDescriptorMenuModel",
            "LvglPrimaryScreenGraphRuntime",
            "runtime.usingPrimaryScreenGraph()",
            "runtime.adoption().presenter()",
            "presenter.menuEntries()",
            "presenter.screenEntries()",
        ],
        renderer_forbidden,
        failures,
    )
    check_renderer_file(
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_renderer_probe.cpp",
        [
            "LvglDescriptorRendererProbe",
            "LvglPrimaryScreenGraphRuntime",
            "runtime.usingPrimaryScreenGraph()",
            "runtime.fallbackUsed()",
            "usedPrimaryScreenGraph",
            "usedFallback",
            "render",
            "loadFallback",
        ],
        renderer_forbidden,
        failures,
    )
    require_tokens(
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_descriptor_renderer_probe.cpp",
        [
            "LvglDescriptorRendererProbe",
            "LvglPrimaryScreenGraphRuntime",
            "usingPrimaryScreenGraph",
            "usedPrimaryScreenGraph",
            "fallbackUsed",
            "usedFallback",
        ],
        failures,
    )

    require_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "linux_sim_runtime_renderer.cpp",
            "linux_sim_runtime_renderer_smoke.cpp",
            "ascii_descriptor_renderer.cpp",
            "test_ascii_descriptor_renderer.cpp",
            "lvgl_descriptor_menu_model",
            "lvgl_descriptor_renderer_probe",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        [
            "linux_uconsole_gtk_page_registry_renderer.cpp",
            "linux_uconsole_gtk_page_registry_renderer_smoke.cpp",
            "gtk_descriptor_page_registry.cpp",
            "test_gtk_descriptor_page_registry.cpp",
        ],
        failures,
    )
    require_tokens(
        "cmake/TrailMateUxPacks.cmake",
        [
            "lvgl_descriptor_menu_model.cpp",
            "lvgl_descriptor_renderer_probe.cpp",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE11_RENDERER_DESCRIPTOR_CONSUMPTION_REPORT.md",
        [
            "LinuxSim",
            "renderer descriptor path",
            "GTK",
            "Page registry descriptor path",
            "LVGL",
            "Descriptor renderer path",
            "fallback",
            "descriptor source",
            "fallback status",
            "Not done",
            "Phase 12 Recommendation",
            "Phase 11 does not rewrite real GTK widgets",
            "Phase 11 does not create LVGL widgets",
            "Phase 11 does not delete fallback",
            "UxPackRegistry",
            "buildMenuForUxPack",
            "GtkWidget",
            "lv_obj_t",
        ],
        failures,
    )

    if failures:
        for failure in failures:
            print(f"[phase11-renderer-consumption] {failure}")
        return 1

    print("[phase11-renderer-consumption] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
