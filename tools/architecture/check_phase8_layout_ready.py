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


def iter_code_files(root: Path):
    if not root.exists():
        return
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
            yield path


def check_required_files() -> int:
    required = [
        "docs/specification/REPOSITORY_LAYOUT_ARCHITECTURE_SPEC.md",
        "docs/specification/DEVICE_UX_PACK_ARCHITECTURE_SPEC.md",
        "docs/audits/REPOSITORY_LAYOUT_CURRENT_STATE_AUDIT.md",
        "docs/audits/UI_SHARED_SPLIT_AUDIT.md",
        "docs/audits/UI_SHARED_COMPATIBILITY_SHIM_POLICY.md",
        "docs/decisions/ADR_BUILD_ENTRYPOINTS.md",
        "docs/audits/BUILD_ENTRYPOINT_NORMALIZATION_AUDIT.md",
        "docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md",
        "docs/specification/APP_SHELL_ARCHITECTURE_SPEC.md",
        "docs/audits/APP_SHELL_CURRENT_STATE_AUDIT.md",
        "builds/README.md",
        "builds/esp_idf/README.md",
        "builds/pio_nrf52/README.md",
        "builds/linux_cmake/README.md",
        "apps/esp32_lvgl/README.md",
        "apps/esp32_lvgl/APP_SHELL_MANIFEST.md",
        "apps/nrf52_node/README.md",
        "apps/nrf52_node/APP_SHELL_MANIFEST.md",
        "apps/linux_uconsole_gtk/README.md",
        "apps/linux_uconsole_gtk/APP_SHELL_MANIFEST.md",
        "apps/linux_sim_shell/README.md",
        "apps/linux_sim_shell/APP_SHELL_MANIFEST.md",
        "docs/targets/README.md",
        "docs/targets/esp32_lvgl_targets.md",
        "docs/targets/nrf52_node_targets.md",
        "docs/targets/linux_targets.md",
        "docs/ux_profiles/README.md",
        "docs/ux_profiles/deck_full.md",
        "docs/ux_profiles/pager_compact.md",
        "docs/ux_profiles/watch_quick.md",
        "docs/ux_profiles/cardputer_wide.md",
        "docs/ux_profiles/tab5_touch.md",
        "docs/ux_profiles/tiny_node_status.md",
        "docs/ux_profiles/uconsole_desktop.md",
        "modules/ui_chat_runtime/README.md",
        "modules/ui_chat_runtime/library.json",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_page_runtime_event_pump.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_ui_refresh_sink.h",
        "modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp",
        "modules/ui_map_runtime/README.md",
        "modules/ui_map_runtime/library.json",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_types.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_resolver.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_source.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_cache.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_render_queue.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/map_tile_decoder_cache.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/filesystem_map_tile_source.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay/map_overlay_projector.h",
        "modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp",
        "modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp",
        "modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp",
        "modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp",
        "modules/ui_map_runtime/tests/test_map_tile_resolver.cpp",
        "modules/ui_map_runtime/tests/test_map_tile_render_queue.cpp",
        "modules/ui_map_runtime/tests/test_filesystem_map_tile_source.cpp",
        "modules/ui_map_runtime/tests/test_map_overlay_projector.cpp",
        "modules/ui_gps_runtime/README.md",
        "modules/ui_gps_runtime/library.json",
        "modules/ui_gps_runtime/include/ui_gps_runtime/gps_page_runtime_pump.h",
        "modules/ui_gps_runtime/include/ui_gps_runtime/gps_ui_refresh_sink.h",
        "modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp",
        "modules/ui_gps_runtime/tests/test_gps_page_runtime_pump.cpp",
        "modules/ui_legacy_adapters/README.md",
        "modules/ui_legacy_adapters/library.json",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_event_bridge.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h",
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_source.cpp",
        "modules/ui_legacy_adapters/src/legacy_key_verification_action_sink.cpp",
        "modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source.cpp",
        "modules/ui_lvgl_core/README.md",
        "modules/ui_lvgl_core/library.json",
        "modules/ui_lvgl_core/include/ui_lvgl_core/lvgl_focus_group.h",
        "modules/ui_lvgl_core/include/ui_lvgl_core/.gitkeep",
        "modules/ui_lvgl_core/src/.gitkeep",
        "modules/ui_lvgl_ux_packs/README.md",
        "modules/ui_lvgl_ux_packs/library.json",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/common/team_position_picker_renderer.h",
        "modules/ui_lvgl_ux_packs/src/common/team_position_picker_renderer.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/common/key_verification_modal_renderer.h",
        "modules/ui_lvgl_ux_packs/src/common/key_verification_modal_renderer.cpp",
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
            "Phase 8.3 establishes app shell skeletons",
            "apps/esp32_lvgl",
            "apps/nrf52_node",
            "apps/linux_uconsole_gtk",
            "apps/linux_sim_shell",
            "modules/ui_chat_runtime",
            "modules/ui_map_runtime",
            "modules/ui_gps_runtime",
            "modules/ui_legacy_adapters",
            "modules/ui_lvgl_core",
            "modules/ui_lvgl_ux_packs",
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
            "UI_SHARED_COMPATIBILITY_SHIM_POLICY.md",
            "transitional umbrella",
            "Forwarding headers",
            "authoritative new module paths",
        ],
        "ui_shared split audit",
    )

    failures += check_tokens(
        "docs/audits/UI_SHARED_COMPATIBILITY_SHIM_POLICY.md",
        [
            "transitional umbrella",
            "compatibility shims only",
            "Authoritative include paths live in the owning module",
            "Forwarding Header Pattern",
            "ui_chat_runtime",
            "ui_map_runtime",
            "ui_gps_runtime",
            "ui_legacy_adapters",
            "ui_lvgl_core",
            "ui_lvgl_ux_packs",
            "ui_shared Must Not Receive",
            "new runtime helpers",
            "new legacy adapters",
            "new map tile helpers",
            "new GPS runtime helpers",
            "new chat runtime helpers",
            "new LVGL UX pack renderers",
            "LegacyFilesystemMapTileSource",
            "deprecated compatibility alias",
            "FilesystemMapTileSource",
            "check_phase8_layout_ready.py",
        ],
        "ui_shared compatibility shim policy",
    )

    return failures


