#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
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


def run_checker(rel: str, failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing checker: {rel}")
        return
    result = subprocess.run(
        [sys.executable, str(path)],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        failures.append(f"{rel} failed:\n{output}")


def iter_code_files(root: Path):
    if not root.exists():
        return
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
            yield path


def is_allowed_legacy_alias_path(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    name = path.name
    allowed_prefixes = [
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/",
        "modules/ui_shared/include/ui/presentation_sources/",
    ]
    return (
        any(rel.startswith(prefix) for prefix in allowed_prefixes)
        or name.endswith("_legacy_alias.cpp")
        or name.endswith("_legacy_alias_test.cpp")
        or name.endswith("_compatibility_test.cpp")
    )


def check_apps_directory(failures: list[str]) -> None:
    allowed = {
        "README.md",
        "esp32_lvgl",
        "nrf52_node",
        "linux_uconsole_gtk",
        "linux_sim_shell",
    }
    apps_root = ROOT / "apps"
    for path in apps_root.iterdir():
        if path.name not in allowed:
            failures.append(f"apps/ contains non-final app shell entry: {path.name}")


def check_legacy_app_implementations(failures: list[str]) -> None:
    legacy_root = ROOT / "legacy" / "app_implementations"
    if not legacy_root.is_dir():
        return

    forbidden = [
        "RuntimeEntryAdoption",
        "RuntimeScreenGraphPresenter",
        "DescriptorRenderer",
        "DescriptorPageRegistry",
        "LvglPrimaryScreenGraphRuntime",
    ]
    for path in legacy_root.rglob("*"):
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for token in forbidden:
            if token in text:
                rel = path.relative_to(ROOT).as_posix()
                failures.append(
                    f"{rel} contains post-refactor runtime adoption token: {token}"
                )


def check_renderer_forbidden_tokens(failures: list[str]) -> None:
    renderer_files = [
        "apps/linux_sim_shell/src/linux_sim_runtime_renderer.cpp",
        "apps/linux_sim_shell/src/linux_sim_runtime_renderer.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_renderer.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_page_registry_renderer.h",
        "modules/ui_ascii_runtime/src/ascii_descriptor_renderer.cpp",
        "modules/ui_gtk_runtime/src/gtk_descriptor_page_registry.cpp",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_menu_model.cpp",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_renderer_probe.cpp",
    ]
    forbidden = [
        "UxPackRegistry",
        "findUxPackById",
        "buildMenuForUxPack",
        "activeUxPackId",
        "GtkWidget",
        "gtk/gtk.h",
        "lvgl.h",
        "lv_obj_t",
        "BOARD_",
    ]
    for rel in renderer_files:
        forbid_tokens(rel, forbidden, failures)


def check_no_main_code_includes_burned_down_legacy_headers(
    failures: list[str],
) -> None:
    forbidden_includes = [
        '#include "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h"',
        '#include "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_source.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_action_sink.h"',
        '#include "ui_legacy_adapters/legacy_key_verification_session.h"',
        '#include "ui_legacy_adapters/legacy_map_overlay_source.h"',
    ]
    for root_name in ["apps", "legacy/app_implementations", "modules", "platform", "boards"]:
        for path in iter_code_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in forbidden_includes:
                if token in text and not is_allowed_legacy_alias_path(path):
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} includes burned-down legacy header: {token}"
                    )


def check_phase12_docs(failures: list[str]) -> None:
    required = [
        "docs/audits/PHASE12_FALLBACK_DELETION_READINESS.md",
        "docs/audits/PHASE12_DEPRECATED_ALIAS_CLEANUP_PLAN.md",
        "docs/specification/POST_REFACTOR_ARCHITECTURE_FREEZE.md",
        "docs/audits/POST_REFACTOR_FINAL_CLOSEOUT_REPORT.md",
    ]
    for rel in required:
        require_file(rel, failures)

    require_tokens(
        "docs/audits/PHASE12_FALLBACK_DELETION_READINESS.md",
        [
            "LinuxSim hardcoded runtime routing",
            "GTK hardcoded page registry",
            "LVGL hardcoded menu/page creation",
            "Chat legacy alias headers",
            "KeyVerification legacy alias headers",
            "MapOverlay legacy alias header",
            "`ui_shared` forwarding shims",
            "`legacy/app_implementations` roots",
            "Safe to delete now?",
            "Deletion condition",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE12_DEPRECATED_ALIAS_CLEANUP_PLAN.md",
        [
            "LegacyChatDeliveryActionBridge",
            "LegacyChatDeliveryEventBridge",
            "LegacyKeyVerificationSource",
            "LegacyKeyVerificationActionSink",
            "LegacyKeyVerificationSession",
            "LegacyMapOverlaySource",
            "Main code must not include deprecated alias headers",
        ],
        failures,
    )
    require_tokens(
        "docs/specification/POST_REFACTOR_ARCHITECTURE_FREEZE.md",
        [
            "`apps/`",
            "`builds/`",
            "`modules/`",
            "`platform/`",
            "`boards/`",
            "Root `legacy/` has been removed",
            "Renderers must not select UX packs",
            "Runtime entries must not call `buildMenuForUxPack`",
            "`modules/` must not depend on `apps/`",
            "New `Legacy*` bridges are forbidden",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/POST_REFACTOR_FINAL_CLOSEOUT_REPORT.md",
        [
            "This architecture refactor is complete",
            "Phase 8 directory/app shell/UX/menu/screen graph",
            "Phase 9 runtime adoption and legacy burn-down",
            "Phase 10 primary path migration",
            "Phase 11 renderer descriptor consumption",
            "Phase 12 closeout",
            "Batch 4 root legacy elimination",
            "Next work is not architecture refactor",
            "feature work",
            "targeted fallback deletion",
        ],
        failures,
    )
    require_tokens(
        "docs/audits/PHASE11_RENDERER_DESCRIPTOR_CONSUMPTION_REPORT.md",
        [
            "fallback-only",
            "Phase 11 does not rewrite real GTK widgets",
            "Phase 11 does not create LVGL widgets",
            "Phase 11 does not delete fallback",
        ],
        failures,
    )
    forbid_tokens(
        "docs/audits/PHASE11_RENDERER_DESCRIPTOR_CONSUMPTION_REPORT.md",
        ["primary hardcoded routing"],
        failures,
    )


def main() -> int:
    failures: list[str] = []

    for rel in [
        "tools/architecture/check_phase8_layout_ready.py",
        "tools/architecture/check_phase9_runtime_adoption_ready.py",
        "tools/architecture/check_phase9_legacy_burndown_ready.py",
        "tools/architecture/check_phase10_primary_path_ready.py",
        "tools/architecture/check_phase11_renderer_consumption_ready.py",
        "tools/architecture/check_legacy_compat_temp_inventory_ready.py",
        "tools/architecture/check_no_root_legacy_ready.py",
        "tools/architecture/check_legacy_disposition_execution_ready.py",
        "tools/architecture/check_esp_idf_final_owner_extraction_ready.py",
        "tools/architecture/check_target_architecture_foundation_ready.py",
        "tools/architecture/check_all_target_architecture_ready.py",
        "tools/architecture/check_no_intermediate_ui_layer_ready.py",
        "tools/architecture/check_board_facts_boundary_ready.py",
        "tools/architecture/check_uconsole_real_ui_restored.py",
        "tools/architecture/check_pio_real_env_restored.py",
        "tools/architecture/check_esp_arduino_real_targets_restored.py",
        "tools/architecture/check_esp_idf_real_targets_restored.py",
        "tools/architecture/check_real_target_regression_recovery_ready.py",
    ]:
        require_file(rel, failures)

    for rel in [
        "tools/architecture/check_phase8_layout_ready.py",
        "tools/architecture/check_phase9_runtime_adoption_ready.py",
        "tools/architecture/check_phase9_legacy_burndown_ready.py",
        "tools/architecture/check_phase10_primary_path_ready.py",
        "tools/architecture/check_phase11_renderer_consumption_ready.py",
        "tools/architecture/check_legacy_compat_temp_inventory_ready.py",
        "tools/architecture/check_no_root_legacy_ready.py",
        "tools/architecture/check_legacy_disposition_execution_ready.py",
        "tools/architecture/check_esp_idf_final_owner_extraction_ready.py",
        "tools/architecture/check_target_architecture_foundation_ready.py",
        "tools/architecture/check_all_target_architecture_ready.py",
        "tools/architecture/check_no_intermediate_ui_layer_ready.py",
        "tools/architecture/check_board_facts_boundary_ready.py",
        "tools/architecture/check_real_target_regression_recovery_ready.py",
    ]:
        run_checker(rel, failures)

    check_phase12_docs(failures)
    check_apps_directory(failures)
    check_legacy_app_implementations(failures)
    check_renderer_forbidden_tokens(failures)
    check_no_main_code_includes_burned_down_legacy_headers(failures)

    if failures:
        for failure in failures:
            print(f"[post-refactor-final] {failure}")
        return 1

    print("[post-refactor-final] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
