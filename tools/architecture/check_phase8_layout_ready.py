#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def fail(message: str) -> int:
    print(f"[phase8-layout-ready] FAIL: {message}")
    return 1


def check_required_files() -> int:
    required = [
        "docs/specification/REPOSITORY_LAYOUT_ARCHITECTURE_SPEC.md",
        "docs/specification/DEVICE_UX_PACK_ARCHITECTURE_SPEC.md",
        "docs/audits/REPOSITORY_LAYOUT_CURRENT_STATE_AUDIT.md",
        "docs/audits/UI_SHARED_SPLIT_AUDIT.md",
        "tools/architecture/check_phase8_layout_ready.py",
    ]

    failures = 0
    for path in required:
        if not exists(path):
            failures += fail(f"missing required file: {path}")
    return failures


def check_tokens(path: str, tokens: list[str], label: str) -> int:
    if not exists(path):
        return 0

    text = read_text(path)
    failures = 0
    for token in tokens:
        if token not in text:
            failures += fail(f"{path} missing {label} token: {token}")
    return failures


def check_specification_language() -> int:
    failures = 0

    failures += check_tokens(
        "docs/specification/REPOSITORY_LAYOUT_ARCHITECTURE_SPEC.md",
        [
            "apps/",
            "builds/",
            "modules/",
            "platform/",
            "boards/",
            "docs/targets/",
            "docs/ux_profiles/",
            "builds -> apps",
            "apps -> modules + platform + boards",
            "modules -> modules only",
            "platform -> SDK/HAL/runtime only",
            "boards -> facts only",
            "modules -> builds",
            "modules -> apps",
            "platform -> apps",
            "boards -> apps",
            "ui_presentation -> platform",
            "ui_presentation -> LVGL",
            "ui_presentation -> filesystem",
            "apps cannot be named by build system",
            "builds cannot carry product app shell",
            "platform cannot carry product behavior",
            "boards cannot decide UX",
            "modules cannot depend on builds",
        ],
        "repository layout",
    )

    failures += check_tokens(
        "docs/specification/DEVICE_UX_PACK_ARCHITECTURE_SPEC.md",
        [
            "DeviceUxProfile",
            "UxPack",
            "UxFeatureSet",
            "ScreenSet",
            "InputBindingSet",
            "ScreenFactory",
            "LayoutProfile",
            "Board describes",
            "Target chooses",
            "UX Pack presents",
            "Renderer draws",
            "ui_presentation",
            "ui_lvgl_core",
            "ui_lvgl_ux_packs",
            "apps/target",
            "boards",
            "Do not branch inside pages on `BOARD_xxx` macros",
            "Renderer must not detect concrete device",
            "Board facts must not decide product features",
            "Do not put all device UI in one `chat_page.cpp` / `map_page.cpp`",
        ],
        "device UX pack",
    )

    return failures


def check_audit_language() -> int:
    failures = 0

    failures += check_tokens(
        "docs/audits/REPOSITORY_LAYOUT_CURRENT_STATE_AUDIT.md",
        [
            "apps/esp_idf",
            "apps/esp_pio",
            "build entrypoints, not final app shells",
            "ui_shared",
            "LVGL core/widgets/screens/runtime bridges/adapters",
            "ESP/nRF UI appears in modules while uConsole is in apps",
            "device-specific UX",
            "Legacy*",
            "Legacy* adapters remain bounded but not renamed/deleted",
        ],
        "repository audit",
    )

    failures += check_tokens(
        "docs/audits/UI_SHARED_SPLIT_AUDIT.md",
        [
            "LVGL core widgets",
            "LVGL screens",
            "legacy presentation sources",
            "legacy action bridges",
            "chat screen runtime helpers",
            "map tile helpers",
            "team picker renderer",
            "key verification modal renderer",
            "GPS page runtime pump",
            "ui_lvgl_core",
            "ui_lvgl_ux_packs",
            "ui_legacy_adapters",
            "ui_map_runtime",
            "ui_chat_runtime",
            "ui_gps_runtime",
        ],
        "ui_shared split audit",
    )

    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_specification_language()
    failures += check_audit_language()

    if failures:
        return 1

    print("[phase8-layout-ready] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