def check_app_shell_language() -> int:
    failures = 0

    app_shell_tokens = [
        "AppShell",
        "AppShellManifest",
        "TargetProfile",
        "AppShellLifecycle",
        "AppShellCompatibilityAdapter",
        "Build Entrypoint invokes",
        "App Shell composes",
        "Target chooses",
        "Board describes",
        "UX Pack presents",
        "select target profile",
        "select board package",
        "select platform family",
        "select UX profile / UX pack",
        "hand off to product composition",
        "start runtime facade",
        "own build host files",
        "define board facts",
        "contain SDK/HAL implementation",
        "implement screen internals",
        "assemble Chat/Map/GPS details behind hidden globals",
    ]

    failures += check_tokens(
        "docs/specification/APP_SHELL_ARCHITECTURE_SPEC.md",
        app_shell_tokens
        + [
            "apps/esp32_lvgl",
            "apps/nrf52_node",
            "apps/linux_uconsole_gtk",
            "apps/linux_sim_shell",
            "trail_mate_esp32_lvgl_start",
            "trail_mate_nrf52_node_start",
            "trail_mate_linux_uconsole_gtk_start",
            "trail_mate_linux_sim_shell_start",
            "No behavior change",
        ],
        "app shell specification",
    )

    failures += check_tokens(
        "docs/audits/APP_SHELL_CURRENT_STATE_AUDIT.md",
        [
            "apps/esp_idf",
            "apps/esp_pio",
            "apps/linux_sim",
            "apps/linux_uconsole",
            "apps/linux_rpi",
            "apps/linux_unoq",
            "apps/gat562_mesh_evb_pro",
            "Current role",
            "App shell part",
            "Build entrypoint part",
            "Platform part",
            "Board-specific part",
            "Future app shell direction",
            "Migration risk",
            "apps/esp32_lvgl",
            "apps/nrf52_node",
            "apps/linux_uconsole_gtk",
            "apps/linux_sim_shell",
            "skeleton only",
        ],
        "app shell audit",
    )

    shell_docs = {
        "apps/esp32_lvgl/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/esp_idf`",
            "Current transitional path = `apps/esp_idf`",
            "must not",
            "own build host files",
            "No behavior change in Phase 8.3",
        ],
        "apps/esp32_lvgl/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "ESP / ESP32-P4",
            "LVGL",
            "`builds/esp_idf`",
            "`apps/esp_idf`",
            "own build host files",
            "Skeleton only",
            "No behavior change in Phase 8.3",
        ],
        "apps/nrf52_node/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/pio_nrf52`",
            "Current transitional path = `apps/esp_pio`",
            "apps/gat562_mesh_evb_pro",
            "own build host files",
            "No behavior change in Phase 8.3",
        ],
        "apps/nrf52_node/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "nRF52",
            "`builds/pio_nrf52`",
            "`apps/esp_pio`",
            "`apps/gat562_mesh_evb_pro`",
            "own build host files",
            "Skeleton only",
            "No behavior change in Phase 8.3",
        ],
        "apps/linux_uconsole_gtk/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/linux_cmake`",
            "Current transitional path = `apps/linux_uconsole`",
            "own build host files",
            "No behavior change in Phase 8.3",
        ],
        "apps/linux_uconsole_gtk/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "Linux uConsole",
            "GTK",
            "`builds/linux_cmake`",
            "`apps/linux_uconsole`",
            "own build host files",
            "Skeleton only",
            "No behavior change in Phase 8.3",
        ],
        "apps/linux_sim_shell/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/linux_cmake`",
            "Current transitional path = `apps/linux_sim`",
            "own build host files",
            "No behavior change in Phase 8.3",
        ],
        "apps/linux_sim_shell/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "Linux simulator",
            "`builds/linux_cmake`",
            "`apps/linux_sim`",
            "own build host files",
            "Skeleton only",
            "No behavior change in Phase 8.3",
        ],
    }

    for path, tokens in shell_docs.items():
        failures += check_tokens(path, tokens, "app shell skeleton")

    target_docs = {
        "docs/targets/README.md": [
            "Target profiles",
            "Build Entrypoint invokes",
            "App Shell composes",
            "Target chooses",
            "Board describes",
            "UX Pack presents",
            "esp32_lvgl_targets.md",
            "nrf52_node_targets.md",
            "linux_targets.md",
        ],
        "docs/targets/esp32_lvgl_targets.md": [
            "tab5",
            "tdeck",
            "builds/esp_idf",
            "apps/esp32_lvgl",
            "tab5_touch",
            "deck_full",
            "transitional",
            "No behavior change in Phase 8.3",
        ],
        "docs/targets/nrf52_node_targets.md": [
            "gat562",
            "gat562_mesh_evb_pro",
            "builds/pio_nrf52",
            "apps/nrf52_node",
            "tiny_node_status",
            "transitional",
            "apps/esp_pio",
            "No behavior change in Phase 8.3",
        ],
        "docs/targets/linux_targets.md": [
            "uconsole",
            "linux_sim",
            "builds/linux_cmake",
            "apps/linux_uconsole_gtk",
            "apps/linux_sim_shell",
            "uconsole_desktop",
            "simulator_full",
            "transitional",
            "No behavior change in Phase 8.3",
        ],
    }

    for path, tokens in target_docs.items():
        failures += check_tokens(path, tokens, "target profile")

    return failures


