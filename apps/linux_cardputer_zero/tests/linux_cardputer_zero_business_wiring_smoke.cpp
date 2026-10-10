#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.is_open());
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool not_contains(const std::string& haystack, const char* needle)
{
    return !contains(haystack, needle);
}

std::size_t position_of(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    assert(pos != std::string::npos);
    return pos;
}

std::string slice_between(const std::string& haystack,
                          const char* begin,
                          const char* end)
{
    const auto begin_pos = haystack.find(begin);
    assert(begin_pos != std::string::npos);
    const auto end_pos = haystack.find(end, begin_pos);
    assert(end_pos != std::string::npos);
    return haystack.substr(begin_pos, end_pos - begin_pos);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];

    const std::string shared_shell = read_file(
        repo_root / "platform/linux/common/src/ui/shared_ui_shell.cpp");
    assert(contains(shared_shell, "Projection::Map"));
    assert(contains(shared_shell, "gps_route_enter"));
    assert(contains(shared_shell, "gps_route_exit"));
    assert(contains(shared_shell, "s_map_spec{\"map\", \"Map\", &gps_icon}"));
    assert(contains(shared_shell, "s_sky_plot_spec{\"sky_plot\", \"Sky Plot\", &Satellite}"));
    assert(contains(shared_shell, "gnss::ui::shell::enter"));
    assert(contains(shared_shell, "s_extensions_spec{\"extensions\", \"Extensions\", &ext}"));
    assert(not_contains(shared_shell, "Projection::GpsStatus"));
    assert(not_contains(shared_shell, "s_gps_app"));
    assert(not_contains(shared_shell, "pc_link_page_shell"));
    assert(not_contains(shared_shell, "sstv_page_shell"));
    assert(not_contains(shared_shell, "energy_sweep_page_shell"));
    assert(not_contains(shared_shell, "usb_page_shell"));
    assert(not_contains(shared_shell, "s_pc_link_spec"));
    assert(not_contains(shared_shell, "s_sstv_spec"));
    assert(not_contains(shared_shell, "s_energy_sweep_spec"));
    assert(not_contains(shared_shell, "usb_mass_storage"));

    const std::string cardputer_ux = read_file(
        repo_root / "modules/ui_lvgl_ux_packs/src/packs/cardputer_compact_ux_pack.cpp");
    assert(contains(cardputer_ux, "ScreenId::SkyPlot"));
    assert(contains(cardputer_ux, "\"Sky Plot\""));
    assert(contains(cardputer_ux, "ScreenId::Extensions"));
    assert(not_contains(cardputer_ux, "pc_link"));
    assert(not_contains(cardputer_ux, "ScreenId::EnergySweep"));
    assert(not_contains(cardputer_ux, "ScreenId::Sstv"));
    assert(not_contains(cardputer_ux, "ScreenId::Gps"));

    const std::string cardputer_cmake = read_file(
        repo_root / "apps/linux_cardputer_zero/CMakeLists.txt");
    assert(not_contains(cardputer_cmake, "NO_LEGACY_PRESENTATION"));
    assert(not_contains(cardputer_cmake, "ui_legacy_adapters"));

    const std::string board_facts = read_file(
        repo_root / "boards/cardputerzero/board_facts.h");
    assert(contains(board_facts, "M5Stack Cap LoRa-1262"));
    assert(contains(board_facts, "bool gps_present = true"));
    assert(contains(board_facts, "ATGM336H-6N@AT6668"));
    assert(contains(board_facts, "NMEA 0183 4.1"));
    assert(contains(board_facts, "int gps_default_baud = 115200"));
    assert(contains(board_facts, "int gps_rx_gpio = 15"));
    assert(contains(board_facts, "int gps_tx_gpio = 14"));

    const std::string board_doc = read_file(
        repo_root / "boards/cardputerzero/BOARD.md");
    assert(contains(board_doc, "M5Stack Cap LoRa-1262 module documentation"));
    assert(contains(board_doc, "Cardputer Zero external wiring note"));
    assert(contains(board_doc, "`G15=GPS_RX`, `G14=GPS_TX`"));
    assert(contains(board_doc, "must not be inferred from Cardputer-Adv"));
    assert(contains(board_doc, "module-level"));
    assert(contains(board_doc, "LoRa/GNSS facts"));
    assert(contains(board_doc, "other M5Stack"));
    assert(contains(board_doc, "must not reconfigure them as ordinary GPIO"));

    const std::string app_manifest = read_file(
        repo_root / "apps/linux_cardputer_zero/APP_SHELL_MANIFEST.md");
    assert(contains(app_manifest, "M5Stack Cap LoRa-1262"));
    assert(contains(app_manifest, "G15=GPS_RX"));
    assert(contains(app_manifest, "G14=GPS_TX"));
    assert(contains(app_manifest, "must not be inferred"));
    assert(contains(app_manifest, "Cardputer-Adv"));
    assert(contains(app_manifest, "adopt the external Meshtastic daemon implementation"));

    const std::string adaptation_doc = read_file(
        repo_root / "docs/targets/cardputerzero-adaptation.md");
    assert(contains(adaptation_doc, "M5Stack Cap LoRa-1262"));
    assert(contains(adaptation_doc, "module-level source for the"));
    assert(contains(adaptation_doc, "Cardputer Zero external wiring evidence"));
    assert(contains(adaptation_doc, "must"));
    assert(contains(adaptation_doc, "not be inferred from the Cardputer-Adv"));
    assert(contains(adaptation_doc, "unrelated M5Stack"));
    assert(contains(adaptation_doc, "`TRAIL_MATE_GPS_DEVICE` is an explicit user override"));
    assert(contains(adaptation_doc, "`TRAIL_MATE_GPS_DEVICE_CANDIDATES` supplies the auto-probe list"));
    assert(contains(adaptation_doc, "OS image/profile owns UART availability"));
    assert(contains(adaptation_doc, "not application-owned GPIO direction lines"));
    assert(contains(adaptation_doc, "must not force"));

    const std::string linux_sources_cmake = read_file(
        repo_root / "cmake/TrailMateLinuxSources.cmake");
    const std::string common_includes = slice_between(
        linux_sources_cmake,
        "set(TRAIL_MATE_LINUX_COMMON_INCLUDES",
        "set(TRAIL_MATE_LINUX_UI_SHELL_INCLUDES");
    const std::string shell_includes = slice_between(
        linux_sources_cmake,
        "set(TRAIL_MATE_LINUX_UI_SHELL_INCLUDES",
        "function(trailmate_apply_linux_common_warnings");
    assert(not_contains(common_includes, "TRAIL_MATE_UI_LEGACY_ADAPTERS_INCLUDE_ROOT"));
    assert(not_contains(shell_includes, "TRAIL_MATE_UI_LEGACY_ADAPTERS_INCLUDE_ROOT"));
    assert(not_contains(linux_sources_cmake, "TRAIL_MATE_LINUX_UI_LEGACY_PRESENTATION_SOURCES"));
    assert(not_contains(linux_sources_cmake, "NO_LEGACY_PRESENTATION"));
    assert(not_contains(linux_sources_cmake, "legacy_air_device_status_source.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_gps_status_source.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_mesh_status_source.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_settings_source.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_settings_action_sink.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_chat_action_sink.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_map_presentation_source.cpp"));
    assert(not_contains(linux_sources_cmake, "legacy_map_action_sink.cpp"));
    assert(contains(linux_sources_cmake, "TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/ui_status.cpp"));
    assert(contains(linux_sources_cmake, "TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/gps_topbar.c"));
    assert(contains(linux_sources_cmake, "TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/wifi_topbar.c"));
    assert(contains(linux_sources_cmake, "TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/ble_topbar.c"));
    assert(not_contains(linux_sources_cmake, "TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/ui_status.cpp"));

    const std::string menu_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/menu/menu_runtime.cpp");
    assert(contains(menu_runtime, "config.title = \"Main Menu Help\""));
    assert(contains(menu_runtime, "::ui::components::shortcut_help_modal::open("));
    assert(contains(menu_runtime, "screenshotShortcutHelpEnabled"));
    assert(contains(menu_runtime, "defined(ARDUINO_T_LORA_PAGER)"));
    assert(contains(menu_runtime, "defined(ARDUINO_T_DECK)"));
    assert(contains(menu_runtime, "defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)"));
    assert(contains(menu_runtime, "{\"ALT\", \"ALT\", \"Save screenshot\"}"));
    assert(contains(menu_runtime,
                    "if (isMenuHelpKey(key))\n    {\n#if defined(ARDUINO_T_DECK_PRO)"));
    // Cardputer Zero is not the T-Deck Pro text-shell target. Its help
    // shortcut must continue through the shared LVGL modal branch.
    assert(contains(menu_runtime,
                    "#else\n        openMenuHelpModal();\n        return true;\n#endif"));

    // The P4 keyboard adapter owns physical-key behavior. Keep the Alt
    // double-press screenshot shortcut with that adapter rather than the
    // display runtime, which owns the panel and touch lifecycle only.
    const std::string t_display_p4_keyboard = read_file(
        repo_root / "platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_keyboard.cpp");
    assert(contains(t_display_p4_keyboard, "kAltDoublePressMs"));
    assert(contains(t_display_p4_keyboard, "ui_take_screenshot_to_sd();"));

    const std::string gps_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp");
    assert(contains(gps_runtime, "runtime_gps_status_source"));
    assert(contains(gps_runtime, "runtime_map_workspace_state"));
    assert(contains(gps_runtime, "RuntimeMapWorkspaceSource"));
    assert(contains(gps_runtime, "RuntimeMapActionSink"));
    assert(contains(gps_runtime, "GpsStatusModel"));
    assert(contains(gps_runtime, "create_map_control_bar"));
    assert(contains(gps_runtime, "create_map_control_button"));
    assert(contains(gps_runtime, "add_map_controls_to_group"));
    assert(contains(gps_runtime, "kLvglFunctionKeyF1"));
    assert(contains(gps_runtime, "open_map_help_modal"));
    assert(contains(gps_runtime, "create_map_notice_overlay"));
    assert(contains(gps_runtime, "create_map_context_rail"));
    assert(contains(gps_runtime, "show_team_overlay_notice"));
    assert(contains(gps_runtime, "route_context_available"));
    assert(contains(gps_runtime, "config.route_enabled && config.route_path[0] != '\\0'"));
    assert(contains(gps_runtime, "load_team_snapshot(team_snapshot) && !team_snapshot.members.empty()"));
    assert(contains(gps_runtime, "create_member_button(member)"));
    assert(contains(gps_runtime, "select_member(member_id)"));
    assert(contains(gps_runtime, "lv_obj_align(s_map_notice_panel, LV_ALIGN_TOP_LEFT"));
    assert(contains(gps_runtime, "lv_obj_set_style_bg_opa(s_map_notice_panel, LV_OPA_70"));
    assert(contains(gps_runtime, "set_hidden(s_map_context_rail, next_mask == 0)"));
    assert(contains(gps_runtime, "MapControlAction::Route"));
    assert(not_contains(gps_runtime, "\"GPX\""));
    assert(contains(gps_runtime, "gps_ui::kDefaultLat"));
    assert(contains(gps_runtime, "gps_ui::kDefaultLng"));
    assert(contains(gps_runtime, "model.focus_point.valid = true"));
    assert(contains(gps_runtime, "model.focus_point.lat = has_viewport_center"));
    assert(contains(gps_runtime, "? snapshot.viewport.center_lat"));
    assert(contains(gps_runtime, ": (snapshot.self.valid ? snapshot.self.lat"));
    assert(contains(gps_runtime, ": gps_ui::kDefaultLat)"));
    assert(contains(gps_runtime, "model.focus_point.lon = has_viewport_center"));
    assert(contains(gps_runtime, "? snapshot.viewport.center_lon"));
    assert(contains(gps_runtime, ": (snapshot.self.valid ? snapshot.self.lon"));
    assert(contains(gps_runtime, ": gps_ui::kDefaultLng)"));
    assert(contains(gps_runtime, "sync_workspace_center_from_screen"));
    assert(contains(gps_runtime, "screen_center(s_map_runtime, center)"));
    assert(contains(gps_runtime, "commit_pending_map_pan_from_screen"));
    assert(contains(gps_runtime, "if (!s_map_drag_active && commit_pending_map_pan_from_screen())"));
    assert(contains(gps_runtime, "lv_group_add_obj(group, s_map_zoom_in_btn)"));
    assert(contains(gps_runtime, "lv_group_add_obj(group, s_map_layer_btn)"));
    assert(contains(gps_runtime, "set_layer_map_source"));
    assert(contains(gps_runtime, "toggle_layer_contour"));
    assert(contains(gps_runtime, "center_map_on_self"));
    assert(contains(gps_runtime, "case 'p':"));
    assert(contains(gps_runtime, "case 'P':"));
    assert(contains(gps_runtime, "case 'o':"));
    assert(contains(gps_runtime, "case 'O':"));
    assert(contains(gps_runtime, "case 'w':"));
    assert(contains(gps_runtime, "case 'W':"));
    assert(contains(gps_runtime, "case 'a':"));
    assert(contains(gps_runtime, "case 'A':"));
    assert(contains(gps_runtime, "case 's':"));
    assert(contains(gps_runtime, "case 'S':"));
    assert(contains(gps_runtime, "case 'd':"));
    assert(contains(gps_runtime, "case 'D':"));
    assert(contains(gps_runtime, "case LV_KEY_LEFT:"));
    assert(contains(gps_runtime, "s_map_pan_x += gps_ui::kMapPanStep;"));
    assert(contains(gps_runtime, "case LV_KEY_RIGHT:"));
    assert(contains(gps_runtime, "s_map_pan_x -= gps_ui::kMapPanStep;"));
    assert(contains(gps_runtime, "take_missing_tile_notice"));
    assert(contains(gps_runtime, "s_map_refresh_pending"));
    assert(contains(gps_runtime, "refresh_view_async"));
    assert(contains(gps_runtime, "request_refresh_view"));
    assert(contains(gps_runtime, "lv_async_call(refresh_view_async, nullptr)"));
    assert(contains(gps_runtime, "request_open_map_help_modal"));
    assert(contains(gps_runtime, "lv_async_call(open_map_help_modal_async, nullptr)"));
    assert(contains(gps_runtime, "lv_obj_add_flag(s_map_help_modal, LV_OBJ_FLAG_IGNORE_LAYOUT)"));
    assert(contains(gps_runtime, "lv_label_set_text(title, \"Map Help\")"));
    assert(contains(gps_runtime, "add_help_row(\"WASD\", nullptr, \"Move map\")"));
    assert(contains(gps_runtime, "add_help_row(\"Q\", \"E\", \"Zoom map\")"));
    assert(contains(gps_runtime, "add_help_row(\"C\", \"Pos\", \"Center current position\")"));
    assert(contains(gps_runtime, "add_help_row(\"P\", nullptr, \"Show/hide route photos\")"));
    assert(contains(gps_runtime, "add_help_row(\"L\", nullptr, \"Change base layer\")"));
    assert(contains(gps_runtime, "add_help_row(\"T\", \"Track\", \"Select track file\")"));
    assert(contains(gps_runtime, "add_help_row(\"V\", nullptr, \"Show/hide elevation profile\")"));
    assert(contains(gps_runtime, "add_help_row(\"I\", nullptr, \"Hide info, keep route\")"));
    assert(contains(gps_runtime, "add_help_row(\"O\", \"Ctr\", \"Toggle contour overlay\")"));
    assert(contains(gps_runtime, "add_help_row(\"Route\", nullptr, \"Shown when route active\")"));
    assert(contains(gps_runtime, "add_help_row(\"Members\", nullptr, \"Shown when team active\")"));
    assert(contains(gps_runtime, "add_help_row(help_key_label(), \"Back\", \"Close help\")"));
    assert(not_contains(gps_runtime, "\"Topo\""));
    assert(contains(gps_runtime, "case 't':"));
    assert(contains(gps_runtime, "case 'T':"));
    assert(not_contains(gps_runtime, "case kLvglFunctionKeyF1:\n        open_map_help_modal();"));
    assert(not_contains(gps_runtime, "lv_indev_stop_processing(indev);"));
    assert(contains(gps_runtime, "if (snapshot.header.valid)"));
    assert(not_contains(gps_runtime, "legacy_gps_status_source"));
    assert(not_contains(gps_runtime, "LegacyMapPresentationSource"));
    assert(not_contains(gps_runtime, "LegacyMapActionSink"));

    const std::string skyplot_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp");
    assert(contains(skyplot_runtime, "make_dense_compact_layout"));
    assert(contains(skyplot_runtime, "ui::page_profile::is_dense()"));
    assert(contains(skyplot_runtime, "status_toggle_font"));
    assert(not_contains(skyplot_runtime, "TRAIL_MATE_CARDPUTER_ZERO_LINUX"));

    const std::string linux_gps_runtime = read_file(
        repo_root / "platform/linux/common/src/platform/ui/gps_runtime.cpp");
    assert(contains(linux_gps_runtime, "FallbackMode::LiveOnly"));
    assert(contains(linux_gps_runtime, "TRAIL_MATE_GPS_DEMO_DEFAULTS"));
    assert(contains(linux_gps_runtime, "demo_defaults_enabled()"));
    assert(contains(linux_gps_runtime, "GPS source opened"));
    assert(contains(linux_gps_runtime, "GPS serial waiting for NMEA"));
    assert(contains(linux_gps_runtime, "TRAIL_MATE_GPS_DEVICE_CANDIDATES"));
    assert(contains(linux_gps_runtime, "GPS serial no traffic"));
    assert(contains(linux_gps_runtime, "next_auto_serial_candidate_locked"));
    assert(contains(linux_gps_runtime, "deduplicate_auto_serial_candidates"));
    assert(contains(linux_gps_runtime, "std::filesystem::weakly_canonical"));

    const std::string linux_time_runtime = read_file(
        repo_root / "platform/linux/common/src/platform/ui/time_runtime.cpp");
    assert(contains(linux_time_runtime, "std::localtime"));
    assert(contains(linux_time_runtime, "timezone_setting_configured()"));

    const std::string linux_ui_common = read_file(
        repo_root / "platform/linux/common/src/ui/ui_common.cpp");
    assert(contains(linux_ui_common, "platform::ui::time::localtime_now"));
    assert(contains(linux_ui_common, "top_bar_set_right_text_ascii"));
    assert(not_contains(linux_ui_common, "\"USB\""));
    assert(not_contains(linux_ui_common, "platform::ui::device::battery_info()"));
    assert(not_contains(linux_ui_common, "format_top_bar_power"));
    assert(not_contains(linux_ui_common, "noop_top_bar_battery_update"));

    const std::string top_bar = read_file(
        repo_root / "modules/ui_shared/src/ui/widgets/top_bar.cpp");
    assert(contains(top_bar, "dense ? 72"));

    const std::string chat_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp");
    assert(contains(chat_runtime, "RuntimeChatActionSink"));
    assert(not_contains(chat_runtime, "LegacyChatActionSink"));
    assert(not_contains(chat_runtime, "legacy_chat_action_sink"));

    const std::string settings_components = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp");
    assert(contains(settings_components, "runtime_settings_source"));
    assert(not_contains(settings_components, "legacy_settings_source"));
    assert(not_contains(settings_components, "legacy_settings_action_sink"));
    assert(contains(settings_components, "device_runtime::supports_screen_brightness()"));
    assert(contains(settings_components, "screen_runtime::supports_app_timeout_setting()"));
    assert(contains(settings_components, "gps_runtime::supports_receiver_baud_setting()"));
    assert(contains(settings_components, "gps_runtime::supports_receiver_init_policy_settings()"));
    assert(contains(settings_components, "gps_runtime::supports_gnss_runtime_settings()"));
    assert(contains(settings_components, "gps_runtime::supports_collection_interval_setting()"));
    assert(contains(settings_components, "gps_runtime::supports_external_nmea_output_setting()"));
    assert(contains(settings_components, "gps_runtime::supports_altitude_reference_setting()"));
    assert(contains(settings_components, "gps_runtime::supports_coordinate_format_setting()"));

    const std::string uconsole_cmake = read_file(
        repo_root / "apps/linux_uconsole_gtk/CMakeLists.txt");
    assert(contains(uconsole_cmake, "runtime_gps_status_source.cpp"));
    assert(contains(uconsole_cmake, "runtime_map_workspace_source.cpp"));
    assert(not_contains(uconsole_cmake, "legacy_gps_status_source.cpp"));
    assert(not_contains(uconsole_cmake, "legacy_map_presentation_source.cpp"));
    assert(not_contains(uconsole_cmake, "legacy_map_action_sink.cpp"));

    const std::string uconsole_map_model_header = read_file(
        repo_root / "platform/linux/uconsole/include/uconsole/uconsole_map_workspace_model.h");
    const std::string uconsole_map_model = read_file(
        repo_root / "platform/linux/uconsole/src/uconsole_map_workspace_model.cpp");
    assert(contains(uconsole_map_model_header, "runtime_gps_status_source.h"));
    assert(contains(uconsole_map_model_header, "runtime_map_workspace_source.h"));
    assert(contains(uconsole_map_model_header, "RuntimeMapWorkspaceSource"));
    assert(contains(uconsole_map_model_header, "RuntimeMapActionSink"));
    assert(contains(uconsole_map_model, "runtime_gps_status_source()"));
    assert(not_contains(uconsole_map_model_header, "LegacyGpsStatusSource"));
    assert(not_contains(uconsole_map_model_header, "LegacyMapPresentationSource"));
    assert(not_contains(uconsole_map_model_header, "LegacyMapActionSink"));
    assert(not_contains(uconsole_map_model, "legacy_gps_source_"));
    assert(not_contains(uconsole_map_model, "legacy_map_source_"));
    assert(not_contains(uconsole_map_model, "legacy_map_sink_"));

    const std::string linux_services = read_file(
        repo_root / "platform/linux/common/src/app/linux_app_services.cpp");
    assert(contains(linux_services, "LinuxGpsTrackSource"));
    assert(contains(linux_services, "platform::ui::gps::tick_service()"));
    assert(position_of(linux_services, "platform::ui::gps::tick_service()") <
           position_of(linux_services, "platform::ui::tracker::poll"));
    assert(contains(linux_services, "platform::ui::gps::set_receiver_init_config(receiver_init)"));
    assert(contains(linux_services, "platform::ui::tracker::poll"));
    assert(contains(linux_services, "platform::ui::gps::set_fallback_mode"));
    assert(contains(linux_services, "FallbackMode::LiveOnly"));
    assert(contains(linux_services, "applyMeshConfig();"));
    assert(contains(linux_services, "impl_->tickMeshAdapter();"));
    assert(contains(linux_services, "impl_->chat_service.processIncoming();"));
    assert(contains(linux_services, "impl_->team_service.processIncoming();"));
    assert(contains(linux_services, "TRAIL_MATE_DESKTOP_NOTIFICATIONS"));
    assert(contains(linux_services, "notify-send"));
    assert(not_contains(linux_services, "LinuxNullTrackSource"));

    const std::string gps_runtime_header = read_file(
        repo_root / "modules/core_sys/include/platform/ui/gps_runtime.h");
    assert(contains(gps_runtime_header, "void tick_service();"));
    assert(contains(gps_runtime_header, "bool supports_receiver_baud_setting();"));
    assert(contains(gps_runtime_header, "bool supports_receiver_init_policy_settings();"));
    assert(contains(gps_runtime_header, "bool supports_gnss_runtime_settings();"));
    assert(contains(gps_runtime_header, "bool supports_collection_interval_setting();"));
    assert(contains(gps_runtime_header, "bool supports_external_nmea_output_setting();"));

    const std::string linux_gps_platform = read_file(
        repo_root / "platform/linux/common/src/platform/ui/gps_runtime.cpp");
    assert(contains(linux_gps_platform, "void tick_service()"));
    assert(contains(linux_gps_platform, "poll_external_source_locked();"));
    assert(contains(linux_gps_platform, "bool supports_receiver_baud_setting()"));
    assert(contains(linux_gps_platform, "bool supports_receiver_init_policy_settings()"));
    assert(contains(linux_gps_platform, "bool supports_gnss_runtime_settings()"));
    assert(contains(linux_gps_platform, "bool supports_collection_interval_setting()"));
    assert(contains(linux_gps_platform, "bool supports_external_nmea_output_setting()"));
    assert(contains(linux_gps_platform, "return s_receiver_init_config.baud != 0 ? \"settings\" : \"runtime\""));
    assert(not_contains(slice_between(linux_gps_platform, "GpsState get_data()", "bool get_gnss_snapshot"),
                        "poll_external_source_locked();"));
    assert(not_contains(slice_between(linux_gps_platform, "bool get_gnss_snapshot", "GpsDiagnosticsSnapshot diagnostics"),
                        "poll_external_source_locked();"));
    assert(not_contains(slice_between(linux_gps_platform, "GpsDiagnosticsSnapshot diagnostics()", "uint32_t last_motion_ms"),
                        "poll_external_source_locked();"));
    assert(not_contains(slice_between(linux_gps_platform, "uint32_t last_motion_ms()", "void tick_service"),
                        "poll_external_source_locked();"));

    const std::string linux_screen_runtime = read_file(
        repo_root / "platform/linux/common/src/platform/ui/screen_runtime.cpp");
    assert(contains(linux_screen_runtime, "bool supports_app_timeout_setting()"));
    assert(contains(linux_screen_runtime, "return false;"));

    const std::string linux_device_runtime = read_file(
        repo_root / "platform/linux/common/src/platform/ui/device_runtime.cpp");
    assert(contains(linux_device_runtime, "bool supports_screen_brightness()"));
    assert(contains(linux_device_runtime, "return false;"));

    const std::string raw_lora_header = read_file(
        repo_root / "platform/linux/common/include/chat/linux_raw_lora_mesh_adapter.h");
    const std::string raw_lora = read_file(
        repo_root / "platform/linux/common/src/chat/linux_raw_lora_mesh_adapter.cpp");
    assert(contains(raw_lora_header, "triggerDiscoveryActionDetailed"));
    assert(contains(raw_lora, "supports_discovery_actions"));
    assert(contains(raw_lora, "Meshtastic discovery queued"));
    assert(contains(raw_lora, "MeshDiscoveryAction::ScanLocal"));

    const std::string linux_map_tiles = read_file(
        repo_root / "platform/linux/common/src/ui/widgets/map/map_tiles.cpp");
    assert(contains(linux_map_tiles, "platform/linux/map_tile_cache.h"));
    assert(contains(linux_map_tiles, "platform/linux/map_contour_tile_generator.h"));
    assert(contains(linux_map_tiles, "schedule_base_tile_fetch"));
    assert(contains(linux_map_tiles, "online_tile_cache().ensure_tile(tile)"));
    assert(contains(linux_map_tiles, "TRAIL_MATE_EARTHDATA_TOKEN"));
    assert(contains(linux_map_tiles, "MapContourTileGenerator"));
    assert(contains(linux_map_tiles, "contour_profiles_for_zoom"));
    assert(contains(linux_map_tiles, "ContourMinor20"));
    assert(contains(linux_map_tiles, "lv_obj_set_style_opa(contour, LV_OPA_COVER"));
    assert(contains(linux_map_tiles, "get_screen_center_lat_lng(ctx, center_lat, center_lng)"));
    assert(contains(linux_map_tiles, "latLngToTile(center_lat, center_lng, zoom, center_tile_x, center_tile_y)"));
    assert(not_contains(linux_map_tiles, "TRAIL_MATE_CARDPUTER_ZERO_LINUX"));

    const std::string linux_tile_cache = read_file(
        repo_root / "platform/linux/common/src/platform/linux/map_tile_cache.cpp");
    assert(contains(linux_tile_cache, "TRAIL_MATE_OSM_TILE_URL"));
    assert(contains(linux_tile_cache, "TRAIL_MATE_TERRAIN_TILE_URL"));
    assert(contains(linux_tile_cache, "TRAIL_MATE_SATELLITE_TILE_URL"));
    assert(contains(linux_tile_cache, "ensure_directory(path.parent_path())"));

    const std::string contour_generator = read_file(
        repo_root / "platform/linux/common/src/platform/linux/map_contour_tile_generator.cpp");
    assert(contains(contour_generator, "std::array<int, 4>{46, 32, 18, 255}"));
    assert(contains(contour_generator, "std::array<int, 4>{96, 68, 42, 235}"));
    assert(not_contains(contour_generator, "std::array<int, 4>{214, 193, 145, 220}"));

    const std::string linux_map_diagnostics = read_file(
        repo_root / "platform/linux/common/src/platform/linux/map_diagnostics.cpp");
    assert(contains(linux_map_diagnostics, "TRAIL_MATE_MAP_DOH_URL"));
    assert(contains(linux_map_diagnostics, "TRAIL_MATE_CURL_DOH_URL"));
    assert(contains(linux_map_diagnostics, "TRAIL_MATE_MAP_IP_RESOLVE"));
    assert(contains(linux_map_diagnostics, "TRAIL_MATE_CURL_IP_RESOLVE"));
    assert(not_contains(linux_map_diagnostics, "https://1.1.1.1/dns-query"));

    const std::string map_spec = read_file(
        repo_root / "docs/specification/MAP_TILE_SOURCE_CACHE_RUNTIME_SPEC.md");
    assert(contains(map_spec, "ESP32 / embedded SD runtimes"));
    assert(contains(map_spec, "Offline only. Check the SD/cache layout"));
    assert(contains(map_spec, "Linux compact LVGL runtimes, including Cardputer Zero"));
    assert(contains(map_spec, "Queue missing OSM/Terrain/Satellite tiles through Linux `MapTileCache`"));
    assert(contains(map_spec, "TRAIL_MATE_MAP_IP_RESOLVE"));
    assert(contains(map_spec, "Page-owned curl/download code"));
    return 0;
}
