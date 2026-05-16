#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def exists(path: str) -> bool:
    return (ROOT / path).exists()


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="ignore")


def fail(message: str) -> int:
    print(f"[phase8-layout-ready] FAIL: {message}")
    return 1


def check_post_root_legacy_removed() -> int:
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/architecture/check_no_root_legacy_ready.py")],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        return fail(f"post-root legacy removal check failed:\n{output}")
    print("[phase8-layout-ready] OK (root legacy removed)")
    return 0


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
        "docs/audits/PHASE8_RUNTIME_UI_ADOPTION_REPORT.md",
        "docs/specification/APP_SHELL_ARCHITECTURE_SPEC.md",
        "docs/audits/APP_SHELL_CURRENT_STATE_AUDIT.md",
        "legacy/LEGACY_GOVERNANCE.md",
        "legacy/app_implementations/LEGACY_IMPLEMENTATION_INDEX.md",
        "cmake/TrailMateUxPacks.cmake",
        "builds/README.md",
        "builds/esp_idf/README.md",
        "builds/esp_idf/CMakeLists.txt",
        "builds/pio_nrf52/README.md",
        "builds/pio_nrf52/platformio.ini",
        "builds/pio_nrf52/src/nrf52_node_wrapper_baseline.cpp",
        "builds/linux_cmake/README.md",
        "builds/linux_cmake/CMakeLists.txt",
        "apps/esp32_lvgl/README.md",
        "apps/esp32_lvgl/APP_SHELL_MANIFEST.md",
        "apps/esp32_lvgl/CMakeLists.txt",
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.h",
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp",
        "apps/esp32_lvgl/tests/esp32_lvgl_app_shell_smoke.cpp",
        "apps/nrf52_node/README.md",
        "apps/nrf52_node/APP_SHELL_MANIFEST.md",
        "apps/nrf52_node/library.json",
        "apps/nrf52_node/src/nrf52_node_app_shell.h",
        "apps/nrf52_node/src/nrf52_node_app_shell.cpp",
        "apps/nrf52_node/tests/nrf52_node_app_shell_smoke.cpp",
        "apps/linux_uconsole_gtk/README.md",
        "apps/linux_uconsole_gtk/APP_SHELL_MANIFEST.md",
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.cpp",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h",
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_app_shell_smoke.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_historical_source_descriptor_smoke.cpp",
        "apps/linux_sim_shell/README.md",
        "apps/linux_sim_shell/APP_SHELL_MANIFEST.md",
        "apps/linux_sim_shell/CMakeLists.txt",
        "apps/linux_sim_shell/src/linux_sim_app_shell.h",
        "apps/linux_sim_shell/src/linux_sim_app_shell.cpp",
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h",
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.cpp",
        "apps/linux_sim_shell/tests/linux_sim_app_shell_smoke.cpp",
        "apps/linux_sim_shell/tests/linux_sim_historical_source_descriptor_smoke.cpp",
        "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.h",
        "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.cpp",
        "legacy/app_implementations/linux_uconsole/TRANSITIONAL_IMPLEMENTATION_ROOT.md",
        "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.h",
        "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.cpp",
        "legacy/app_implementations/linux_sim/TRANSITIONAL_IMPLEMENTATION_ROOT.md",
        "legacy/app_implementations/esp_idf/src/esp_idf_legacy_implementation_adapter.h",
        "legacy/app_implementations/esp_idf/src/esp_idf_legacy_implementation_adapter.cpp",
        "legacy/app_implementations/esp_idf/TRANSITIONAL_IMPLEMENTATION_ROOT.md",
        "legacy/app_implementations/esp_pio/src/nrf52_pio_legacy_implementation_adapter.h",
        "legacy/app_implementations/esp_pio/TRANSITIONAL_IMPLEMENTATION_ROOT.md",
        "legacy/app_implementations/gat562_mesh_evb_pro/TRANSITIONAL_IMPLEMENTATION_ROOT.md",
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
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_port.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_action_port_adapter.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_delivery_event_projection_adapter.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_page_runtime_event_pump.h",
        "modules/ui_chat_runtime/include/ui_chat_runtime/chat_ui_refresh_sink.h",
        "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp",
        "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp",
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
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_projection_adapter.h",
        "modules/ui_map_runtime/include/ui_map_runtime/map_overlay_snapshot_source.h",
        "modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp",
        "modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp",
        "modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp",
        "modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp",
        "modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp",
        "modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp",
        "modules/ui_map_runtime/tests/test_map_tile_resolver.cpp",
        "modules/ui_map_runtime/tests/test_map_tile_render_queue.cpp",
        "modules/ui_map_runtime/tests/test_filesystem_map_tile_source.cpp",
        "modules/ui_map_runtime/tests/test_map_overlay_projector.cpp",
        "modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp",
        "modules/ui_gps_runtime/README.md",
        "modules/ui_gps_runtime/library.json",
        "modules/ui_gps_runtime/include/ui_gps_runtime/gps_page_runtime_pump.h",
        "modules/ui_gps_runtime/include/ui_gps_runtime/gps_ui_refresh_sink.h",
        "modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp",
        "modules/ui_gps_runtime/tests/test_gps_page_runtime_pump.cpp",
        "modules/ui_key_verification_runtime/README.md",
        "modules/ui_key_verification_runtime/library.json",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_session_adapter.h",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_presentation_source.h",
        "modules/ui_key_verification_runtime/include/ui_key_verification_runtime/key_verification_action_sink.h",
        "modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp",
        "modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp",
        "modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp",
        "modules/ui_legacy_adapters/README.md",
        "modules/ui_legacy_adapters/library.json",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_event_bridge.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h",
        "modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h",
        "modules/ui_chat_runtime/tests/test_chat_delivery_event_projection_adapter.cpp",
        "modules/ui_chat_runtime/tests/test_chat_delivery_action_port_adapter.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp",
        "modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source_legacy_alias.cpp",
        "modules/ui_lvgl_core/README.md",
        "modules/ui_lvgl_core/library.json",
        "modules/ui_lvgl_core/include/ui_lvgl_core/lvgl_focus_group.h",
        "modules/ui_lvgl_core/include/ui_lvgl_core/.gitkeep",
        "modules/ui_lvgl_core/src/.gitkeep",
        "modules/ui_presentation/library.json",
        "modules/ui_presentation/include/ui_presentation/menu/menu_model.h",
        "modules/ui_presentation/src/menu/menu_model.cpp",
        "modules/ui_presentation/include/ui_presentation/screen/screen_route.h",
        "modules/ui_presentation/include/ui_presentation/screen/screen_binding_registry.h",
        "modules/ui_presentation/src/screen/screen_binding_registry.cpp",
        "modules/ui_presentation/tests/test_screen_binding_registry.cpp",
        "modules/ui_lvgl_ux_packs/README.md",
        "modules/ui_lvgl_ux_packs/library.json",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/common/team_position_picker_renderer.h",
        "modules/ui_lvgl_ux_packs/src/common/team_position_picker_renderer.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/common/key_verification_modal_renderer.h",
        "modules/ui_lvgl_ux_packs/src/common/key_verification_modal_renderer.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/device_ux_profile.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_feature_set.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/screen_registry.h",
        "modules/ui_lvgl_ux_packs/src/ux/screen_registry.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/input_binding_set.h",
        "modules/ui_lvgl_ux_packs/src/ux/input_binding_set.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_model.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_menu_model.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_provider.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_menu_runtime_adapter.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_menu_runtime_adapter.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h",
        "modules/ui_lvgl_ux_packs/src/runtime/compatibility_screen_factory.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_host_adapter.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_pack.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_pack_registry.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_pack_registry.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/packs/compatibility_ux_pack.h",
        "modules/ui_lvgl_ux_packs/src/packs/compatibility_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/packs/uconsole_desktop_ux_pack.h",
        "modules/ui_lvgl_ux_packs/src/packs/uconsole_desktop_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/packs/tiny_node_status_ux_pack.h",
        "modules/ui_lvgl_ux_packs/src/packs/tiny_node_status_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/packs/simulator_full_ux_pack.h",
        "modules/ui_lvgl_ux_packs/src/packs/simulator_full_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_screen_registry.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_input_binding_set.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_ux_screen_menu_adapter.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_ux_menu_provider.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_menu_runtime_adapter.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_compatibility_screen_factory.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_screen_host_adapter.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_screen_graph_bridge.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_compatibility_menu_binding.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_compatibility_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_uconsole_desktop_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_tiny_node_status_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_simulator_full_ux_pack.cpp",
        "modules/ui_lvgl_ux_packs/tests/test_ux_pack_registry.cpp",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_menu_runtime_adapter.h",
        "modules/ui_ascii_runtime/src/ascii_menu_runtime_adapter.cpp",
        "modules/ui_ascii_runtime/tests/ascii_menu_runtime_adapter_smoke.cpp",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_host_adapter.h",
        "modules/ui_ascii_runtime/src/ascii_screen_host_adapter.cpp",
        "modules/ui_ascii_runtime/tests/ascii_screen_host_adapter_smoke.cpp",
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_graph_bridge.h",
        "modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp",
        "modules/ui_ascii_runtime/tests/ascii_screen_graph_bridge_smoke.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_menu_runtime_adapter.h",
        "modules/ui_gtk_runtime/src/gtk_menu_runtime_adapter.cpp",
        "modules/ui_gtk_runtime/tests/gtk_menu_runtime_adapter_smoke.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_screen_host_adapter.h",
        "modules/ui_gtk_runtime/src/gtk_screen_host_adapter.cpp",
        "modules/ui_gtk_runtime/tests/gtk_screen_host_adapter_smoke.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp",
        "modules/ui_gtk_runtime/tests/gtk_uconsole_screen_graph_bridge_smoke.cpp",
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
            "`legacy/app_implementations/esp_idf` and `legacy/app_implementations/esp_pio` remain transitional",
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
            "legacy/app_implementations/esp_idf",
            "legacy/app_implementations/esp_pio",
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
            "legacy/app_implementations/esp_idf",
            "legacy/app_implementations/esp_pio",
            "legacy/app_implementations/linux_sim",
            "legacy/app_implementations/linux_uconsole",
            "legacy/app_implementations/linux_rpi",
            "legacy/app_implementations/linux_unoq",
            "legacy/app_implementations/gat562_mesh_evb_pro",
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
            "executable app shell baseline",
            "Phase 8 Build/AppShell Executable Convergence",
            "trailmate_esp32_lvgl_app_shell",
            "trailmate-nrf52-node-app-shell",
            "trailmate_linux_uconsole_gtk_shell",
            "trailmate_linux_sim_shell",
            "esp32_lvgl_historical_source_descriptor",
            "ESP_IDF_COMPONENT_SOURCES.cmake",
            "trailmate_nrf52_pio_legacy_adapter",
            "TRANSITIONAL_IMPLEMENTATION_ROOT.md",
        ],
        "app shell audit",
    )

    shell_docs = {
        "apps/esp32_lvgl/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/esp_idf`",
            "Historical source identity = `legacy/app_implementations/esp_idf`",
            "must not",
            "own build host files",
            "src/esp32_lvgl_app_shell.h",
            "target_family = esp32_lvgl",
            "default_ux_pack_id = compatibility",
            "historical_root_name = legacy/app_implementations/esp_idf",
            "component_sources = builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake",
            "src/esp32_lvgl_startup_runtime.*",
            "src/esp32_lvgl_loop_runtime.*",
            "src/esp32_lvgl_runtime_config.*",
        ],
        "apps/esp32_lvgl/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "ESP / ESP32-P4",
            "LVGL",
            "`builds/esp_idf`",
            "`legacy/app_implementations/esp_idf`",
            "own build host files",
            "Executable app shell baseline",
            "esp32_lvgl_app_shell.cpp",
            "Default UX Pack",
            "`compatibility`",
            "esp32_lvgl_startup_runtime.*",
            "esp32_lvgl_loop_runtime.*",
            "esp32_lvgl_runtime_config.*",
            "ESP_IDF_COMPONENT_SOURCES.cmake",
        ],
        "apps/nrf52_node/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/pio_nrf52`",
            "Historical source identity = `legacy/app_implementations/esp_pio`",
            "legacy/app_implementations/gat562_mesh_evb_pro",
            "own build host files",
            "src/nrf52_node_app_shell.h",
            "target_family = nrf52_node",
            "default_ux_pack_id = tiny_node_status",
            "historical_generic_root_name = legacy/app_implementations/esp_pio",
            "historical_board_root_name = legacy/app_implementations/gat562_mesh_evb_pro",
            "replacement_owner = apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro",
            "No behavior change in Phase 8 Build/AppShell Executable Convergence",
        ],
        "apps/nrf52_node/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "nRF52",
            "`builds/pio_nrf52`",
            "`legacy/app_implementations/esp_pio`",
            "`legacy/app_implementations/gat562_mesh_evb_pro`",
            "own build host files",
            "Executable app shell baseline",
            "nrf52_node_app_shell.cpp",
            "Default UX Pack",
            "`tiny_node_status`",
            "nrf52_historical_source_descriptor",
            "No behavior change in Phase 8 Build/AppShell Executable Convergence",
        ],
        "apps/linux_uconsole_gtk/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/linux_cmake`",
            "Historical source identity = `legacy/app_implementations/linux_uconsole`",
            "own build host files",
            "src/linux_uconsole_gtk_app_shell.h",
            "target_id = uconsole",
            "ux_pack_id = uconsole_desktop",
            "historical_source = legacy/app_implementations/linux_uconsole",
            "No GTK runtime behavior changes in Phase 8 Correction",
        ],
        "apps/linux_uconsole_gtk/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "Linux uConsole",
            "GTK",
            "`builds/linux_cmake`",
            "`legacy/app_implementations/linux_uconsole`",
            "own build host files",
            "Executable app shell baseline",
            "linux_uconsole_gtk_app_shell.cpp",
            "Selected UX Pack",
            "`uconsole_desktop`",
            "No GTK runtime behavior change in Phase 8 Correction",
        ],
        "apps/linux_sim_shell/README.md": [
            "Role = product app shell / target app shell",
            "Build entrypoint = `builds/linux_cmake`",
            "Historical source identity = `legacy/app_implementations/linux_sim`",
            "own build host files",
            "src/linux_sim_app_shell.h",
            "target_id = linux_sim",
            "ux_pack_id = simulator_full",
            "historical_source = legacy/app_implementations/linux_sim",
            "No simulator runtime behavior changes in Phase 8 Correction",
        ],
        "apps/linux_sim_shell/APP_SHELL_MANIFEST.md": [
            "Product app shell / target app shell",
            "Linux simulator",
            "`builds/linux_cmake`",
            "`legacy/app_implementations/linux_sim`",
            "own build host files",
            "Executable app shell baseline",
            "linux_sim_app_shell.cpp",
            "Selected UX Pack",
            "`simulator_full`",
            "No simulator runtime behavior change in Phase 8 Correction",
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
            "legacy/app_implementations/esp_pio",
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
        "legacy/app_implementations/esp_idf",
        "legacy/app_implementations/esp_pio",
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
            "legacy/app_implementations/linux_sim",
            "legacy/app_implementations/linux_uconsole",
            "legacy/app_implementations/linux_rpi",
            "legacy/app_implementations/linux_unoq",
            "legacy/app_implementations/gat562_mesh_evb_pro",
            "root `build/`",
            "build.cardputer",
            "build.tab5",
            "BuildEntrypoint Constraints",
            "No build behavior changes in this phase",
            "Phase 8 Correction",
            "Linux Executable Wrapper Baseline",
            "executable wrapper baseline",
            "trailmate_linux_uconsole_gtk_shell",
            "trailmate_linux_sim_shell",
            "Phase 8 Build/AppShell Executable Convergence",
            "builds/esp_idf",
            "builds/pio_nrf52",
            "trailmate_esp32_lvgl_app_shell",
            "trailmate-nrf52-node-app-shell",
            "linux_uconsole_gtk_historical_source_descriptor",
            "linux_sim_historical_source_descriptor",
            "ESP_IDF_COMPONENT_SOURCES.cmake",
            "trailmate_nrf52_pio_legacy_adapter",
            "Transitional Adapter delegates",
            "Legacy implementation still runs",
            "TRANSITIONAL_IMPLEMENTATION_ROOT.md",
        ],
        "build entrypoint audit",
    )

    failures += check_tokens(
        "docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md",
        [
            "legacy/app_implementations/esp_idf",
            "legacy/app_implementations/esp_pio",
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
            "legacy/app_implementations/esp_idf",
            "legacy/app_implementations/esp_pio",
        ],
        "builds README",
    )

    failures += check_tokens(
        "builds/esp_idf/README.md",
        [
            "Authoritative ESP build entrypoint",
            "ESP / ESP32-P4 -> ESP-IDF",
            "Current transitional path",
            "legacy/app_implementations/esp_idf",
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
            "legacy/app_implementations/esp_pio",
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
            "legacy/app_implementations/linux_sim",
            "legacy/app_implementations/linux_uconsole",
            "legacy/app_implementations/linux_rpi",
            "legacy/app_implementations/linux_unoq",
            "selected Linux app shell",
            "thin wrapper",
            "Build Entrypoint invokes",
            "App Shell composes",
        ],
        "Linux CMake build README",
    )

    return failures


