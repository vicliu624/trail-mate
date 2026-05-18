#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

ACTIVE_ROOTS = [
    "src",
    "apps",
    "builds",
    "modules",
    "platform",
    "boards",
    "variants",
    "cmake",
]

FORBIDDEN_ACTIVE_TOKENS = [
    "apps/esp_pio",
    "apps\\esp_pio",
    "apps/gat562_mesh_evb_pro",
    "apps\\gat562_mesh_evb_pro",
]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")


def iter_files(root: Path):
    if not root.exists():
        return
    for path in root.rglob("*"):
        if path.is_file():
            yield path


def require_file(rel: str, failures: list[str]) -> None:
    if not (ROOT / rel).is_file():
        failures.append(f"missing required ESP Arduino owner file: {rel}")


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


def check_active_code_has_no_deleted_app_root_references(failures: list[str]) -> None:
    for root_name in ACTIVE_ROOTS:
        for path in iter_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in FORBIDDEN_ACTIVE_TOKENS:
                if token in text:
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} contains deleted app root reference: {token}"
                    )


def check_tlora_pager_has_no_sx1280_surface(failures: list[str]) -> None:
    roots = [
        "variants/lilygo_tlora_pager",
        "boards/tlora_pager",
        "docs/devices/lilygo-t-lora-pager.md",
        "docs/targets/esp-t-lora-pager-lvgl.capabilities.yaml",
        "docs/boards/t-lora-pager.board.yaml",
    ]
    tokens = [
        "tlora_pager_sx1280",
        "ARDUINO_LILYGO_LORA_SX1280",
        "sx1262_or_sx1280_by_build_env",
        "SX1280 env",
    ]
    for root_name in roots:
        root = ROOT / root_name
        files = [root] if root.is_file() else list(iter_files(root))
        for path in files:
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in tokens:
                if token in text:
                    failures.append(
                        f"{path.relative_to(ROOT).as_posix()} contains removed pager SX1280 surface: {token}"
                    )


def main() -> int:
    failures: list[str] = []

    for forbidden_dir in [
        "apps/esp_pio",
        "apps/gat562_mesh_evb_pro",
    ]:
        if (ROOT / forbidden_dir).exists():
            failures.append(f"{forbidden_dir}/ must not be restored")

    for rel in [
        "apps/esp32_lvgl/library.json",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_app_registry.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_app_runtime_access.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_app_runtime_access.h",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_entry.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_entry.h",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_loop_runtime.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_loop_runtime.h",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.cpp",
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.h",
        "platform/esp/arduino_common/include/app/app_context.h",
        "platform/esp/arduino_common/src/app_context.cpp",
        "platform/esp/arduino_common/src/app_context_time_sync.cpp",
    ]:
        require_file(rel, failures)

    require_tokens(
        "src/main.cpp",
        [
            '#include "esp32_lvgl_arduino_entry.h"',
            '#include "nrf52_node_arduino_entry.h"',
            "trailmate::apps::esp32_lvgl::arduino_entry::setup()",
            "trailmate::apps::esp32_lvgl::arduino_entry::loop()",
            "trailmate::apps::nrf52_node::arduino_entry::setup()",
            "trailmate::apps::nrf52_node::arduino_entry::loop()",
        ],
        failures,
    )
    forbid_tokens("src/main.cpp", FORBIDDEN_ACTIVE_TOKENS, failures)

    require_tokens(
        "apps/esp32_lvgl/library.json",
        [
            '"name": "trailmate_esp32_lvgl_app_shell"',
            "+<esp32_lvgl_arduino_app_registry.cpp>",
            "+<esp32_lvgl_arduino_app_runtime_access.cpp>",
            "+<esp32_lvgl_arduino_entry.cpp>",
            "+<esp32_lvgl_arduino_loop_runtime.cpp>",
            "+<esp32_lvgl_arduino_startup_runtime.cpp>",
            '"name": "esp_arduino_common"',
        ],
        failures,
    )

    require_tokens(
        "platform/esp/arduino_common/src/app_runtime_bootstrap_support.cpp",
        ['#include "app/app_context.h"'],
        failures,
    )
    forbid_tokens(
        "platform/esp/arduino_common/src/app_runtime_bootstrap_support.cpp",
        ['#include "apps/esp_pio/app_context.h"'],
        failures,
    )

    require_tokens(
        "platformio.ini",
        [
            "trailmate-nrf52-node-app-shell",
            "ui_mono_128x64",
        ],
        failures,
    )
    forbid_tokens(
        "platformio.ini",
        [
            "-I${PROJECT_DIR}/modules/",
            "-I${PROJECT_DIR}/platform/esp/arduino_common/include",
        ],
        failures,
    )

    require_tokens(
        "variants/lilygo_twatch_s3/envs/lilygo_twatch_s3.ini",
        ["-<modules/ui_shared/src/ui/screens/chat/>"],
        failures,
    )

    check_active_code_has_no_deleted_app_root_references(failures)
    check_tlora_pager_has_no_sx1280_surface(failures)

    if failures:
        for failure in failures:
            print(f"[esp-arduino-real-targets] {failure}")
        return 1

    print("[esp-arduino-real-targets] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