def check_ux_profile_language() -> int:
    failures = 0
    common = [
        "Screen Class",
        "Input Model",
        "Feature Set",
        "Screen Set",
        "Map Mode",
        "Chat Mode",
        "Team Action Mode",
        "GPS Mode",
        "Modal/Picker Strategy",
        "Renderer Family",
        "Deferred Decisions",
        "Board describes. Target chooses. UX Pack presents. Renderer draws.",
    ]
    profiles = [
        "docs/ux_profiles/deck_full.md",
        "docs/ux_profiles/pager_compact.md",
        "docs/ux_profiles/watch_quick.md",
        "docs/ux_profiles/cardputer_wide.md",
        "docs/ux_profiles/tab5_touch.md",
        "docs/ux_profiles/tiny_node_status.md",
        "docs/ux_profiles/uconsole_desktop.md",
    ]
    for path in profiles:
        failures += check_tokens(path, common, "UX profile")

    failures += check_tokens(
        "docs/ux_profiles/README.md",
        [
            "Board describes",
            "Target chooses",
            "UX Pack presents",
            "Renderer draws",
            "deck_full.md",
            "pager_compact.md",
            "watch_quick.md",
            "cardputer_wide.md",
            "tab5_touch.md",
            "tiny_node_status.md",
            "uconsole_desktop.md",
        ],
        "UX profile index",
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


def check_forwarding_headers() -> int:
    expected = {
        "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h":
            "ui_chat_runtime/chat_page_runtime_event_pump.h",
        "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h":
            "ui_chat_runtime/chat_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/screens/gps/gps_page_runtime_pump.h":
            "ui_gps_runtime/gps_page_runtime_pump.h",
        "modules/ui_shared/include/ui/screens/gps/gps_ui_refresh_sink.h":
            "ui_gps_runtime/gps_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_types.h":
            "ui_map_runtime/map_tiles/map_tile_types.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_resolver.h":
            "ui_map_runtime/map_tiles/map_tile_resolver.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_source.h":
            "ui_map_runtime/map_tiles/map_tile_source.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_cache.h":
            "ui_map_runtime/map_tiles/map_tile_cache.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_decoder_cache.h":
            "ui_map_runtime/map_tiles/map_tile_decoder_cache.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_render_queue.h":
            "ui_map_runtime/map_tiles/map_tile_render_queue.h",
        "modules/ui_shared/include/ui/map_tiles/legacy_filesystem_map_tile_source.h":
            "ui_map_runtime/map_tiles/filesystem_map_tile_source.h",
        "modules/ui_shared/include/ui/map_overlay/map_overlay_projector.h":
            "ui_map_runtime/map_overlay/map_overlay_projector.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_event_bridge.h":
            "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h":
            "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h":
            "ui_legacy_adapters/legacy_key_verification_session.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h":
            "ui_legacy_adapters/legacy_key_verification_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h":
            "ui_legacy_adapters/legacy_key_verification_action_sink.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h":
            "ui_legacy_adapters/legacy_map_overlay_source.h",
        "modules/ui_shared/include/ui/screens/chat/team_position_picker_renderer.h":
            "ui_lvgl_ux_packs/common/team_position_picker_renderer.h",
        "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h":
            "ui_lvgl_ux_packs/common/key_verification_modal_renderer.h",
    }

    failures = 0
    for path, include_token in expected.items():
        if not exists(path):
            failures += fail(f"missing forwarding header: {path}")
            continue
        text = read_text(path)
        if include_token not in text:
            failures += fail(f"{path} does not forward to {include_token}")
        if "class " in text or "struct " in text or "namespace " in text:
            failures += fail(f"{path} must be a forwarding header only")

    forbidden_sources = [
        "modules/ui_shared/src/ui/screens/chat/chat_page_runtime_event_pump.cpp",
        "modules/ui_shared/src/ui/screens/gps/gps_page_runtime_pump.cpp",
        "modules/ui_shared/src/ui/map_tiles/map_tile_resolver.cpp",
        "modules/ui_shared/src/ui/map_tiles/map_tile_render_queue.cpp",
        "modules/ui_shared/src/ui/map_tiles/legacy_filesystem_map_tile_source.cpp",
        "modules/ui_shared/src/ui/map_overlay/map_overlay_projector.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_event_bridge.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_chat_delivery_action_bridge.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_source.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_key_verification_action_sink.cpp",
        "modules/ui_shared/src/ui/presentation_sources/legacy_map_overlay_source.cpp",
        "modules/ui_shared/src/ui/screens/chat/team_position_picker_renderer.cpp",
        "modules/ui_shared/src/ui/screens/chat/key_verification_modal_renderer.cpp",
    ]
    for path in forbidden_sources:
        if exists(path):
            failures += fail(f"old implementation path still exists: {path}")

    return failures


def check_authoritative_include_paths() -> int:
    old_include_replacements = {
        "ui/screens/chat/chat_page_runtime_event_pump.h":
            "ui_chat_runtime/chat_page_runtime_event_pump.h",
        "ui/screens/chat/chat_ui_refresh_sink.h":
            "ui_chat_runtime/chat_ui_refresh_sink.h",
        "ui/screens/gps/gps_page_runtime_pump.h":
            "ui_gps_runtime/gps_page_runtime_pump.h",
        "ui/screens/gps/gps_ui_refresh_sink.h":
            "ui_gps_runtime/gps_ui_refresh_sink.h",
        "ui/map_tiles/map_tile_types.h":
            "ui_map_runtime/map_tiles/map_tile_types.h",
        "ui/map_tiles/map_tile_resolver.h":
            "ui_map_runtime/map_tiles/map_tile_resolver.h",
        "ui/map_tiles/map_tile_source.h":
            "ui_map_runtime/map_tiles/map_tile_source.h",
        "ui/map_tiles/map_tile_cache.h":
            "ui_map_runtime/map_tiles/map_tile_cache.h",
        "ui/map_tiles/map_tile_decoder_cache.h":
            "ui_map_runtime/map_tiles/map_tile_decoder_cache.h",
        "ui/map_tiles/map_tile_render_queue.h":
            "ui_map_runtime/map_tiles/map_tile_render_queue.h",
        "ui/map_tiles/legacy_filesystem_map_tile_source.h":
            "ui_map_runtime/map_tiles/filesystem_map_tile_source.h",
        "ui/map_overlay/map_overlay_projector.h":
            "ui_map_runtime/map_overlay/map_overlay_projector.h",
        "ui/presentation_sources/legacy_chat_delivery_event_bridge.h":
            "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h",
        "ui/presentation_sources/legacy_chat_delivery_action_bridge.h":
            "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h",
        "ui/presentation_sources/legacy_key_verification_session.h":
            "ui_legacy_adapters/legacy_key_verification_session.h",
        "ui/presentation_sources/legacy_key_verification_source.h":
            "ui_legacy_adapters/legacy_key_verification_source.h",
        "ui/presentation_sources/legacy_key_verification_action_sink.h":
            "ui_legacy_adapters/legacy_key_verification_action_sink.h",
        "ui/presentation_sources/legacy_map_overlay_source.h":
            "ui_legacy_adapters/legacy_map_overlay_source.h",
        "ui/screens/chat/team_position_picker_renderer.h":
            "ui_lvgl_ux_packs/common/team_position_picker_renderer.h",
        "ui/screens/chat/key_verification_modal_renderer.h":
            "ui_lvgl_ux_packs/common/key_verification_modal_renderer.h",
    }

    forwarding_headers = {
        "modules/ui_shared/include/ui/screens/chat/chat_page_runtime_event_pump.h",
        "modules/ui_shared/include/ui/screens/chat/chat_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/screens/gps/gps_page_runtime_pump.h",
        "modules/ui_shared/include/ui/screens/gps/gps_ui_refresh_sink.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_types.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_resolver.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_source.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_cache.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_decoder_cache.h",
        "modules/ui_shared/include/ui/map_tiles/map_tile_render_queue.h",
        "modules/ui_shared/include/ui/map_tiles/legacy_filesystem_map_tile_source.h",
        "modules/ui_shared/include/ui/map_overlay/map_overlay_projector.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_event_bridge.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h",
        "modules/ui_shared/include/ui/screens/chat/team_position_picker_renderer.h",
        "modules/ui_shared/include/ui/screens/chat/key_verification_modal_renderer.h",
    }

    failures = 0
    for root_name in ["apps", "modules", "platform", "boards"]:
        for path in iter_code_files(ROOT / root_name):
            rel = path.relative_to(ROOT).as_posix()
            if rel in forwarding_headers:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if "PHASE8_COMPATIBILITY_INCLUDE_TEST" in text:
                continue
            for line in text.splitlines():
                stripped = line.strip()
                if not stripped.startswith("#include"):
                    continue
                for old_include, replacement in old_include_replacements.items():
                    if f'"{old_include}"' in stripped or f"<{old_include}>" in stripped:
                        failures += fail(
                            f"{rel} includes compatibility path {old_include}; use {replacement}"
                        )

    return failures


def check_build_manifest_authority() -> int:
    failures = 0
    common_include_tokens = [
        "modules/ui_chat_runtime/include",
        "modules/ui_map_runtime/include",
        "modules/ui_gps_runtime/include",
        "modules/ui_legacy_adapters/include",
        "modules/ui_lvgl_core/include",
        "modules/ui_lvgl_ux_packs/include",
    ]

    for manifest in [
        "platformio.ini",
        "modules/ui_shared/library.json",
        "apps/esp_pio/library.json",
        "apps/gat562_mesh_evb_pro/library.json",
    ]:
        failures += check_tokens(manifest, common_include_tokens, "new module include authority")

    failures += check_tokens(
        "apps/esp_pio/library.json",
        [
            '"name":  "ui_chat_runtime"',
            '"name":  "ui_map_runtime"',
            '"name":  "ui_gps_runtime"',
            '"name":  "ui_legacy_adapters"',
            '"name":  "ui_lvgl_core"',
            '"name":  "ui_lvgl_ux_packs"',
        ],
        "ESP PIO new module dependency",
    )

    failures += check_tokens(
        "apps/esp_idf/CMakeLists.txt",
        [
            "modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp",
            "modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp",
            "modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp",
            "modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp",
            "modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp",
            "modules/ui_legacy_adapters/src/legacy_chat_delivery_event_bridge.cpp",
            "modules/ui_legacy_adapters/src/legacy_key_verification_source.cpp",
            "modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp",
            "modules/ui_lvgl_core/include",
            "modules/ui_lvgl_ux_packs/src/common/team_position_picker_renderer.cpp",
            "modules/ui_lvgl_ux_packs/src/common/key_verification_modal_renderer.cpp",
        ],
        "ESP-IDF new module authority",
    )

    failures += check_tokens(
        "cmake/TrailMateLinuxSources.cmake",
        [
            "TRAIL_MATE_UI_CHAT_RUNTIME_INCLUDE_ROOT",
            "TRAIL_MATE_UI_CHAT_RUNTIME_SRC_ROOT",
            "TRAIL_MATE_UI_MAP_RUNTIME_INCLUDE_ROOT",
            "TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT",
            "TRAIL_MATE_UI_GPS_RUNTIME_INCLUDE_ROOT",
            "TRAIL_MATE_UI_GPS_RUNTIME_SRC_ROOT",
            "TRAIL_MATE_UI_LEGACY_ADAPTERS_INCLUDE_ROOT",
            "TRAIL_MATE_UI_LEGACY_ADAPTERS_SRC_ROOT",
            "TRAIL_MATE_UI_LVGL_CORE_INCLUDE_ROOT",
            "TRAIL_MATE_UI_LVGL_UX_PACKS_INCLUDE_ROOT",
            "TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT",
            "chat_page_runtime_event_pump.cpp",
            "filesystem_map_tile_source.cpp",
            "gps_page_runtime_pump.cpp",
            "team_position_picker_renderer.cpp",
            "key_verification_modal_renderer.cpp",
        ],
        "Linux CMake new module authority",
    )

    return failures


def check_runtime_module_boundaries() -> int:
    failures = 0
    forbidden_by_root = {
        "modules/ui_chat_runtime": [
            "lvgl.h",
            "apps/",
            "apps\\",
            "builds/",
            "boards/",
            "boards\\",
        ],
        "modules/ui_gps_runtime": [
            "lvgl.h",
            "apps/",
            "apps\\",
            "builds/",
            "boards/",
            "boards\\",
        ],
        "modules/ui_map_runtime": [
            "lvgl.h",
            "apps/",
            "apps\\",
            "builds/",
            "boards/",
            "boards\\",
        ],
        "modules/ui_legacy_adapters": [
            "apps/",
            "apps\\",
            "builds/",
            "boards/",
            "boards\\",
        ],
        "modules/ui_lvgl_ux_packs": [
            "apps/",
            "apps\\",
            "builds/",
            "boards/",
            "boards\\",
            "platform/",
            "platform\\",
        ],
    }

    for root_name, forbidden_tokens in forbidden_by_root.items():
        for path in iter_code_files(ROOT / root_name):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in forbidden_tokens:
                if token in text:
                    failures += fail(
                        f"{path.relative_to(ROOT)} contains forbidden boundary token {token}"
                    )

    filesystem_header = "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/filesystem_map_tile_source.h"
    if exists(filesystem_header):
        text = read_text(filesystem_header)
        if "class FilesystemMapTileSource" not in text:
            failures += fail("FilesystemMapTileSource class is missing")
        if "using LegacyFilesystemMapTileSource" not in text:
            failures += fail("LegacyFilesystemMapTileSource compatibility alias is missing")
        if "[[deprecated" not in text:
            failures += fail("LegacyFilesystemMapTileSource alias must be deprecated")
        if "FilesystemMapTileSource;" not in text:
            failures += fail("LegacyFilesystemMapTileSource must alias FilesystemMapTileSource")

    for path in iter_code_files(ROOT / "modules/ui_map_runtime"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "LegacyFilesystemMapTileSource" in text and path.as_posix().endswith(
            "filesystem_map_tile_source.h"
        ):
            continue
        if "LegacyFilesystemMapTileSource" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} uses deprecated LegacyFilesystemMapTileSource"
            )

    ui_presentation_root = ROOT / "modules/ui_presentation"
    for path in iter_code_files(ui_presentation_root):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for token in ["lvgl.h", "platform/", "platform\\", "filesystem", "FILE*"]:
            if token in text:
                failures += fail(
                    f"{path.relative_to(ROOT)} contains forbidden portable presentation token {token}"
                )

    return failures


def check_deprecated_alias_usage() -> int:
    allowed = {
        "modules/ui_map_runtime/include/ui_map_runtime/map_tiles/filesystem_map_tile_source.h",
    }

    failures = 0
    for root_name in ["apps", "modules", "platform", "boards"]:
        for path in iter_code_files(ROOT / root_name):
            rel = path.relative_to(ROOT).as_posix()
            text = path.read_text(encoding="utf-8", errors="ignore")
            if "LegacyFilesystemMapTileSource" not in text:
                continue
            if rel in allowed:
                continue
            if "PHASE8_LEGACY_ALIAS_COMPATIBILITY_TEST" in text:
                continue
            failures += fail(
                f"{rel} uses deprecated LegacyFilesystemMapTileSource alias; use FilesystemMapTileSource"
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
    failures += check_app_shell_language()
    failures += check_ux_profile_language()
    failures += check_build_entrypoint_language()
    failures += check_forwarding_headers()
    failures += check_authoritative_include_paths()
    failures += check_build_manifest_authority()
    failures += check_runtime_module_boundaries()
    failures += check_deprecated_alias_usage()
    failures += check_transitional_app_markers()

    if failures:
        return 1

    print("[phase8-layout-ready] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
