#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = "docs/audits/LEGACY_COMPAT_TEMP_SURFACE_INVENTORY.md"


HISTORICAL_DESCRIPTOR_FILES = [
    "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h",
    "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.cpp",
    "apps/linux_sim_shell/tests/linux_sim_historical_source_descriptor_smoke.cpp",
    "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h",
    "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.cpp",
    "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_historical_source_descriptor_smoke.cpp",
    "apps/nrf52_node/src/nrf52_historical_source_descriptor.h",
    "apps/nrf52_node/src/nrf52_historical_source_descriptor.cpp",
    "apps/nrf52_node/tests/nrf52_historical_source_descriptor_smoke.cpp",
    "apps/esp32_lvgl/src/esp32_lvgl_historical_source_descriptor.h",
    "apps/esp32_lvgl/src/esp32_lvgl_historical_source_descriptor.cpp",
    "apps/esp32_lvgl/tests/esp32_lvgl_historical_source_descriptor_smoke.cpp",
]


HEADLESS_RUNTIME_FILES = [
    "modules/ui_headless_runtime/include/ui_headless_runtime/headless_descriptor_consumer.h",
    "modules/ui_headless_runtime/src/headless_descriptor_consumer.cpp",
    "modules/ui_headless_runtime/tests/test_headless_descriptor_consumer.cpp",
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


def iter_files(base: Path):
    if not base.exists():
        return
    for path in base.rglob("*"):
        if path.is_file():
            yield path


def check_no_legacy_source_descriptor_files(failures: list[str]) -> None:
    for path in iter_files(ROOT / "apps"):
        rel = path.relative_to(ROOT).as_posix()
        if "legacy_source_descriptor" in rel:
            failures.append(f"apps/ still contains legacy_source_descriptor file: {rel}")


def check_historical_descriptors(failures: list[str]) -> None:
    for rel in HISTORICAL_DESCRIPTOR_FILES:
        require_file(rel, failures)

    forbidden = [
        'root_path = "legacy/',
        "active_root",
        "source_root_path",
    ]
    for rel in HISTORICAL_DESCRIPTOR_FILES:
        path = ROOT / rel
        if not path.is_file():
            continue
        text = read(rel)
        for token in forbidden:
            if token in text:
                failures.append(f"{rel} contains forbidden historical descriptor token: {token}")

    require_tokens(
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h",
        [
            "LinuxSimHistoricalSourceDescriptor",
            "historical_root_name",
            "historical_role",
            "replacement_owner",
        ],
        failures,
    )
    require_tokens(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h",
        [
            "LinuxUConsoleGtkHistoricalSourceDescriptor",
            "historical_root_name",
            "historical_role",
            "replacement_owner",
        ],
        failures,
    )
    require_tokens(
        "apps/nrf52_node/src/nrf52_historical_source_descriptor.h",
        [
            "Nrf52HistoricalSourceDescriptor",
            "historical_generic_root_name",
            "historical_board_root_name",
            "replacement_owner",
        ],
        failures,
    )
    require_tokens(
        "apps/esp32_lvgl/src/esp32_lvgl_historical_source_descriptor.h",
        [
            "Esp32LvglHistoricalSourceDescriptor",
            "historical_root_name",
            "historical_role",
            "replacement_owner",
        ],
        failures,
    )


def check_nrf52_gat562_final_owner_surface(failures: list[str]) -> None:
    for rel in [
        "boards/gat562_mesh_evb_pro/BOARD.md",
        "boards/gat562_mesh_evb_pro/board_facts.h",
    ]:
        require_file(rel, failures)
    for rel in HEADLESS_RUNTIME_FILES:
        require_file(rel, failures)

    require_tokens(
        "boards/gat562_mesh_evb_pro/board_facts.h",
        [
            "Gat562BoardFacts",
            "board_id = \"gat562\"",
            "display_present = true",
            "display_width = 128",
            "display_height = 64",
            "lora_present = true",
            "gnss_present = true",
            "keyboard_present = false",
            "touch_present = false",
        ],
        failures,
    )
    require_tokens(
        "modules/ui_headless_runtime/include/ui_headless_runtime/headless_descriptor_consumer.h",
        [
            "HeadlessRuntimeDescriptorConsumer",
            "HeadlessPageManifestConsumer",
            "NodeStatusDescriptor",
        ],
        failures,
    )


def check_pio_nrf52_legacy_dependency_extracted(failures: list[str]) -> None:
    for rel in [
        "builds/pio_nrf52/platformio.ini",
        "builds/pio_nrf52/src/nrf52_node_wrapper_baseline.cpp",
        "apps/nrf52_node/library.json",
    ]:
        require_file(rel, failures)

    forbidden_by_file = {
        "builds/pio_nrf52/platformio.ini": [
            "legacy/app_implementations/esp_pio/include",
            "legacy/app_implementations/esp_pio/src",
            "legacy/app_implementations/gat562_mesh_evb_pro/include",
            "legacy/app_implementations/gat562_mesh_evb_pro/src",
            "nrf52_pio_legacy_implementation_adapter.h",
        ],
        "builds/pio_nrf52/src/nrf52_node_wrapper_baseline.cpp": [
            "nrf52_pio_legacy_implementation_adapter.h",
            "Nrf52PioLegacyImplementationDescriptor",
        ],
        "apps/nrf52_node/library.json": [
            "legacy/app_implementations/esp_pio/include",
            "legacy/app_implementations/esp_pio/src",
            "legacy/app_implementations/gat562_mesh_evb_pro/include",
            "legacy/app_implementations/gat562_mesh_evb_pro/src",
        ],
    }
    for rel, tokens in forbidden_by_file.items():
        if not (ROOT / rel).is_file():
            continue
        text = read(rel)
        for token in tokens:
            if token in text:
                failures.append(f"{rel} contains forbidden active PIO dependency token: {token}")


def check_esp_idf_landing_plan(failures: list[str]) -> None:
    rel = "docs/audits/ESP_IDF_FINAL_OWNER_MIGRATION_PLAN.md"
    require_file(rel, failures)
    require_tokens(
        rel,
        [
            "idf_component.yml",
            "CMakeLists.txt",
            "targets/*/sdkconfig.defaults",
            "startup_runtime",
            "loop_runtime",
            "runtime_config",
            "meshtastic_radio_adapter",
            "gps_service_api",
            "gps_tracker_overlay",
            "team_ui_store",
            "idf_entry",
            "esp_idf_legacy_implementation_adapter",
            "no ESP-IDF legacy root is deleted",
        ],
        failures,
    )


def check_inventory_updated(failures: list[str]) -> None:
    require_file(INVENTORY, failures)
    if not (ROOT / INVENTORY).is_file():
        return
    text = read(INVENTORY)
    for token in [
        "linux_sim_historical_source_descriptor (formerly linux_sim_legacy_source_descriptor)",
        "linux_uconsole_gtk_historical_source_descriptor (formerly linux_uconsole_gtk_legacy_source_descriptor)",
        "Batch 1 completed the rename and removed the `root_path` field",
        "nrf52_historical_source_descriptor",
        "esp32_lvgl_historical_source_descriptor",
        "ui_headless_runtime descriptor consumer",
    ]:
        if token not in text:
            failures.append(f"{INVENTORY} missing Batch 1 inventory token: {token}")


def run_checker(rel: str, failures: list[str]) -> None:
    path = ROOT / rel
    if not path.is_file():
        failures.append(f"missing checker: {rel}")
        return
    result = subprocess.run(
        [sys.executable, str(path)],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="ignore",
        capture_output=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        failures.append(f"{rel} failed:\n{output}")


def main() -> int:
    failures: list[str] = []

    check_no_legacy_source_descriptor_files(failures)
    check_historical_descriptors(failures)
    check_nrf52_gat562_final_owner_surface(failures)
    check_pio_nrf52_legacy_dependency_extracted(failures)
    check_esp_idf_landing_plan(failures)
    check_inventory_updated(failures)
    if (ROOT / "legacy").exists():
        run_checker("tools/architecture/check_no_root_legacy_preflight_ready.py", failures)
    else:
        run_checker("tools/architecture/check_no_root_legacy_ready.py", failures)

    if failures:
        for failure in failures:
            print(f"[legacy-disposition-execution] {failure}")
        return 1

    print("[legacy-disposition-execution] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
