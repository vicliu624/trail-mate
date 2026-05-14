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
        "docs/decisions/ADR_BUILD_ENTRYPOINTS.md",
        "docs/audits/BUILD_ENTRYPOINT_NORMALIZATION_AUDIT.md",
        "docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md",
        "builds/README.md",
        "builds/esp_idf/README.md",
        "builds/pio_nrf52/README.md",
        "builds/linux_cmake/README.md",
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
            "Phase 8.2 establishes builds/ skeleton",
            "builds/esp_idf",
            "builds/pio_nrf52",
            "builds/linux_cmake",
            "`apps/esp_idf` and `apps/esp_pio` remain transitional",
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


def check_build_entrypoint_language() -> int:
    failures = 0

    build_tokens = [
        "ESP-IDF",
        "PlatformIO",
        "nRF52",
        "Linux",
        "CMake",
        "apps/esp_idf",
        "apps/esp_pio",
        "transitional",
        "thin wrapper",
        "App Shell composes",
        "Build Entrypoint invokes",
    ]

    failures += check_tokens(
        "docs/decisions/ADR_BUILD_ENTRYPOINTS.md",
        build_tokens
        + [
            "ESP / ESP32-P4",
            "BuildEntrypointManifest",
            "They are not final product app shells",
        ],
        "build entrypoint ADR",
    )

    failures += check_tokens(
        "docs/audits/BUILD_ENTRYPOINT_NORMALIZATION_AUDIT.md",
        build_tokens
        + [
            "apps/linux_sim",
            "apps/linux_uconsole",
            "apps/linux_rpi",
            "apps/linux_unoq",
            "apps/gat562_mesh_evb_pro",
            "root `build/`",
            "build.cardputer",
            "build.tab5",
            "BuildEntrypoint Constraints",
            "No build behavior changes in this phase",
        ],
        "build entrypoint audit",
    )

    failures += check_tokens(
        "docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md",
        [
            "apps/esp_idf",
            "apps/esp_pio",
            "transitional historical build entrypoints",
            "They are not final product app shells",
            "New code must not treat this as final app shell semantics",
            "Build Entrypoint invokes",
            "App Shell composes",
        ],
        "transitional build entrypoint marker",
    )

    failures += check_tokens(
        "builds/README.md",
        [
            "build system entrypoints",
            "It does not contain product app shells",
            "Build Entrypoint invokes",
            "App Shell composes",
            "thin wrappers",
            "apps/esp_idf",
            "apps/esp_pio",
        ],
        "builds README",
    )

    failures += check_tokens(
        "builds/esp_idf/README.md",
        [
            "Authoritative ESP build entrypoint",
            "ESP / ESP32-P4 -> ESP-IDF",
            "Current transitional path",
            "apps/esp_idf",
            "apps/esp32_lvgl",
            "thin wrapper",
            "Build Entrypoint invokes",
            "App Shell composes",
        ],
        "ESP-IDF build README",
    )

    failures += check_tokens(
        "builds/pio_nrf52/README.md",
        [
            "Authoritative nRF52 build entrypoint",
            "nRF52 -> PlatformIO",
            "apps/esp_pio",
            "root platformio.ini",
            "apps/nrf52_node",
            "thin wrapper",
            "Build Entrypoint invokes",
            "App Shell composes",
        ],
        "PIO nRF52 build README",
    )

    failures += check_tokens(
        "builds/linux_cmake/README.md",
        [
            "Authoritative Linux build entrypoint",
            "Linux -> CMake",
            "apps/linux_sim",
            "apps/linux_uconsole",
            "apps/linux_rpi",
            "apps/linux_unoq",
            "selected Linux app shell",
            "thin wrapper",
            "Build Entrypoint invokes",
            "App Shell composes",
        ],
        "Linux CMake build README",
    )

    return failures


def check_transitional_app_markers() -> int:
    failures = 0
    marker_path = "docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md"
    marker = read_text(marker_path) if exists(marker_path) else ""

    for path in ["apps/esp_idf", "apps/esp_pio"]:
        if exists(path) and path not in marker:
            failures += fail(f"{path} exists but is not marked transitional")

    apps_root = ROOT / "apps"
    if not apps_root.exists():
        return failures

    allowed = {"esp_idf", "esp_pio"}
    suspicious_terms = ["idf", "pio", "platformio", "cmake"]
    for child in apps_root.iterdir():
        if not child.is_dir():
            continue
        name = child.name.lower()
        if name in allowed:
            continue
        if any(term in name for term in suspicious_terms):
            failures += fail(
                f"apps/{child.name} looks build-system-named; use builds/ for new build entrypoints"
            )

    return failures


def main() -> int:
    failures = 0
    failures += check_required_files()
    failures += check_specification_language()
    failures += check_audit_language()
    failures += check_build_entrypoint_language()
    failures += check_transitional_app_markers()

    if failures:
        return 1

    print("[phase8-layout-ready] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