def check_executable_layout_convergence() -> int:
    failures = 0

    failures += check_tokens(
        "builds/linux_cmake/CMakeLists.txt",
        [
            "Build Entrypoint invokes; App Shell composes.",
            "TRAIL_MATE_BUILD_LINUX_UCONSOLE_GTK",
            "TRAIL_MATE_BUILD_LINUX_SIM_SHELL",
            "add_subdirectory",
            "apps/linux_uconsole_gtk",
            "apps/linux_sim_shell",
        ],
        "Linux executable build wrapper",
    )

    wrapper_text = read_text("builds/linux_cmake/CMakeLists.txt")
    for token in [
        "ChatService",
        "MapWorkspaceModel",
        "GpsStatusModel",
        "trailmate_add_linux_common",
        "TrailMateLinuxSources.cmake",
    ]:
        if token in wrapper_text:
            failures += fail(f"builds/linux_cmake/CMakeLists.txt owns forbidden token: {token}")

    failures += check_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        [
            "TrailMateUxPacks.cmake",
            "trailmate_ui_lvgl_ux_packs",
            "trailmate_linux_uconsole_gtk_shell",
            "linux_uconsole_gtk_app_shell.cpp",
            "linux_uconsole_gtk_historical_source_descriptor.cpp",
            "linux_uconsole_gtk_app_shell_smoke.cpp",
            "linux_uconsole_gtk_historical_source_descriptor_smoke.cpp",
            "add_test",
        ],
        "uConsole GTK app shell CMake",
    )

    failures += check_tokens(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.h",
        [
            "LinuxUConsoleGtkAppShellConfig",
            "target_id = \"uconsole\"",
            "ux_pack_id = \"uconsole_desktop\"",
            "linux_uconsole_gtk_historical_source_descriptor.h",
            "linuxUConsoleGtkHistoricalSourceDescriptor().historical_root_name",
            "LinuxUConsoleGtkAppShell",
        ],
        "uConsole GTK app shell header",
    )

    failures += check_tokens(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.cpp",
        [
            "LinuxUConsoleGtkAppShell::config",
            "LinuxUConsoleGtkAppShell::validate",
            "ui_lvgl_ux_packs/ux/ux_pack_registry.h",
            "findUxPackById",
            "config_.target_id",
            "config_.ux_pack_id",
            "config_.historical_source",
            "historical_source.historical_root_name",
            "historical_source.replacement_owner",
        ],
        "uConsole GTK app shell source",
    )

    failures += check_tokens(
        "apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h",
        [
            "LinuxUConsoleGtkHistoricalSourceDescriptor",
            "historical_root_name = \"legacy/app_implementations/linux_uconsole\"",
            "historical_role = \"pre-refactor uConsole GTK implementation root\"",
            "replacement_owner = \"apps/linux_uconsole_gtk + modules/ui_gtk_runtime\"",
        ],
        "uConsole GTK historical source descriptor",
    )

    failures += check_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "TrailMateUxPacks.cmake",
            "trailmate_ui_lvgl_ux_packs",
            "trailmate_linux_sim_shell",
            "linux_sim_app_shell.cpp",
            "linux_sim_historical_source_descriptor.cpp",
            "linux_sim_app_shell_smoke.cpp",
            "linux_sim_historical_source_descriptor_smoke.cpp",
            "ux_pack_registry",
            "add_test",
        ],
        "Linux sim app shell CMake",
    )

    failures += check_tokens(
        "apps/linux_sim_shell/src/linux_sim_app_shell.h",
        [
            "LinuxSimAppShellConfig",
            "target_id = \"linux_sim\"",
            "ux_pack_id = \"simulator_full\"",
            "linux_sim_historical_source_descriptor.h",
            "linuxSimHistoricalSourceDescriptor().historical_root_name",
            "LinuxSimAppShell",
        ],
        "Linux sim app shell header",
    )

    failures += check_tokens(
        "apps/linux_sim_shell/src/linux_sim_app_shell.cpp",
        [
            "LinuxSimAppShell::config",
            "LinuxSimAppShell::validate",
            "ui_lvgl_ux_packs/ux/ux_pack_registry.h",
            "findUxPackById",
            "config_.target_id",
            "config_.ux_pack_id",
            "config_.historical_source",
            "historical_source.historical_root_name",
            "historical_source.replacement_owner",
        ],
        "Linux sim app shell source",
    )

    failures += check_tokens(
        "apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h",
        [
            "LinuxSimHistoricalSourceDescriptor",
            "historical_root_name = \"legacy/app_implementations/linux_sim\"",
            "historical_role = \"pre-refactor Linux simulator implementation root\"",
            "replacement_owner = \"apps/linux_sim_shell + modules/ui_ascii_runtime\"",
        ],
        "Linux sim historical source descriptor",
    )

    failures += check_tokens(
        "builds/esp_idf/CMakeLists.txt",
        [
            "Build Entrypoint invokes; App Shell composes.",
            "TRAIL_MATE_ESP_APP_SHELL",
            "apps/esp32_lvgl",
            "add_subdirectory",
        ],
        "ESP-IDF executable build wrapper",
    )

    esp_wrapper_text = read_text("builds/esp_idf/CMakeLists.txt")
    for token in [
        "idf_component_register",
        "trail_mate_idf_ui_shared_sources",
        "ChatService",
        "MapWorkspaceModel",
        "GpsStatusModel",
    ]:
        if token in esp_wrapper_text:
            failures += fail(f"builds/esp_idf/CMakeLists.txt owns forbidden token: {token}")

    failures += check_tokens(
        "apps/esp32_lvgl/CMakeLists.txt",
        [
            "TrailMateUxPacks.cmake",
            "trailmate_ui_lvgl_ux_packs",
            "trailmate_esp32_lvgl_app_shell",
            "esp32_lvgl_app_shell.cpp",
            "esp32_lvgl_historical_source_descriptor.cpp",
            "esp32_lvgl_startup_runtime.cpp",
            "esp32_lvgl_loop_runtime.cpp",
            "esp32_lvgl_runtime_config.cpp",
            "esp32_lvgl_app_shell_smoke.cpp",
            "add_test",
        ],
        "ESP32 LVGL app shell CMake",
    )

    failures += check_tokens(
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.h",
        [
            "Esp32LvglAppShellConfig",
            "target_family = \"esp32_lvgl\"",
            "default_ux_pack_id = \"compatibility\"",
            "build_entrypoint = \"builds/esp_idf\"",
            "component_sources = \"builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake\"",
            "historical_root_name =",
            "esp32LvglHistoricalSourceDescriptor().historical_root_name",
            "Esp32LvglAppShell",
        ],
        "ESP32 LVGL app shell header",
    )

    failures += check_tokens(
        "apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp",
        [
            "Esp32LvglAppShell::config",
            "Esp32LvglAppShell::validate",
            "ui_lvgl_ux_packs/ux/ux_pack_registry.h",
            "findUxPackById",
            "config_.target_family",
            "config_.default_ux_pack_id",
            "config_.build_entrypoint",
            "config_.component_sources",
            "config_.historical_root_name",
            "config_.historical_role",
            "config_.replacement_owner",
        ],
        "ESP32 LVGL app shell source",
    )

    failures += check_tokens(
        "legacy/app_implementations/esp_idf/src/esp_idf_legacy_implementation_adapter.h",
        [
            "EspIdfLegacyImplementationDescriptor",
            "implementation_root = \"legacy/app_implementations/esp_idf\"",
            "app_shell = \"apps/esp32_lvgl\"",
            "build_wrapper = \"builds/esp_idf\"",
            "EspIdfLegacyImplementationAdapter",
        ],
        "ESP-IDF legacy adapter header",
    )

    failures += check_tokens(
        "builds/pio_nrf52/platformio.ini",
        [
            "Build Entrypoint invokes",
            "App Shell composes",
            "apps/nrf52_node",
            "legacy/app_implementations/esp_pio",
            "legacy/app_implementations/gat562_mesh_evb_pro",
            "env:nrf52_node_wrapper_baseline",
            "symlink://../../apps/nrf52_node",
            "symlink://../../modules/ui_lvgl_ux_packs",
            "TRAIL_MATE_BUILD_WRAPPER_BASELINE",
            "modules/ui_lvgl_ux_packs/include",
            "nrf52_node_wrapper_baseline.cpp",
        ],
        "PIO nRF52 executable build wrapper",
    )

    failures += check_tokens(
        "builds/pio_nrf52/src/nrf52_node_wrapper_baseline.cpp",
        [
            "nrf52_node_app_shell.h",
            "nrf52_historical_source_descriptor.h",
            "Nrf52NodeAppShell",
            "nrf52HistoricalSourceDescriptor",
            "shell.validate()",
        ],
        "PIO nRF52 wrapper source",
    )

    failures += check_tokens(
        "apps/nrf52_node/library.json",
        [
            "trailmate-nrf52-node-app-shell",
            "\"includeDir\": \"src\"",
            "\"srcDir\": \"src\"",
            "ui_lvgl_ux_packs",
        ],
        "nRF52 node PIO library manifest",
    )

    failures += check_tokens(
        "apps/nrf52_node/src/nrf52_node_app_shell.h",
        [
            "Nrf52NodeAppShellConfig",
            "target_family = \"nrf52_node\"",
            "default_ux_pack_id = \"tiny_node_status\"",
            "historical_generic_root_name =",
            "nrf52HistoricalSourceDescriptor().historical_generic_root_name",
            "historical_board_root_name =",
            "nrf52HistoricalSourceDescriptor().historical_board_root_name",
            "Nrf52NodeAppShell",
        ],
        "nRF52 node app shell header",
    )

    failures += check_tokens(
        "apps/nrf52_node/src/nrf52_node_app_shell.cpp",
        [
            "Nrf52NodeAppShell::config",
            "Nrf52NodeAppShell::validate",
            "ui_lvgl_ux_packs/ux/ux_pack_registry.h",
            "findUxPackById",
            "config_.target_family",
            "config_.default_ux_pack_id",
            "config_.historical_generic_root_name",
            "config_.historical_board_root_name",
            "config_.historical_role",
            "config_.replacement_owner",
        ],
        "nRF52 node app shell source",
    )

    failures += check_tokens(
        "legacy/app_implementations/esp_pio/src/nrf52_pio_legacy_implementation_adapter.h",
        [
            "Nrf52PioLegacyImplementationDescriptor",
            "implementation_root = \"legacy/app_implementations/esp_pio\"",
            "board_specific_root = \"legacy/app_implementations/gat562_mesh_evb_pro\"",
            "app_shell = \"apps/nrf52_node\"",
            "build_wrapper = \"builds/pio_nrf52\"",
        ],
        "nRF52 PIO legacy adapter header",
    )

    for path, app_shell, wrapper in [
        ("legacy/app_implementations/linux_uconsole/TRANSITIONAL_IMPLEMENTATION_ROOT.md", "apps/linux_uconsole_gtk", "builds/linux_cmake"),
        ("legacy/app_implementations/linux_sim/TRANSITIONAL_IMPLEMENTATION_ROOT.md", "apps/linux_sim_shell", "builds/linux_cmake"),
        ("legacy/app_implementations/esp_idf/TRANSITIONAL_IMPLEMENTATION_ROOT.md", "apps/esp32_lvgl", "builds/esp_idf"),
        ("legacy/app_implementations/esp_pio/TRANSITIONAL_IMPLEMENTATION_ROOT.md", "apps/nrf52_node", "builds/pio_nrf52"),
        ("legacy/app_implementations/gat562_mesh_evb_pro/TRANSITIONAL_IMPLEMENTATION_ROOT.md", "apps/nrf52_node", "builds/pio_nrf52"),
    ]:
        failures += check_tokens(
            path,
            [
                "transitional implementation root",
                "not the final app shell semantic root",
                "Final app shell",
                app_shell,
                "Authoritative build wrapper",
                wrapper,
                "Exit condition",
            ],
            "transitional implementation root marker",
        )

    return failures


