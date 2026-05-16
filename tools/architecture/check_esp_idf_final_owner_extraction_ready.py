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


def main() -> int:
    failures: list[str] = []

    forbid_tokens(
        "apps/esp32_lvgl/CMakeLists.txt",
        [
            "trailmate_esp_idf_legacy_adapter",
            "esp_idf_legacy_implementation_adapter.cpp",
        ],
        failures,
    )
    for rel in [
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.h",
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp",
        "apps/esp32_lvgl/tests/esp32_lvgl_app_shell_smoke.cpp",
    ]:
        forbid_tokens(rel, ["esp_idf_legacy_implementation_adapter.h", "legacy_adapter_target"], failures)

    for rel in [
        "apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.h",
        "apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_loop_runtime.h",
        "apps/esp32_lvgl/src/esp32_lvgl_loop_runtime.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_runtime_config.h",
        "apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp",
        "builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake",
        "builds/esp_idf/targets/tab5/sdkconfig.defaults",
        "platform/esp/radio/meshtastic_radio_adapter.h",
        "platform/esp/radio/meshtastic_radio_adapter.cpp",
        "docs/audits/ESP_IDF_RUNTIME_SOURCE_OWNER_EXTRACTION_AUDIT.md",
        "docs/audits/ESP_IDF_TARGET_DEFAULTS_MIGRATION_AUDIT.md",
    ]:
        require_file(rel, failures)

    if not (ROOT / "builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults").is_file():
        require_tokens(
            "docs/audits/ESP_IDF_TARGET_DEFAULTS_MIGRATION_AUDIT.md",
            ["tdisplayp4_tft", "missing / deferred"],
            failures,
        )

    require_tokens(
        "builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake",
        [
            "TRAILMATE_ESP_IDF_APP_SHELL_SOURCES",
            "esp32_lvgl_startup_runtime.cpp",
            "esp32_lvgl_loop_runtime.cpp",
            "esp32_lvgl_runtime_config.cpp",
            "TRAILMATE_ESP_IDF_PLATFORM_SOURCES",
            "platform/esp/radio/meshtastic_radio_adapter.cpp",
        ],
        failures,
    )
    if (ROOT / "legacy").exists():
        require_tokens(
            "legacy/app_implementations/esp_idf/CMakeLists.txt",
            [
                "ESP_IDF_COMPONENT_SOURCES.cmake",
                "src/startup_runtime\\.cpp",
                "src/loop_runtime\\.cpp",
                "src/runtime_config\\.cpp",
                "src/meshtastic_radio_adapter\\.cpp",
            ],
            failures,
        )
        require_tokens(
            "legacy/app_implementations/esp_idf/src/app_facade_runtime.cpp",
            [
                "platform/esp/radio/meshtastic_radio_adapter.h",
                "platform::esp::radio::MeshtasticRadioAdapter",
            ],
            failures,
        )
        forbid_tokens(
            "legacy/app_implementations/esp_idf/src/app_facade_runtime.cpp",
            [
                "apps/esp_idf/meshtastic_radio_adapter.h",
                "apps::esp_idf::MeshtasticRadioAdapter",
            ],
            failures,
        )
    else:
        require_file("docs/archive/REMOVED_LEGACY_ROOTS.md", failures)

    require_tokens(
        "docs/audits/ESP_IDF_RUNTIME_SOURCE_OWNER_EXTRACTION_AUDIT.md",
        [
            "startup runtime",
            "loop runtime",
            "runtime config",
            "Meshtastic radio adapter",
            "GPS service API",
            "GPS tracker overlay",
            "Team UI store",
            "Final owner",
            "Delete condition",
        ],
        failures,
    )

    if failures:
        for failure in failures:
            print(f"[esp-idf-final-owner-extraction] {failure}")
        return 1

    print("[esp-idf-final-owner-extraction] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
