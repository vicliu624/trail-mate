#include <cassert>
#include <cstdio>
#include <cstdlib>
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

std::size_t position_of(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    if (pos == std::string::npos)
    {
        std::fprintf(stderr, "Missing contract token: %s\n", needle);
        std::abort();
    }
    return pos;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];

    const std::string arduino_startup = read_file(
        repo_root / "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.cpp");
    const std::size_t board_init = position_of(
        arduino_startup,
        "boards::initializeBoardDisplayHardware");
    const std::size_t begin_log = position_of(
        arduino_startup,
        "debug::begin_sd_debug_log");
    const std::size_t display_init = position_of(
        arduino_startup,
        "display_runtime::initialize");
    const std::size_t begin_boot = position_of(
        arduino_startup,
        "startup_shell::beginBootUi");
    const std::size_t storage_init = position_of(
        arduino_startup,
        "boards::initializeStorage");
    const std::size_t export_core = position_of(
        arduino_startup,
        "debug::export_previous_coredump_to_sd");
    assert(board_init < display_init);
    assert(display_init < begin_boot);
    assert(begin_boot < storage_init);
    assert(storage_init < begin_log);
    assert(begin_boot < begin_log);
    assert(begin_log < export_core);

    const std::string header = read_file(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/debug/sd_debug_log.h");
    assert(contains(header, "begin_sd_debug_log"));
    assert(contains(header, "export_previous_coredump_to_sd"));

    const std::string implementation = read_file(
        repo_root / "platform/esp/arduino_common/src/debug/sd_debug_log.cpp");
    assert(contains(implementation, "/trailmate/debug/debug.log"));
    assert(contains(implementation, "/trailmate/debug/debug.prev.log"));
    assert(contains(implementation, "/trailmate/coredumps"));
    assert(contains(implementation, "kMaxDebugLogBytes = 256ULL * 1024ULL"));
    assert(contains(implementation, "esp_core_dump_image_get"));
    assert(contains(implementation, "esp_core_dump_image_check"));
    assert(contains(implementation, "ESP_PARTITION_SUBTYPE_DATA_COREDUMP"));
    assert(contains(implementation, "flash_addr - partition->address"));
    assert(contains(implementation, "esp_partition_read"));
    assert(contains(implementation, "esp_core_dump_image_erase"));
    assert(contains(implementation, "keeping flash copy"));
    assert(!contains(implementation, "esp_core_dump_get_summary"));
    assert(contains(implementation, "summary=deferred_to_offline_decoder"));
    assert(contains(implementation, "append_coredump_erase_result"));

    const std::string platformio = read_file(repo_root / "platformio.ini");
    assert(contains(platformio, "board_build.partitions = partitions.csv"));

    const std::string partitions = read_file(repo_root / "partitions.csv");
    assert(contains(partitions, "coredump, data, coredump"));
    assert(contains(partitions, "0xFF0000"));
    assert(contains(partitions, "0x10000"));

    const std::size_t write_payload = position_of(
        implementation,
        "export_coredump_payload(partition, path, partition_offset, size)");
    const std::size_t erase = position_of(
        implementation,
        "esp_core_dump_image_erase()");
    const std::size_t write_metadata = position_of(
        implementation,
        "write_coredump_metadata(path, size, flash_addr, check_result)");
    assert(write_payload < write_metadata);
    assert(write_metadata < erase);

    const std::string idf_startup = read_file(
        repo_root / "apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp");
    const std::size_t idf_display = position_of(
        idf_startup,
        "boards::initializeDisplay");
    const std::size_t idf_begin_boot = position_of(
        idf_startup,
        "showBootUi(config");
    const std::size_t idf_sd_ready = position_of(
        idf_startup,
        "bsp_runtime::ensure_sdcard_ready");
    const std::size_t idf_export_core = position_of(
        idf_startup,
        "idf_common::debug::export_previous_coredump_to_sd");
    assert(idf_display < idf_begin_boot);
    assert(idf_begin_boot < idf_sd_ready);
    assert(idf_sd_ready < idf_export_core);
    assert(contains(idf_startup,
                    "defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)"));
    const std::size_t p4_startup = position_of(
        idf_startup,
        "// T-Display-P4 puts the SD card and C6 companion");
    const std::size_t other_startup = idf_startup.find("#else", p4_startup);
    assert(other_startup != std::string::npos);
    const std::string p4_startup_body = idf_startup.substr(
        p4_startup,
        other_startup - p4_startup);
    const std::size_t p4_sd_ready = position_of(
        p4_startup_body,
        "bsp_runtime::ensure_sdcard_ready");
    const std::size_t p4_c6_ready = position_of(
        p4_startup_body,
        "wireless_companion::ensure_c6_companion_started");
    assert(p4_sd_ready < p4_c6_ready);

    const std::string idf_header = read_file(
        repo_root /
        "platform/esp/idf_common/include/platform/esp/idf_common/debug/sd_coredump_export.h");
    assert(contains(idf_header, "SdCoredumpExportStatus"));
    assert(contains(idf_header, "export_previous_coredump_to_sd"));

    const std::string idf_implementation = read_file(
        repo_root / "platform/esp/idf_common/src/debug/sd_coredump_export.cpp");
    assert(contains(idf_implementation, "storage::SdRuntimeFile"));
    assert(!contains(idf_implementation, "bsp_runtime::sdcard_mount_point"));
    assert(contains(idf_implementation, "trailmate"));
    assert(contains(idf_implementation, "coredumps"));
    assert(contains(idf_implementation, "esp_core_dump_image_get"));
    assert(contains(idf_implementation, "esp_core_dump_image_check"));
    assert(contains(idf_implementation, "ESP_PARTITION_SUBTYPE_DATA_COREDUMP"));
    assert(contains(idf_implementation, "flash_addr - partition->address"));
    assert(contains(idf_implementation, "esp_partition_read"));
    assert(contains(idf_implementation, "esp_core_dump_image_erase"));
    assert(contains(idf_implementation, "keeping flash copy"));

    const std::string idf_bsp_runtime = read_file(
        repo_root / "platform/esp/idf_common/src/bsp_runtime.cpp");
    const std::size_t sdcard_ready = position_of(
        idf_bsp_runtime,
        "bool sdcard_ready()");
    const std::size_t sdcard_mount_point = position_of(
        idf_bsp_runtime,
        "const char* sdcard_mount_point()");
    const std::string sdcard_ready_body = idf_bsp_runtime.substr(
        sdcard_ready,
        sdcard_mount_point - sdcard_ready);
    assert(contains(sdcard_ready_body, "return s_sdcard_ready;"));
    assert(!contains(sdcard_ready_body, "ensure_sdcard_ready()"));
    const std::size_t ensure_sdcard_ready = position_of(
        idf_bsp_runtime,
        "bool ensure_sdcard_ready()");
    const std::string ensure_sdcard_ready_body = idf_bsp_runtime.substr(
        ensure_sdcard_ready,
        sdcard_ready - ensure_sdcard_ready);
    assert(contains(ensure_sdcard_ready_body, "if (s_sdcard_attempted)"));
    assert(contains(ensure_sdcard_ready_body, "s_sdcard_attempted = true;"));
    assert(contains(ensure_sdcard_ready_body,
                    "continuing without storage"));

    const std::string idf_app_facade = read_file(
        repo_root / "apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp");
    const std::size_t group_sync = position_of(
        idf_app_facade,
        "void syncReticulumGroupConfig");
    const std::size_t normalize_config = position_of(
        idf_app_facade,
        "void normalizeIdfAppConfig");
    const std::string group_sync_body = idf_app_facade.substr(
        group_sync,
        normalize_config - group_sync);
    const std::size_t mounted_state_check = position_of(
        group_sync_body,
        "bsp_runtime::sdcard_ready()");
    const std::size_t group_load = position_of(
        group_sync_body,
        "reticulum_groups::load");
    assert(mounted_state_check < group_load);

    const std::string idf_sources = read_file(
        repo_root / "builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake");
    assert(contains(idf_sources, "platform/esp/idf_common/src/debug/sd_coredump_export.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/arduino_common/src/platform_ui_route_storage.cpp"));
    assert(!contains(idf_sources,
                     "platform/esp/idf_common/src/platform_ui_route_storage.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp"));
    assert(!contains(idf_sources,
                     "platform/esp/idf_common/src/platform_ui_pack_repository_runtime.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/arduino_common/src/ui/screens/team/team_ui_store.cpp"));
    assert(!contains(idf_sources,
                     "platform/esp/idf_common/src/platform_ui_team_ui_store_runtime.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_adapter.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_identity.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/radio/idf_lora_radio_pump.cpp"));
    assert(contains(idf_sources,
                    "platform/esp/idf_common/src/ble_manager_stub.cpp"));
    assert(contains(idf_app_facade,
                    "protocol == chat::MeshProtocol::MeshCore"));
    assert(contains(idf_app_facade,
                    "new chat::meshcore::MeshCoreAdapter"));
    assert(contains(idf_app_facade,
                    "ble::BleManager* getBleManager() override { return nullptr; }"));

    const std::string idf_lora_pump = read_file(
        repo_root / "platform/esp/radio/idf_lora_radio_pump.cpp");
    assert(contains(idf_lora_pump, "board_.getRadioIrqFlags()"));
    assert(contains(idf_lora_pump, "board_.getRadioPacketLength(true)"));
    assert(contains(idf_lora_pump, "board_.readRadioData"));
    const std::string idf_meshtastic = read_file(
        repo_root / "platform/esp/radio/meshtastic_radio_adapter.cpp");
    assert(contains(idf_meshtastic, "radio_pump_.poll(frame)"));
    const std::string esp_meshcore = read_file(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_adapter.cpp");
    assert(contains(esp_meshcore, "idf_radio_pump_.poll(frame)"));
    assert(contains(esp_meshcore,
                    "platform/esp/common/meshcore_runtime_compat.h"));

    const char* idf_targets[] = {
        "tab5",
        "tdeck",
        "t_display_p4_amoled",
        "t_display_p4_tft",
        "tlora_pager",
        "twatch",
    };
    for (const char* target : idf_targets)
    {
        const std::string defaults = read_file(
            repo_root / "builds/esp_idf/targets" / target / "sdkconfig.defaults");
        assert(contains(defaults, "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y"));
        assert(contains(defaults, "CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y"));
    }

    const std::string t_display_p4_tft_defaults = read_file(
        repo_root / "builds/esp_idf/targets/t_display_p4_tft/sdkconfig.defaults");
    assert(contains(t_display_p4_tft_defaults, "CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384"));
    assert(contains(t_display_p4_tft_defaults, "CONFIG_LV_USE_TJPGD=y"));
    assert(contains(t_display_p4_tft_defaults, "CONFIG_LV_USE_FS_POSIX=y"));
    assert(contains(t_display_p4_tft_defaults, "CONFIG_LV_FS_POSIX_LETTER=70"));
    assert(contains(t_display_p4_tft_defaults, "CONFIG_LV_FS_POSIX_PATH=\"/fs\""));
    const std::string t_display_p4_amoled_defaults = read_file(
        repo_root / "builds/esp_idf/targets/t_display_p4_amoled/sdkconfig.defaults");
    assert(contains(t_display_p4_amoled_defaults, "CONFIG_LV_USE_TJPGD=y"));
    assert(contains(t_display_p4_amoled_defaults, "CONFIG_LV_USE_FS_POSIX=y"));
    assert(contains(t_display_p4_amoled_defaults, "CONFIG_LV_FS_POSIX_LETTER=70"));
    assert(contains(t_display_p4_amoled_defaults, "CONFIG_LV_FS_POSIX_PATH=\"/fs\""));

    const std::string t_display_p4_runtime = read_file(
        repo_root /
        "platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp");
    assert(contains(t_display_p4_runtime,
                    "s_hi8561_touch_info_start_address + kHi8561TouchPointAddressOffset"));
    assert(contains(t_display_p4_runtime,
                    "uint8_t response[kHi8561SingleTouchPointDataSize]"));
    assert(contains(t_display_p4_runtime, "constexpr uint32_t kTouchI2cSpeedHz = 100000;"));
    assert(contains(t_display_p4_runtime, "kTouchI2cSpeedHz,"));
    assert(contains(t_display_p4_runtime,
                    "static_cast<uint16_t>((static_cast<uint16_t>(response[0]) << 8) | "
                    "response[1])"));
    assert(!contains(t_display_p4_runtime,
                     "const uint8_t finger_count = response[0];"));
    assert(contains(t_display_p4_runtime, "ESP_LOGI(kTag, \"touch press x=%ld y=%ld\""));
    assert(contains(t_display_p4_runtime, "dpi_cfg.num_fbs = 0;"));
    assert(contains(t_display_p4_runtime, "lvgl_port_add_disp_dsi(&display_cfg, &dsi_cfg);"));
    assert(contains(t_display_p4_runtime, "display_cfg.flags.buff_dma = true;"));
    assert(contains(t_display_p4_runtime, "display_cfg.flags.buff_spiram = true;"));
    assert(contains(t_display_p4_runtime, "display_cfg.flags.sw_rotate = true;"));
    assert(contains(t_display_p4_runtime, "display_cfg.flags.full_refresh = false;"));
    assert(contains(t_display_p4_runtime, "display_cfg.flags.direct_mode = false;"));
    assert(contains(t_display_p4_runtime, "dsi_cfg.flags.avoid_tearing = false;"));
    assert(contains(t_display_p4_runtime,
                    "lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_90);"));
    assert(!contains(t_display_p4_runtime, "P4DsiRotatingPresenter"));
    assert(!contains(t_display_p4_runtime, "esp_lcd_dpi_panel_get_frame_buffer"));
    assert(!contains(t_display_p4_runtime, "LV_DISPLAY_RENDER_MODE_FULL"));
    assert(!contains(t_display_p4_runtime, "lv_draw_sw_rotate("));
    assert(!contains(t_display_p4_runtime, "on_refresh_done"));
    assert(!std::filesystem::exists(
        repo_root / "platform/esp/idf_components/t_display_p4/p4_dsi_rotating_presenter.cpp"));
    assert(!std::filesystem::exists(
        repo_root / "platform/esp/idf_components/t_display_p4/p4_dsi_rotating_presenter.h"));
    const std::string t_display_p4_cmake = read_file(
        repo_root / "platform/esp/idf_components/t_display_p4/CMakeLists.txt");
    assert(!contains(t_display_p4_cmake, "p4_dsi_rotating_presenter.cpp"));
    assert(contains(t_display_p4_tft_defaults, "CONFIG_LVGL_PORT_ENABLE_PPA=y"));
    assert(contains(t_display_p4_amoled_defaults, "CONFIG_LVGL_PORT_ENABLE_PPA=y"));

    const std::string p4_display_architecture = read_file(
        repo_root / "docs/engineering/t-display-p4-display-runtime-architecture.md");
    assert(contains(p4_display_architecture, "`PARTIAL`"));
    assert(contains(p4_display_architecture, "on_color_trans_done"));
    assert(contains(p4_display_architecture, "P4DsiRotatingPresenter"));

    const std::string idf_sd_runtime = read_file(
        repo_root / "platform/esp/idf_common/src/sd_card_runtime_sdfat_adapter.cpp");
    // All native SDMMC targets share the bounded transfer policy; correctness
    // and batching are exercised with real SdFat in tests/sd_transport.
    assert(contains(idf_sd_runtime, "SdmmcSdTransferPolicy::dma_buffer_bytes"));
    assert(contains(idf_sd_runtime, "SdmmcSdTransferPolicy::max_direct_sectors"));
    assert(contains(idf_sd_runtime, "SdmmcSdTransferPolicy::file_slice_bytes"));
    assert(contains(idf_sd_runtime, "bool canUseMultiSectorDma"));
    assert(contains(idf_sd_runtime,
                    "sdmmc_read_sectors(card_, target, sector, batch)"));
    assert(contains(idf_sd_runtime, "bool validTransfer("));
    assert(contains(idf_sd_runtime,
                    "card_->host.check_buffer_alignment("));
    assert(contains(idf_sd_runtime, "sector_count > (SIZE_MAX / kSdSectorSize)"));

    assert(contains(idf_app_facade,
                    "constexpr std::size_t kDeferredRadioApplySlotCount = 3U;"));
    assert(contains(idf_app_facade,
                    "TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) ||"));
    assert(contains(idf_app_facade,
                    "void queueDeferredRadioApply(uint8_t flags)"));
    assert(contains(idf_app_facade,
                    "void flushDeferredRadioApplies()"));
    assert(contains(idf_app_facade,
                    "app::AppTasks::pauseRadioTasks()"));
    assert(contains(idf_app_facade,
                    "app::AppTasks::resumeRadioTasks();"));
    assert(contains(idf_app_facade,
                    "deferred_radio_apply_slots_[kDeferredRadioApplySlotCount]{};"));
    const std::string t_display_p4_keyboard = read_file(
        repo_root /
        "platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_keyboard.cpp");
    const std::size_t keyboard_initialize = position_of(
        t_display_p4_runtime,
        "trail_mate_t_display_p4_keyboard_initialize()");
    const std::size_t create_display = position_of(t_display_p4_runtime, "if (!create_display())");
    assert(keyboard_initialize < create_display);
    assert(contains(t_display_p4_keyboard,
                    "std::unique_ptr<cpp_bus_driver::Xl95x5> s_xl9555;"));
    assert(contains(t_display_p4_keyboard,
                    "std::unique_ptr<cpp_bus_driver::Tca8418> s_tca8418;"));
    assert(contains(t_display_p4_keyboard, "s_xl9555->Init()"));
    assert(contains(t_display_p4_keyboard, "s_tca8418->Init()"));
    assert(contains(t_display_p4_keyboard, "reset_tca8418_via_xl9555()"));
    assert(contains(t_display_p4_keyboard, "SetKeypadScanWindow"));
    assert(contains(t_display_p4_keyboard, "SetInterruptEnable"));
    assert(contains(t_display_p4_keyboard, "ClearIrqFlag"));
    assert(!contains(t_display_p4_runtime, "keyboard_recover_i2c_bus"));
    assert(!contains(t_display_p4_runtime, "kKeyboardAttachRecovery"));
    assert(!contains(t_display_p4_runtime, "monitor_power_recovery"));

    const std::string lvgl_fs_utils = read_file(
        repo_root / "modules/ui_shared/include/ui/support/lvgl_fs_utils.h");
    assert(contains(lvgl_fs_utils, "UI_FS_HAS_ARDUINO_FLASH_PACK_STORAGE"));
    assert(!contains(lvgl_fs_utils,
                     "platform/esp/idf_common/flash_storage_runtime.h"));
    const std::string pack_repository = read_file(
        repo_root / "platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp");
    assert(contains(pack_repository, "bool ensure_flash_pack_storage_ready"));
    assert(contains(pack_repository, "flash_storage_runtime::ensure_ready"));

    const std::string settings_components = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp");
    assert(contains(settings_components, "create_staged_list_content"));
    assert(contains(settings_components, "visible_item_layout_matches_current"));
    assert(contains(settings_components,
                    "settings kind=value visible_clean=0 staging_commit=0"));
    assert(contains(settings_components,
                    "settings kind=structure visible_clean=0 staging_commit=1"));
    assert(!contains(settings_components, "lv_obj_clean(g_state.list_panel);"));

    const std::string extensions_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/extensions/extensions_page_runtime.cpp");
    assert(contains(extensions_runtime, "create_staged_body_panel"));
    assert(contains(extensions_runtime,
                    "extensions install_complete package=%s full_app_rebuild=0"));
    assert(!contains(extensions_runtime, "render_current_view();\n            ui_request_rebuild_active_app();"));

    const std::string app_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/app_runtime.cpp");
    const std::size_t show_menu = position_of(
        app_runtime,
        "void show_menu_internal()");
    const std::size_t child_count_function = position_of(
        app_runtime,
        "uint32_t child_count");
    const std::string show_menu_body = app_runtime.substr(
        show_menu,
        child_count_function - show_menu);
    const std::size_t reveal_menu = position_of(
        show_menu_body,
        "ui::menu_layout::setMenuVisible(true)");
    const std::size_t activate_menu = position_of(
        show_menu_body,
        "ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu)");
    const std::size_t select_menu_tile = position_of(
        show_menu_body,
        "lv_tileview_set_tile_by_index(main_screen, 0, 0");
    assert(reveal_menu < activate_menu);
    assert(activate_menu < select_menu_tile);

    const std::string menu_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/menu/menu_runtime.cpp");
    const std::size_t set_menu_active = position_of(
        menu_runtime,
        "void setMenuActive(bool active)");
    const std::size_t handle_walkie_key = position_of(
        menu_runtime,
        "bool handleWalkieKey");
    const std::string set_menu_active_body = menu_runtime.substr(
        set_menu_active,
        handle_walkie_key - set_menu_active);
    assert(!contains(set_menu_active_body, "lv_timer_reset(s_runtime.time_timer)"));
    assert(!contains(set_menu_active_body, "lv_timer_reset(s_runtime.battery_timer)"));
    assert(contains(set_menu_active_body, "refreshTimeLabel()"));
    assert(contains(set_menu_active_body, "refreshBatteryLabel()"));
    assert(contains(set_menu_active_body, "refreshBottomBar()"));

    const std::string mesh_router = read_file(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/mesh_adapter_router.cpp");
    const std::size_t get_node_id = position_of(
        mesh_router,
        "NodeId MeshAdapterRouter::getNodeId() const");
    const std::size_t is_pki_ready = position_of(
        mesh_router,
        "bool MeshAdapterRouter::isPkiReady() const");
    const std::string get_node_id_body = mesh_router.substr(
        get_node_id,
        is_pki_ready - get_node_id);
    assert(contains(get_node_id_body, "defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)"));
    assert(contains(get_node_id_body, "LockGuard lock(mutex_, 0)"));

    const std::size_t get_identity = position_of(
        mesh_router,
        "bool MeshAdapterRouter::getReticulumLocalIdentityInfo");
    const std::size_t has_pki_key = position_of(
        mesh_router,
        "bool MeshAdapterRouter::hasPkiKey");
    const std::string get_identity_body = mesh_router.substr(
        get_identity,
        has_pki_key - get_identity);
    assert(contains(get_identity_body, "defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)"));
    assert(contains(get_identity_body, "LockGuard lock(mutex_, 0)"));

    const std::string t_display_p4_board_header = read_file(
        repo_root / "boards/t_display_p4/include/boards/t_display_p4/t_display_p4_board.h");
    assert(contains(t_display_p4_board_header,
                    "std::atomic<TaskHandle_t> system_i2c_owner_task_{nullptr};"));
    assert(contains(t_display_p4_board_header,
                    "std::atomic<TaskHandle_t> system_i2c_waiter_task_{nullptr};"));
    const std::string t_display_p4_board = read_file(
        repo_root / "boards/t_display_p4/src/t_display_p4_board.cpp");
    assert(contains(t_display_p4_board,
                    "SYS I2C lock timeout requester=%s request_owner=%s at=%s:%d"));
    assert(contains(t_display_p4_board,
                    "owner=%s owner_task=%s held_ms=%lu waiter=%s waiter_task=%s"));
    assert(contains(t_display_p4_board, "ldo_config.flags.adjustable = 1;"));
    assert(contains(t_display_p4_board, "ldo_config.flags.owned_by_hw = 0;"));
    assert(!contains(t_display_p4_board, "ldo_config.flags.bypass"));
    assert(contains(t_display_p4_board,
                    "return ensure_external_3v3_power_control();"));
    assert(!contains(t_display_p4_board,
                     "recoverExternal3v3ForKeyboardAttach"));
    assert(!contains(t_display_p4_board,
                     "Keyboard attach recovery"));
    const std::size_t managed_i2c_device = position_of(
        t_display_p4_board,
        "i2c_master_dev_handle_t TDisplayP4Board::getManagedSystemI2cDevice");
    const std::size_t expander_ready = position_of(
        t_display_p4_board,
        "bool TDisplayP4Board::expanderReady");
    const std::string managed_i2c_device_body = t_display_p4_board.substr(
        managed_i2c_device,
        expander_ready - managed_i2c_device);
    const std::size_t resource_lock = position_of(
        managed_i2c_device_body,
        "std::lock_guard<std::mutex> resource_lock(resource_mutex_);");
    const std::size_t system_i2c_lock = position_of(
        managed_i2c_device_body,
        "if (!lockSystemI2c(timeout_ms, config.owner, __FILE__, __LINE__))");
    assert(resource_lock < system_i2c_lock);
    const std::size_t transmit_radio = position_of(
        t_display_p4_board,
        "int TDisplayP4Board::transmitRadio");
    const std::size_t start_radio_receive = position_of(
        t_display_p4_board,
        "int TDisplayP4Board::startRadioReceive");
    const std::string transmit_radio_body = t_display_p4_board.substr(
        transmit_radio,
        start_radio_receive - transmit_radio);
    assert(contains(transmit_radio_body, "const uint32_t irq_flags = radio().getIrqFlags();"));
    assert(contains(transmit_radio_body, "kRadioIrqTxDone"));
    assert(contains(transmit_radio_body, "kRadioIrqTimeout"));
    assert(contains(transmit_radio_body,
                    "wireless_companion::c6_companion().poll()"));
    assert(contains(transmit_radio_body,
                    "%s transmit timeout len=%u elapsed_ms=%lu"));
    assert(!contains(transmit_radio_body, "readLoraDio1"));

    const std::string c6_companion_runtime = read_file(
        repo_root / "platform/esp/idf_common/src/c6_companion_runtime.cpp");
    assert(contains(c6_companion_runtime, "sdmmc_host_runtime::initialize_slot"));
    assert(contains(c6_companion_runtime, "sdmmc_host_runtime::release_slot"));
    assert(!contains(c6_companion_runtime, "sdmmc_host_deinit();"));
    assert(contains(c6_companion_runtime, "send_retryable_control_frame"));
    assert(contains(c6_companion_runtime, "\"wifi_control\""));
    assert(contains(c6_companion_runtime, "\"config_set\""));
    assert(contains(c6_companion_runtime,
                    "TCP data deliberately stays on the non-retrying send_frame() path"));

    const std::string sdmmc_host_runtime = read_file(
        repo_root / "platform/esp/idf_common/src/sdmmc_host_runtime.cpp");
    assert(contains(sdmmc_host_runtime, "std::mutex s_lifecycle_mutex;"));
    assert(contains(sdmmc_host_runtime, "sdmmc_host_deinit_slot(slot)"));
    assert(contains(sdmmc_host_runtime,
                    "Do not call forceful sdmmc_host_deinit() here"));

    const std::string idf_runtime_config = read_file(
        repo_root / "apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp");
    assert(contains(idf_runtime_config,
                    "\"t_display_p4_amoled_app_loop\",\n        10,\n        12288,"));
    assert(contains(idf_runtime_config,
                    "\"t_display_p4_tft_app_loop\",\n        10,\n        12288,"));

    const std::string doc = read_file(
        repo_root / "docs/devices/esp32-sd-debug-coredump.md");
    assert(contains(doc, "ESP32 shared diagnostics"));
    assert(contains(doc, "SD-capable ESP32 product paths"));
    assert(contains(doc, "Pager"));
    assert(contains(doc, "T-Deck"));
    assert(contains(doc, "Tab5"));
    assert(contains(doc, "T-Display-P4"));
    assert(contains(doc, "next normal boot"));
    assert(contains(doc, "does not write FAT/exFAT files from the panic handler"));
    assert(contains(doc, "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH"));
    assert(contains(doc, "/trailmate/coredumps/core-*.elf"));
    return 0;
}