def check_ux_pack_runtime_binding() -> int:
    failures = 0

    ux_tokens = {
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/device_ux_profile.h": [
            "DeviceUxProfile",
            "ScreenClass",
            "InputModel",
            "MapMode",
            "ChatMode",
            "supports_team_actions",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_feature_set.h": [
            "UxFeatureSet",
            "chat",
            "contacts",
            "map",
            "gps",
            "team",
            "tracker",
            "settings",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/screen_registry.h": [
            "ScreenRegistry",
            "ScreenDescriptor",
            "ScreenId",
            "kMaxScreens",
            "void clear()",
            "bool add",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/input_binding_set.h": [
            "InputBindingSet",
            "InputBinding",
            "InputAction",
            "kMaxBindings",
            "void clear()",
            "bool add",
        ],
        "modules/ui_presentation/include/ui_presentation/menu/menu_model.h": [
            "MenuModel",
            "MenuItem",
            "MenuScreenId",
            "kMaxItems",
            "void clear()",
            "bool add",
        ],
        "modules/ui_presentation/include/ui_presentation/screen/screen_route.h": [
            "ScreenRoute",
            "ScreenOpenMode",
            "routeForMenuScreen",
            "MenuScreenId",
            "Replace",
            "valid",
        ],
        "modules/ui_presentation/include/ui_presentation/screen/screen_binding_registry.h": [
            "ScreenBindingRegistry",
            "ScreenBinding",
            "MenuScreenId",
            "kMaxBindings",
            "find",
            "bool add",
        ],
        "modules/ui_presentation/src/screen/screen_binding_registry.cpp": [
            "ScreenBindingRegistry",
            "find",
            "screen_id",
            "items_",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_model.h": [
            "ui_presentation/menu/menu_model.h",
            "UxMenuModel",
            "UxMenuItem",
            "using UxMenuModel",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h": [
            "UxScreenMenuAdapter",
            "ScreenRegistry",
            "ui::menu::MenuModel",
            "buildMenu",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_provider.h": [
            "buildMenuForUxPack",
            "ui::menu::MenuModel",
        ],
        "modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp": [
            "UxScreenMenuAdapter",
            "ScreenRegistry",
            "MenuScreenId",
            "toMenuScreenId",
            "screen.enabled",
        ],
        "modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp": [
            "buildMenuForUxPack",
            "findUxPackById",
            "ScreenRegistry",
            "ui::menu::MenuModel",
            "UxScreenMenuAdapter",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_menu_runtime_adapter.h": [
            "LvglMenuRuntimeAdapter",
            "LvglMenuEntry",
            "ui::menu::MenuModel",
            "ScreenRoute",
            "route",
            "buildEntries",
        ],
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_menu_runtime_adapter.cpp": [
            "LvglMenuRuntimeAdapter",
            "buildEntries",
            "MenuItem",
            "routeForMenuScreen",
            "out_count",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h": [
            "CompatibilityScreenFactory",
            "CompatibilityScreenDescriptor",
            "ScreenBindingRegistry",
            "describe",
            "buildBindingsForMenu",
        ],
        "modules/ui_lvgl_ux_packs/src/runtime/compatibility_screen_factory.cpp": [
            "CompatibilityScreenFactory",
            "Dashboard",
            "Chat",
            "Map",
            "Settings",
            "binding_id",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h": [
            "LvglScreenHostAdapter",
            "LvglScreenHostEntry",
            "resolve",
            "binding_id",
        ],
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_host_adapter.cpp": [
            "LvglScreenHostAdapter",
            "CompatibilityScreenFactory",
            "resolve",
            "binding_id",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h": [
            "LvglScreenGraphBridge",
            "PresentationBundle",
            "LvglMenuRuntimeAdapter",
            "LvglScreenHostAdapter",
            "menuCount",
            "screenCount",
        ],
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp": [
            "LvglScreenGraphBridge",
            "PresentationBundle",
            "hasUxMenu",
            "hasScreenBindings",
            "LvglMenuRuntimeAdapter",
            "LvglScreenHostAdapter",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_pack.h": [
            "IUxPack",
            "virtual const char* id() const = 0",
            "DeviceUxProfile",
            "UxFeatureSet",
            "buildScreens",
            "buildInputBindings",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_pack_registry.h": [
            "findUxPackById",
            "IUxPack",
        ],
        "modules/ui_lvgl_ux_packs/src/ux/ux_pack_registry.cpp": [
            "findUxPackById",
            "CompatibilityUxPack",
            "UConsoleDesktopUxPack",
            "TinyNodeStatusUxPack",
            "SimulatorFullUxPack",
            "compatibility",
            "uconsole_desktop",
            "tiny_node_status",
            "simulator_full",
            "nullptr",
        ],
        "modules/ui_lvgl_ux_packs/src/packs/compatibility_ux_pack.cpp": [
            "CompatibilityUxPack",
            "compatibility",
            "ScreenId::Dashboard",
            "ScreenId::Chat",
            "ScreenId::Extensions",
            "InputAction::Compose",
        ],
        "modules/ui_lvgl_ux_packs/src/packs/uconsole_desktop_ux_pack.cpp": [
            "UConsoleDesktopUxPack",
            "uconsole_desktop",
            "ScreenClass::Desktop",
            "InputModel::DesktopKeyboardMouse",
            "MapMode::Desktop",
            "ChatMode::Full",
        ],
        "modules/ui_lvgl_ux_packs/src/packs/tiny_node_status_ux_pack.cpp": [
            "TinyNodeStatusUxPack",
            "tiny_node_status",
            "ScreenClass::TinyStatus",
            "MapMode::None",
            "ChatMode::StatusOnly",
            "ScreenId::Team",
        ],
        "modules/ui_lvgl_ux_packs/src/packs/simulator_full_ux_pack.cpp": [
            "SimulatorFullUxPack",
            "simulator_full",
            "ScreenClass::Desktop",
            "MapMode::Full",
            "ChatMode::Full",
            "ScreenId::Extensions",
        ],
        "cmake/TrailMateUxPacks.cmake": [
            "trailmate_add_ui_lvgl_ux_packs",
            "menu_model.cpp",
            "screen_binding_registry.cpp",
            "screen_registry.cpp",
            "input_binding_set.cpp",
            "ux_menu_model.cpp",
            "ux_screen_menu_adapter.cpp",
            "ux_menu_provider.cpp",
            "ux_pack_registry.cpp",
            "compatibility_screen_factory.cpp",
            "lvgl_menu_runtime_adapter.cpp",
            "lvgl_screen_host_adapter.cpp",
            "lvgl_screen_graph_bridge.cpp",
            "compatibility_ux_pack.cpp",
            "uconsole_desktop_ux_pack.cpp",
            "tiny_node_status_ux_pack.cpp",
            "simulator_full_ux_pack.cpp",
        ],
    }

    for path, tokens in ux_tokens.items():
        failures += check_tokens(path, tokens, "UX Pack runtime binding")

    for path, expected_pack in [
        ("apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp", "default_ux_pack_id"),
        ("apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.cpp", "ux_pack_id"),
        ("apps/linux_sim_shell/src/linux_sim_app_shell.cpp", "ux_pack_id"),
        ("apps/nrf52_node/src/nrf52_node_app_shell.cpp", "default_ux_pack_id"),
    ]:
        failures += check_tokens(
            path,
            [
                "ui_lvgl_ux_packs/ux/ux_pack_registry.h",
                "findUxPackById",
                "activeUxPackId",
                expected_pack,
            ],
            "app shell UX Pack lookup",
        )

    for path, expected_pack_id in [
        ("apps/esp32_lvgl/tests/esp32_lvgl_app_shell_smoke.cpp", "default_ux_pack_id"),
        ("apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_app_shell_smoke.cpp", "ux_pack_id"),
        ("apps/linux_sim_shell/tests/linux_sim_app_shell_smoke.cpp", "ux_pack_id"),
        ("apps/nrf52_node/tests/nrf52_node_app_shell_smoke.cpp", "default_ux_pack_id"),
    ]:
        failures += check_tokens(
            path,
            [
                "findUxPackById",
                "activeUxPackId",
                expected_pack_id,
            ],
            "app shell UX Pack smoke test",
        )

    failures += check_tokens(
        "modules/ui_shared/include/ui/menu/menu_layout.h",
        [
            "ui_presentation/menu/menu_model.h",
            "MenuModel",
            "ux_menu",
        ],
        "existing menu UX model entry point",
    )

    failures += check_tokens(
        "modules/ui_shared/src/ui/menu/menu_layout.cpp",
        [
            "MenuScreenId",
            "stableIdForScreenId",
            "createAppButtonsFromUxMenu",
            "createAppButtonsFromCatalog",
        ],
        "existing menu UX model consumer",
    )

    failures += check_tokens(
        "modules/ui_shared/src/ui/startup_shell.cpp",
        [
            "buildMenuForUxPack",
            "hooks.ux_pack_id",
            "menu_options.ux_menu",
        ],
        "startup shell UX menu provider",
    )

    failures += check_tokens(
        "modules/ui_shared/src/ui/startup_ui_shell.cpp",
        [
            "buildMenuForUxPack",
            "hooks.ux_pack_id",
            "options.ux_menu",
        ],
        "startup UI shell UX menu provider",
    )

    return failures


