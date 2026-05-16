#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def check_post_root_legacy_removed() -> int:
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/architecture/check_no_root_legacy_ready.py")],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        print(f"[phase9-runtime-adoption] {output}")
        return 1
    print("[phase9-runtime-adoption] OK (root legacy removed)")
    return 0


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required file: {rel}")


def require_absent(rel: str, failures: list[str]) -> None:
    if (ROOT / rel).exists():
        failures.append(f"legacy runtime adoption must be burned down, not maintained: {rel}")


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


def forbid_legacy_entry_adoption_tokens(failures: list[str]) -> None:
    legacy_root = ROOT / "legacy" / "app_implementations"
    if not legacy_root.exists():
        return

    forbidden = [
        "AsciiRuntimeEntryAdoption",
        "GtkRuntimeEntryAdoption",
        "LvglRuntimeEntryAdoption",
        "ascii_runtime_entry_adoption",
        "gtk_runtime_entry_adoption",
        "lvgl_runtime_entry_adoption",
    ]
    for path in legacy_root.rglob("*"):
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for token in forbidden:
            if token in text:
                rel = path.relative_to(ROOT).as_posix()
                failures.append(
                    f"{rel} contains forbidden Phase 9 runtime adoption token: {token}"
                )


def main() -> int:
    if not (ROOT / "legacy").exists():
        return check_post_root_legacy_removed()

    failures: list[str] = []

    required_files = [
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_menu_runtime_adapter.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_host_adapter.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_graph_bridge.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_runtime_screen_graph_presenter.h",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_runtime_entry_adoption.h",
        "modules/ui_ascii_runtime/src/ascii_menu_runtime_adapter.cpp",
        "modules/ui_ascii_runtime/src/ascii_screen_host_adapter.cpp",
        "modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp",
        "modules/ui_ascii_runtime/src/ascii_runtime_screen_graph_presenter.cpp",
        "modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp",
        "modules/ui_ascii_runtime/tests/ascii_runtime_screen_graph_presenter_smoke.cpp",
        "modules/ui_ascii_runtime/tests/ascii_runtime_entry_adoption_smoke.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_menu_runtime_adapter.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_screen_host_adapter.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_presenter.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_runtime_entry_adoption.h",
        "modules/ui_gtk_runtime/src/gtk_menu_runtime_adapter.cpp",
        "modules/ui_gtk_runtime/src/gtk_screen_host_adapter.cpp",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_presenter.cpp",
        "modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp",
        "modules/ui_gtk_runtime/tests/gtk_uconsole_screen_graph_presenter_smoke.cpp",
        "modules/ui_gtk_runtime/tests/gtk_runtime_entry_adoption_smoke.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_runtime_screen_graph_presenter.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_runtime_entry_adoption.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_runtime_adoption_probe.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_screen_graph_presenter.cpp",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_entry_adoption.cpp",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_adoption_probe.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_screen_graph_presenter.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_entry_adoption.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_adoption_probe.cpp",
        "apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.h",
        "apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.cpp",
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.h",
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.cpp",
        "apps/linux_sim_shell/tests/linux_sim_runtime_entry_adoption_probe_smoke.cpp",
        "apps/linux_sim_shell/tests/linux_sim_runtime_entry_smoke.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_runtime_entry_adoption_probe.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_runtime_entry_adoption_probe.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_runtime_entry_adoption_probe_smoke.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_adoption_smoke.cpp",
        "docs/audits/PHASE9_RUNTIME_ADOPTION_REPORT.md",
        "docs/audits/PHASE9_RUNTIME_ENTRY_ADOPTION_REPORT.md",
        "docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md",
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
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
        "legacy/app_implementations/linux_sim/src/ascii_runtime_entry_adoption.h",
        "legacy/app_implementations/linux_sim/src/ascii_runtime_entry_adoption.cpp",
        "legacy/app_implementations/linux_sim/tests/ascii_runtime_entry_adoption_smoke.cpp",
        "legacy/app_implementations/linux_sim/src/linux_sim_runtime_adoption_bridge.h",
        "legacy/app_implementations/linux_sim/src/linux_sim_runtime_adoption_bridge.cpp",
        "legacy/app_implementations/linux_sim/tests/linux_sim_runtime_adoption_bridge_smoke.cpp",
        "legacy/app_implementations/linux_uconsole/src/gtk_menu_runtime_adapter.h",
        "legacy/app_implementations/linux_uconsole/src/gtk_menu_runtime_adapter.cpp",
        "legacy/app_implementations/linux_uconsole/src/gtk_screen_host_adapter.h",
        "legacy/app_implementations/linux_uconsole/src/gtk_screen_host_adapter.cpp",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_screen_graph_bridge.h",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_screen_graph_bridge.cpp",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_runtime_entry_adoption.h",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_runtime_entry_adoption.cpp",
        "legacy/app_implementations/linux_uconsole/tests/gtk_runtime_entry_adoption_smoke.cpp",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_phase9_screen_graph_bridge.h",
        "legacy/app_implementations/linux_uconsole/src/platform/gtk/gtk_uconsole_phase9_screen_graph_bridge.cpp",
        "legacy/app_implementations/linux_uconsole/tests/gtk_uconsole_phase9_screen_graph_bridge_smoke.cpp",
    ]
    for rel in old_legacy_runtime_files:
        require_absent(rel, failures)

    forbid_legacy_entry_adoption_tokens(failures)

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

    entry_adoption_contracts = {
        "modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp": [
            "AsciiRuntimeEntryAdoption",
            "AsciiRuntimeScreenGraphPresenter",
            "PresentationBundle",
            "ready_",
        ],
        "modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp": [
            "GtkRuntimeEntryAdoption",
            "GtkUConsoleScreenGraphPresenter",
            "PresentationBundle",
            "ready_",
        ],
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_entry_adoption.cpp": [
            "LvglRuntimeEntryAdoption",
            "LvglRuntimeScreenGraphPresenter",
            "PresentationBundle",
            "ready_",
        ],
    }
    for rel, tokens in entry_adoption_contracts.items():
        require_tokens(rel, tokens, failures)
        forbid_tokens(
            rel,
            [
                "findUxPackById",
                "UxPackRegistry",
                "activeUxPackId",
                "MenuModel menu",
                "ui::menu::MenuModel",
                "buildMenuForUxPack",
                "gtk/gtk.h",
                "GtkWidget",
                "lvgl.h",
                "lv_obj_t",
                "apps/",
                "boards/",
                "platform/",
            ],
            failures,
        )

    app_shell_probe_contracts = {
        "apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.cpp": [
            "LinuxSimRuntimeEntryAdoptionProbe",
            "AsciiRuntimeEntryAdoption",
            "activeUxPackId",
            "buildMenuForUxPack",
            "CompatibilityScreenFactory",
            "fallback_",
        ],
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_runtime_entry_adoption_probe.cpp": [
            "LinuxUConsoleGtkRuntimeEntryAdoptionProbe",
            "GtkRuntimeEntryAdoption",
            "activeUxPackId",
            "buildMenuForUxPack",
            "CompatibilityScreenFactory",
            "fallback_",
        ],
    }
    for rel, tokens in app_shell_probe_contracts.items():
        require_tokens(rel, tokens, failures)
        forbid_tokens(
            rel,
            [
                "findUxPackById",
                "UxPackRegistry",
                "gtk/gtk.h",
                "GtkWidget",
                "lvgl.h",
                "lv_obj_t",
                "legacy/app_implementations",
                "boards/",
                "platform/",
            ],
            failures,
        )

    real_entry_contracts = {
        "apps/linux_sim_shell/src/linux_sim_runtime_entry.cpp": [
            "LinuxSimRuntimeEntry",
            "AsciiRuntimeEntryAdoption",
            "fallback",
        ],
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_adoption.cpp": [
            "LinuxUConsoleGtkPageRegistryAdoption",
            "GtkRuntimeEntryAdoption",
            "fallback",
        ],
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_adoption_probe.cpp": [
            "LvglRuntimeAdoptionProbe",
            "LvglRuntimeEntryAdoption",
            "fallback",
        ],
    }
    for rel, tokens in real_entry_contracts.items():
        require_tokens(rel, tokens, failures)
        forbid_tokens(
            rel,
            [
                "findUxPackById",
                "UxPackRegistry",
                "buildMenuForUxPack",
                "MenuModel menu",
                "ui::menu::MenuModel",
                "gtk/gtk.h",
                "GtkWidget",
                "lvgl.h",
                "lv_obj_t",
                "BOARD_",
                "legacy/app_implementations",
                "boards/",
                "platform/",
            ],
            failures,
        )

    require_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "linux_sim_runtime_entry.cpp",
            "linux_sim_runtime_entry_smoke.cpp",
            "modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp",
            "linux_sim_runtime_entry_adoption_probe.cpp",
            "linux_sim_runtime_entry_adoption_probe_smoke.cpp",
            "ascii_runtime_entry_adoption_smoke.cpp",
            "lvgl_runtime_adoption_probe",
            "lvgl_runtime_entry_adoption",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        [
            "linux_uconsole_gtk_page_registry_adoption.cpp",
            "linux_uconsole_gtk_page_registry_adoption_smoke.cpp",
            "modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp",
            "linux_uconsole_gtk_runtime_entry_adoption_probe.cpp",
            "linux_uconsole_gtk_runtime_entry_adoption_probe_smoke.cpp",
            "gtk_runtime_entry_adoption_smoke.cpp",
        ],
        failures,
    )
    require_tokens(
        "cmake/TrailMateUxPacks.cmake",
        [
            "lvgl_runtime_screen_graph_presenter.cpp",
            "lvgl_runtime_entry_adoption.cpp",
            "lvgl_runtime_adoption_probe.cpp",
        ],
        failures,
    )

    require_tokens(
        "apps/linux_sim_shell/tests/linux_sim_runtime_entry_adoption_probe_smoke.cpp",
        [
            "LinuxSimRuntimeEntryAdoptionProbe",
            "fallbackUsed",
            "route.valid",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_sim_shell/tests/linux_sim_runtime_entry_smoke.cpp",
        [
            "LinuxSimRuntimeEntry",
            "fallbackUsed",
            "route.valid",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_runtime_entry_adoption_probe_smoke.cpp",
        [
            "LinuxUConsoleGtkRuntimeEntryAdoptionProbe",
            "fallbackUsed",
            "route.valid",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_page_registry_adoption_smoke.cpp",
        [
            "LinuxUConsoleGtkPageRegistryAdoption",
            "fallbackUsed",
            "route.valid",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_entry_adoption.cpp",
        [
            "LvglRuntimeEntryAdoption",
            "PresentationBundle",
            "route.valid",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_runtime_adoption_probe.cpp",
        [
            "LvglRuntimeAdoptionProbe",
            "LvglRuntimeEntryAdoption",
            "fallbackUsed",
            "route.valid",
        ],
        failures,
    )

    require_tokens(
        "docs/audits/PHASE9_RUNTIME_ADOPTION_REPORT.md",
        [
            "Phase 9 Rule",
            "Phase 9.2 Entry Adoption",
            "modules/ui_ascii_runtime",
            "modules/ui_gtk_runtime",
            "apps/linux_sim_shell",
            "apps/linux_uconsole_gtk",
            "Phase 9.3 Real Entry Adoption",
            "must not be added to `legacy/`",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_RUNTIME_ENTRY_ADOPTION_REPORT.md",
        [
            "fallback",
            "apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.*",
            "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_runtime_entry_adoption_probe.*",
            "modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp",
            "modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp",
            "LinuxSimRuntimeEntry",
            "LinuxUConsoleGtkPageRegistryAdoption",
            "LvglRuntimeAdoptionProbe",
            "legacy/app_implementations remains burn-down",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md",
        [
            "LinuxSim hardcoded runtime routing",
            "GTK hardcoded page registry",
            "LVGL hardcoded menu/page creation",
            "Chat LegacyDelivery bridges",
            "KeyVerification legacy source/sink",
            "MapOverlay legacy source",
            "exit condition",
            "contained fallback",
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