def check_composition_root_binding() -> int:
    failures = 0

    failures += check_tokens(
        "modules/product_composition/include/product_composition/presentation_bundle.h",
        [
            "ui_presentation/menu/menu_model.h",
            "ui_presentation/screen/screen_binding_registry.h",
            "MenuModel",
            "ux_menu",
            "hasUxMenu",
            "screen_bindings",
            "hasScreenBindings",
        ],
        "presentation bundle UX menu slot",
    )
    if "ui_lvgl_ux_packs/ux/ux_menu_model.h" in read_text(
        "modules/product_composition/include/product_composition/presentation_bundle.h"
    ):
        failures += fail(
            "PresentationBundle must not depend on ui_lvgl_ux_packs/ux/ux_menu_model.h"
        )

    for path, config_token, pack_id in [
        (
            "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.h",
            "UConsoleCompositionRootConfig",
            "uconsole_desktop",
        ),
        (
            "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.h",
            "LinuxSimCompositionRootConfig",
            "simulator_full",
        ),
    ]:
        failures += check_tokens(
            path,
            [
                "ui_presentation/menu/menu_model.h",
                "MenuModel",
                "ScreenBindingRegistry",
                "ux_pack_id",
                pack_id,
                config_token,
                "initialize(const",
                "uxMenu",
            ],
            "composition root UX menu contract",
        )
        if "ui_lvgl_ux_packs/ux/ux_menu_model.h" in read_text(path):
            failures += fail(
                f"{path} must use ui_presentation/menu/menu_model.h, not the UX-pack menu alias"
            )

    for path in [
        "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.cpp",
        "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.cpp",
    ]:
        failures += check_tokens(
            path,
            [
                "buildMenuForUxPack",
                "config.ux_pack_id",
                "ux_menu_",
                "screen_bindings_",
                "presentation_.ux_menu",
                "presentation_.screen_bindings",
            ],
            "composition root UX menu wiring",
        )

        text = read_text(path)
        for token in [
            "builds/",
            "builds\\",
            "boards/",
            "boards\\",
            "platformio",
            "idf_component_register",
            "TRAIL_MATE_IDF_TARGET",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden composition root token: {token}")

    for path in [
        "apps/esp32_lvgl/tests/esp32_lvgl_app_shell_smoke.cpp",
        "apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_app_shell_smoke.cpp",
        "apps/linux_sim_shell/tests/linux_sim_app_shell_smoke.cpp",
        "apps/nrf52_node/tests/nrf52_node_app_shell_smoke.cpp",
    ]:
        failures += check_tokens(
            path,
            [
                "activeUxPackId",
                "buildMenuForUxPack",
                "menu.size()",
            ],
            "app shell UX menu smoke test",
        )

    failures += check_tokens(
        "legacy/app_implementations/linux_uconsole/CMakeLists.txt",
        [
            "archive-only",
            "Use apps/linux_uconsole_gtk",
        ],
        "uConsole composition root UX pack linkage",
    )

    failures += check_tokens(
        "legacy/app_implementations/linux_sim/CMakeLists.txt",
        [
            "archive-only",
            "Use apps/linux_sim_shell",
        ],
        "Linux sim composition root UX pack target",
    )

    return failures


def check_ui_runtime_consumption_boundary() -> int:
    failures = 0

    failures += check_tokens(
        "legacy/LEGACY_GOVERNANCE.md",
        [
            "not a feature area",
            "historical implementation roots only",
            "LEGACY_BURNDOWN_REGISTER",
            "stable adapters must be renamed",
            "compatibility containment",
        ],
        "legacy governance",
    )

    failures += check_tokens(
        "legacy/app_implementations/LEGACY_IMPLEMENTATION_INDEX.md",
        [
            "Historical path",
            "Current path",
            "Final app shell",
            "Authoritative build wrapper",
            "Exit condition",
            "legacy/app_implementations/gat562_mesh_evb_pro",
        ],
        "legacy implementation index",
    )

    failures += check_tokens(
        "docs/audits/PHASE8_RUNTIME_UI_ADOPTION_REPORT.md",
        [
            "GTK descriptor bridge",
            "ASCII descriptor bridge",
            "LVGL descriptor bridge",
            "real LVGL menu renderer",
            "real GTK page switch logic",
            "real simulator main UI selection",
            "hardcoded screen creation",
            "fallbacks remain compatibility fallbacks",
        ],
        "Phase 8 runtime UI adoption report",
    )

    failures += check_tokens(
        "modules/product_composition/include/product_composition/presentation_bundle.h",
        [
            "ui_presentation/menu/menu_model.h",
            "ux_menu",
            "hasUxMenu",
        ],
        "portable presentation menu boundary",
    )

    product_composition_root = ROOT / "modules/product_composition"
    for path in iter_code_files(product_composition_root):
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "ui_lvgl_ux_packs" in text:
            failures += fail(
                f"{path.relative_to(ROOT)} depends on ui_lvgl_ux_packs; use ui_presentation/menu/menu_model.h"
            )

    failures += check_tokens(
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_model.h",
        [
            "ui_presentation/menu/menu_model.h",
            "using UxMenuItem",
            "using UxMenuModel",
        ],
        "UX menu compatibility alias",
    )

    for path in [
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_provider.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp",
        "modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp",
    ]:
        failures += check_tokens(
            path,
            ["ui::menu::MenuModel"],
            "portable MenuModel provider output",
        )

    for path in [
        "legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.h",
        "legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.h",
    ]:
        failures += check_tokens(
            path,
            [
                "ui_presentation/menu/menu_model.h",
                "ui::menu::MenuModel",
            ],
            "composition root portable menu model",
        )
        if "ui_lvgl_ux_packs/ux/ux_menu_model.h" in read_text(path):
            failures += fail(
                f"{path} must not include the UX-pack menu compatibility alias"
            )

    runtime_contracts = {
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_menu_runtime_adapter.cpp": [
            "LvglMenuRuntimeAdapter",
            "buildMenuForUxPack",
            "MenuModel",
        ],
        "modules/ui_ascii_runtime/tests/ascii_menu_runtime_adapter_smoke.cpp": [
            "AsciiMenuRuntimeAdapter",
            "presentation().ux_menu",
            "hasUxMenu",
        ],
        "modules/ui_gtk_runtime/tests/gtk_menu_runtime_adapter_smoke.cpp": [
            "GtkMenuRuntimeAdapter",
            "presentation().ux_menu",
            "hasUxMenu",
        ],
    }
    for path, tokens in runtime_contracts.items():
        failures += check_tokens(path, tokens, "runtime menu consumption smoke")

    for path in [
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_menu_runtime_adapter.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_menu_runtime_adapter.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp",
    ]:
        text = read_text(path)
        for token in [
            "lvgl.h",
            "apps/",
            "apps\\",
            "builds/",
            "boards/",
            "boards\\",
            "platform/",
            "platform\\",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden runtime menu adapter token {token}")

    failures += check_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "ascii_menu_runtime_adapter.cpp",
            "ascii_screen_graph_bridge.cpp",
        ],
        "ASCII menu runtime adapter final shell source",
    )
    failures += check_tokens(
        "apps/linux_uconsole_gtk/CMakeLists.txt",
        [
            "gtk_menu_runtime_adapter.cpp",
            "gtk_uconsole_screen_graph_bridge.cpp",
        ],
        "GTK menu runtime adapter final shell source",
    )
    failures += check_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "lvgl_menu_runtime_adapter",
            "lvgl_screen_graph_bridge",
        ],
        "LVGL menu runtime adapter test target",
    )

    return failures


def check_screen_factory_host_binding() -> int:
    failures = 0

    portable_contracts = {
        "modules/ui_presentation/include/ui_presentation/screen/screen_route.h": [
            "ScreenRoute",
            "ScreenOpenMode",
            "routeForMenuScreen",
        ],
        "modules/ui_presentation/include/ui_presentation/screen/screen_binding_registry.h": [
            "ScreenBindingRegistry",
            "ScreenBinding",
            "find",
        ],
        "modules/ui_presentation/tests/test_screen_binding_registry.cpp": [
            "ScreenRoute",
            "routeForMenuScreen",
            "ScreenBindingRegistry",
        ],
    }
    for path, tokens in portable_contracts.items():
        failures += check_tokens(path, tokens, "portable screen binding contract")

    screen_runtime_contracts = {
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h": [
            "CompatibilityScreenFactory",
            "CompatibilityScreenDescriptor",
            "buildBindingsForMenu",
        ],
        "modules/ui_lvgl_ux_packs/tests/test_compatibility_screen_factory.cpp": [
            "CompatibilityScreenFactory",
            "Dashboard",
            "Chat",
            "Map",
            "Settings",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h": [
            "LvglScreenHostAdapter",
            "resolve",
            "binding_id",
        ],
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_screen_host_adapter.cpp": [
            "LvglScreenHostAdapter",
            "resolve",
            "binding_id",
        ],
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h": [
            "LvglScreenGraphBridge",
            "PresentationBundle",
            "screenCount",
        ],
        "modules/ui_lvgl_ux_packs/tests/test_lvgl_screen_graph_bridge.cpp": [
            "LvglScreenGraphBridge",
            "PresentationBundle",
            "hasUxMenu",
            "hasScreenBindings",
        ],
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_host_adapter.h": [
            "AsciiScreenHostAdapter",
            "ScreenRoute",
            "resolve",
        ],
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_screen_host_adapter.h": [
            "GtkScreenHostAdapter",
            "ScreenRoute",
            "resolve",
        ],
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_graph_bridge.h": [
            "AsciiScreenGraphBridge",
            "PresentationBundle",
            "AsciiMenuRuntimeAdapter",
            "AsciiScreenHostAdapter",
            "AsciiScreenGraph",
        ],
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h": [
            "GtkUConsoleScreenGraphBridge",
            "PresentationBundle",
            "GtkMenuRuntimeAdapter",
            "GtkScreenHostAdapter",
            "screenBindingCount",
        ],
    }
    for path, tokens in screen_runtime_contracts.items():
        failures += check_tokens(path, tokens, "screen factory host binding")

    bridge_source_contracts = {
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp": [
            "PresentationBundle",
            "hasUxMenu",
            "hasScreenBindings",
            "LvglMenuRuntimeAdapter",
            "LvglScreenHostAdapter",
        ],
        "modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp": [
            "PresentationBundle",
            "hasUxMenu",
            "hasScreenBindings",
            "AsciiMenuRuntimeAdapter",
            "AsciiScreenHostAdapter",
        ],
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp": [
            "PresentationBundle",
            "hasUxMenu",
            "hasScreenBindings",
            "GtkMenuRuntimeAdapter",
            "GtkScreenHostAdapter",
        ],
    }
    for path, tokens in bridge_source_contracts.items():
        failures += check_tokens(path, tokens, "screen graph bridge runtime adoption")

    for path in [
        "modules/ui_ascii_runtime/tests/ascii_screen_host_adapter_smoke.cpp",
        "modules/ui_gtk_runtime/tests/gtk_screen_host_adapter_smoke.cpp",
        "modules/ui_ascii_runtime/tests/ascii_screen_graph_bridge_smoke.cpp",
        "modules/ui_gtk_runtime/tests/gtk_uconsole_screen_graph_bridge_smoke.cpp",
    ]:
        failures += check_tokens(
            path,
            [
                "presentation().ux_menu",
                "route",
                "ScreenRoute",
                "hasScreenBindings",
            ],
            "screen host adapter smoke route consumption",
        )

    for path in [
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_menu_runtime_adapter.h",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_menu_runtime_adapter.h",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_menu_runtime_adapter.h",
    ]:
        failures += check_tokens(
            path,
            ["ScreenRoute", "route"],
            "menu runtime route-bearing descriptor",
        )

    for path in [
        "apps/linux_sim_shell/CMakeLists.txt",
        "apps/linux_uconsole_gtk/CMakeLists.txt",
    ]:
        failures += check_tokens(
            path,
            [
                "screen_host_adapter.cpp",
                "screen_graph_bridge.cpp",
            ],
            "screen host adapter final shell source",
        )

    failures += check_tokens(
        "apps/linux_sim_shell/CMakeLists.txt",
        [
            "compatibility_screen_factory",
            "lvgl_screen_host_adapter",
        ],
        "LVGL screen factory smoke target",
    )

    forbidden_portable = [
        "lvgl.h",
        "gtk",
        "apps/",
        "apps\\",
        "boards/",
        "boards\\",
        "platform/",
        "platform\\",
    ]
    for path in [
        "modules/ui_presentation/include/ui_presentation/screen/screen_route.h",
        "modules/ui_presentation/include/ui_presentation/screen/screen_binding_registry.h",
        "modules/ui_presentation/src/screen/screen_binding_registry.cpp",
    ]:
        text = read_text(path)
        for token in forbidden_portable:
            if token in text:
                failures += fail(f"{path} contains forbidden portable screen token {token}")

    for path in [
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h",
        "modules/ui_lvgl_ux_packs/src/runtime/compatibility_screen_factory.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_host_adapter.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h",
        "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp",
    ]:
        text = read_text(path)
        for token in [
            "lvgl.h",
            "gtk",
            "apps/",
            "apps\\",
            "boards/",
            "boards\\",
            "platform/",
            "platform\\",
            "ChatService",
            "MapRuntime",
            "GpsRuntime",
            "lv_obj",
            "GtkWidget",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden screen factory token {token}")

    for path in [
        "modules/ui_ascii_runtime/include/ui_ascii_runtime/ascii_screen_graph_bridge.h",
        "modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp",
        "modules/ui_gtk_runtime/include/ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h",
        "modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp",
    ]:
        text = read_text(path)
        for token in [
            "lvgl.h",
            "GtkWidget",
            "gtk_widget",
            "activeUxPackId",
            "findUxPackById",
            "buildMenuForUxPack",
            "apps/",
            "boards/",
            "platformio",
        ]:
            if token in text:
                failures += fail(f"{path} contains forbidden screen graph bridge token {token}")

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
            "ui_chat_runtime/chat_delivery_event_projection_adapter.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h":
            "ui_chat_runtime/chat_delivery_action_port_adapter.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h":
            "ui_key_verification_runtime/key_verification_session_adapter.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h":
            "ui_key_verification_runtime/key_verification_presentation_source.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h":
            "ui_key_verification_runtime/key_verification_action_sink.h",
        "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h":
            "ui_map_runtime/map_overlay_snapshot_source.h",
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
        if path in {
            "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_event_bridge.h",
            "modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h",
            "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h",
            "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h",
            "modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h",
            "modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h",
        }:
            if "using Legacy" not in text or "[[deprecated" not in text:
                failures += fail(f"{path} must be a deprecated alias forwarding header")
            if "class " in text or "struct " in text:
                failures += fail(f"{path} must not contain a class or struct implementation")
            continue
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
            "ui_chat_runtime/chat_delivery_event_projection_adapter.h",
        "ui/presentation_sources/legacy_chat_delivery_action_bridge.h":
            "ui_chat_runtime/chat_delivery_action_port_adapter.h",
        "ui/presentation_sources/legacy_key_verification_session.h":
            "ui_key_verification_runtime/key_verification_session_adapter.h",
        "ui/presentation_sources/legacy_key_verification_source.h":
            "ui_key_verification_runtime/key_verification_presentation_source.h",
        "ui/presentation_sources/legacy_key_verification_action_sink.h":
            "ui_key_verification_runtime/key_verification_action_sink.h",
        "ui/presentation_sources/legacy_map_overlay_source.h":
            "ui_map_runtime/map_overlay_snapshot_source.h",
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
        "legacy/app_implementations/esp_pio/library.json",
        "legacy/app_implementations/gat562_mesh_evb_pro/library.json",
    ]:
        failures += check_tokens(manifest, common_include_tokens, "new module include authority")

    failures += check_tokens(
        "legacy/app_implementations/esp_pio/library.json",
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
        "legacy/app_implementations/esp_idf/CMakeLists.txt",
        [
            "modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp",
            "modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp",
            "modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp",
            "modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp",
            "modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp",
            "modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp",
            "modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp",
            "modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp",
            "modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp",
            "modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp",
            "modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp",
            "modules/ui_lvgl_core/include",
            "modules/ui_presentation/src/*/*.cpp",
            "modules/ui_lvgl_ux_packs/src/common/team_position_picker_renderer.cpp",
            "modules/ui_lvgl_ux_packs/src/common/key_verification_modal_renderer.cpp",
            "modules/ui_lvgl_ux_packs/src/ux/screen_registry.cpp",
            "modules/ui_lvgl_ux_packs/src/ux/input_binding_set.cpp",
            "modules/ui_lvgl_ux_packs/src/ux/ux_menu_model.cpp",
            "modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp",
            "modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp",
            "modules/ui_lvgl_ux_packs/src/ux/ux_pack_registry.cpp",
            "modules/ui_lvgl_ux_packs/src/runtime/compatibility_screen_factory.cpp",
            "modules/ui_lvgl_ux_packs/src/runtime/lvgl_menu_runtime_adapter.cpp",
            "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_host_adapter.cpp",
            "modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp",
            "modules/ui_lvgl_ux_packs/src/packs/compatibility_ux_pack.cpp",
            "modules/ui_lvgl_ux_packs/src/packs/uconsole_desktop_ux_pack.cpp",
            "modules/ui_lvgl_ux_packs/src/packs/tiny_node_status_ux_pack.cpp",
            "modules/ui_lvgl_ux_packs/src/packs/simulator_full_ux_pack.cpp",
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
            "TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_INCLUDE_ROOT",
            "TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_SRC_ROOT",
            "TRAIL_MATE_UI_LEGACY_ADAPTERS_INCLUDE_ROOT",
            "TRAIL_MATE_UI_LEGACY_ADAPTERS_SRC_ROOT",
            "TRAIL_MATE_UI_LVGL_CORE_INCLUDE_ROOT",
            "TRAIL_MATE_UI_LVGL_UX_PACKS_INCLUDE_ROOT",
            "TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT",
            "chat_page_runtime_event_pump.cpp",
            "chat_delivery_action_port_adapter.cpp",
            "chat_delivery_event_projection_adapter.cpp",
            "key_verification_presentation_source.cpp",
            "key_verification_action_sink.cpp",
            "filesystem_map_tile_source.cpp",
            "map_overlay_projection_adapter.cpp",
            "map_overlay_snapshot_source.cpp",
            "gps_page_runtime_pump.cpp",
            "menu_model.cpp",
            "screen_binding_registry.cpp",
            "ux_menu_model.cpp",
            "ux_screen_menu_adapter.cpp",
            "ux_menu_provider.cpp",
            "ux_pack_registry.cpp",
            "compatibility_screen_factory.cpp",
            "lvgl_menu_runtime_adapter.cpp",
            "lvgl_screen_host_adapter.cpp",
            "lvgl_screen_graph_bridge.cpp",
            "compatibility_ux_pack.cpp",
            "uconsole_desktop_ux_pack.cpp",
            "tiny_node_status_ux_pack.cpp",
            "simulator_full_ux_pack.cpp",
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
        "modules/ui_key_verification_runtime": [
            "lvgl.h",
            "GtkWidget",
            "lv_obj_t",
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

    for path in [
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_model.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_menu_model.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp",
        "modules/ui_lvgl_ux_packs/include/ui_lvgl_ux_packs/ux/ux_menu_provider.h",
        "modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp",
    ]:
        text = read_text(path)
        for token in ["lvgl.h", "apps/", "apps\\", "boards/", "boards\\", "platform/", "platform\\"]:
            if token in text:
                failures += fail(f"{path} contains forbidden UX menu binding token {token}")

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

    for path in [
        "legacy/app_implementations/esp_idf",
        "legacy/app_implementations/esp_pio",
        "legacy/app_implementations/linux_sim",
        "legacy/app_implementations/linux_uconsole",
        "legacy/app_implementations/gat562_mesh_evb_pro",
    ]:
        if exists(path) and path not in marker:
            failures += fail(f"{path} exists but is not marked transitional")

    apps_root = ROOT / "apps"
    if not apps_root.exists():
        return failures

    allowed = {
        "README.md",
        "esp32_lvgl",
        "nrf52_node",
        "linux_uconsole_gtk",
        "linux_sim_shell",
    }
    for child in apps_root.iterdir():
        if child.name not in allowed:
            failures += fail(
                f"apps/{child.name} is not an allowed final app shell entry"
            )

    for legacy_name in [
        "esp_idf",
        "esp_pio",
        "linux_uconsole",
        "linux_sim",
        "linux_rpi",
        "linux_unoq",
        "gat562_mesh_evb_pro",
    ]:
        if (apps_root / legacy_name).exists():
            failures += fail(
                f"apps/{legacy_name} must not contain a legacy implementation root"
            )

    return failures


def main() -> int:
    if not exists("legacy"):
        return check_post_root_legacy_removed()

    failures = 0
    failures += check_required_files()
    failures += check_specification_language()
    failures += check_audit_language()
    failures += check_app_shell_language()
    failures += check_ux_profile_language()
    failures += check_build_entrypoint_language()
    failures += check_executable_layout_convergence()
    failures += check_ux_pack_runtime_binding()
    failures += check_composition_root_binding()
    failures += check_ui_runtime_consumption_boundary()
    failures += check_screen_factory_host_binding()
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
