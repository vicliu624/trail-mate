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

std::size_t position_of_after(const std::string& haystack,
                              const char* needle,
                              std::size_t offset)
{
    const auto pos = haystack.find(needle, offset);
    assert(pos != std::string::npos);
    return pos;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];

    const std::string registry = read_file(
        repo_root / "modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp");

    assert(contains(registry, "std::string translations_path;"));
    assert(contains(registry, "bool translations_loaded = false;"));
    const std::size_t lazy_translation_loader =
        position_of(registry, "bool ensure_locale_translations_loaded(LocalePackRecord& locale)");
    const std::size_t lazy_translation_release =
        position_of(registry, "void release_inactive_locale_translations()");
    const std::size_t locale_catalog =
        position_of(registry, "bool catalog_external_locale_pack(const std::string& pack_dir)");
    const std::size_t locale_activation =
        position_of(registry, "bool activate_locale_internal(LocalePackRecord* locale");
    const std::size_t catalog_translation_release = position_of_after(
        registry, "decltype(pack.translations){}.swap(pack.translations);", locale_catalog);
    const std::size_t catalog_translation_path =
        position_of_after(registry, "pack.translations_path = strings_path;", locale_catalog);
    const std::size_t activation_previous_release = position_of_after(
        registry, "release_locale_translations(*s_active_locale);", locale_activation);
    const std::size_t activation_translation_load = position_of_after(
        registry, "ensure_locale_translations_loaded(*locale)", locale_activation);
    const std::size_t activation_inactive_release = position_of_after(
        registry, "release_inactive_locale_translations();", locale_activation);
    assert(lazy_translation_loader < lazy_translation_release);
    assert(lazy_translation_release < locale_catalog);
    assert(locale_catalog < catalog_translation_release);
    assert(catalog_translation_release < catalog_translation_path);
    assert(locale_activation < activation_translation_load);
    assert(activation_previous_release < activation_translation_load);
    assert(activation_translation_load < activation_inactive_release);
    const std::size_t font_heap_log =
        position_of(registry, "void log_external_font_load_heap(const FontPackRecord& pack");
    const std::size_t font_load = position_of(registry, "bool load_font_pack(FontPackRecord& pack)");
    const std::size_t font_heap_before =
        position_of_after(registry, "log_external_font_load_heap(pack, \"before\");", font_load);
    const std::size_t font_heap_after =
        position_of_after(registry, "log_external_font_load_heap(pack, \"after\");", font_load);
    assert(font_heap_log < font_load);
    assert(font_load < font_heap_before);
    assert(font_heap_before < font_heap_after);

    assert(contains(registry, "constexpr bool kAllowSynchronousContentSupplementFontLoad = false;"));
    assert(contains(registry, "constexpr bool kAllowDeferredContentSupplementFontLoad = true;"));
    assert(contains(registry, "font_load_overlay_policy()"));
    assert(contains(registry, "Policy::Overlay"));
    assert(not_contains(registry, "Policy::OverlayImmediate"));
    assert(contains(registry, "font_load_overlay_policy(),"));
    assert(contains(registry, "FontPackRecord* s_pending_content_supplement_load = nullptr;"));
    assert(contains(registry, "lv_timer_t* s_content_supplement_retry_timer = nullptr;"));
    assert(contains(registry, "s_content_supplement_load_not_before_frame"));
    assert(contains(registry, "void on_lvgl_frame_completed()"));
    assert(contains(registry, "bool request_locale_by_index"));
    const std::size_t cancel_locale =
        position_of(registry, "void cancel_pending_locale_change()");
    const std::size_t rebuild_registry = position_of(registry, "void rebuild_registry()");
    const std::size_t cancel_before_clear = position_of_after(
        registry, "cancel_pending_locale_change();", rebuild_registry);
    assert(cancel_locale < rebuild_registry);
    assert(rebuild_registry < cancel_before_clear);

    const std::size_t hot_path_guard = position_of(registry, "bool can_activate_content_supplement_for_text");
    const std::size_t hot_path_check = position_of(registry, "can_load_font_from_content_hot_path(pack)");
    const std::size_t budget_check = position_of(registry, "can_add_content_supplement(pack)");
    assert(hot_path_guard < hot_path_check);
    assert(hot_path_check < budget_check);
    const std::size_t deferred_guard =
        position_of(registry, "bool can_schedule_deferred_content_supplement_load");
    assert(hot_path_guard < deferred_guard);
    assert(contains(registry, "can_load_font_from_activation_path(pack)"));
    assert(contains(registry, "bool prepare_content_font_for_text"));
    const std::size_t prepare_content =
        position_of(registry, "bool prepare_content_font_for_text");
    const std::string prepare_content_body = registry.substr(prepare_content);
    assert(not_contains(prepare_content_body,
                        "ScopedExternalFontActivation activation(force_overlay);"));

    const std::size_t content_ensure = position_of(registry, "bool ensure_content_font_for_text");
    const std::size_t supplement_check = position_of(
        registry,
        "!can_activate_content_supplement_for_text(*candidate)");
    const std::size_t hot_path_reason = position_of_after(registry, "\"ui_hot_path\"", supplement_check);
    const std::size_t content_budget_reason = position_of_after(registry, "\"content_budget\"", supplement_check);
    const std::size_t deferred_candidate_guard = position_of_after(
        registry,
        "can_schedule_deferred_content_supplement_load(*candidate)",
        supplement_check);
    const std::size_t deferred_queue = position_of_after(
        registry,
        "queue_deferred_content_supplement_load(*candidate, reason);",
        deferred_candidate_guard);
    const std::size_t deferred_skip = position_of_after(
        registry,
        "ui_hot_path_no_deferred_load",
        deferred_candidate_guard);
    assert(content_ensure < supplement_check);
    assert(supplement_check < hot_path_reason);
    assert(supplement_check < content_budget_reason);
    assert(supplement_check < deferred_candidate_guard);
    assert(deferred_candidate_guard < deferred_queue);
    assert(deferred_candidate_guard < deferred_skip);

    const std::size_t post_frame_schedule =
        position_of(registry, "bool schedule_deferred_content_supplement_async");
    const std::size_t post_frame_target = position_of_after(
        registry,
        "s_content_supplement_load_not_before_frame = s_completed_lvgl_frame_count + 2U;",
        post_frame_schedule);
    const std::size_t retry_timer = position_of(
        registry,
        "lv_timer_create(deferred_content_supplement_retry_timer_cb");
    assert(post_frame_schedule < post_frame_target);
    assert(post_frame_schedule < retry_timer);

    const std::size_t async_callback =
        position_of(registry, "void deferred_content_supplement_load_cb");
    const std::size_t loader_call =
        position_of_after(registry, "ensure_font_pack_loaded(pack)", async_callback);
    const std::size_t append_call = position_of_after(
        registry,
        "append_unique_pack(s_content_supplement_packs, pack);",
        async_callback);
    const std::size_t rebuild_call =
        position_of_after(registry, "rebuild_runtime_font_chains();", append_call);
    const std::size_t invalidate_call = position_of_after(
        registry,
        "invalidate_active_screen_after_font_chain_change();",
        rebuild_call);
    assert(async_callback < loader_call);
    assert(loader_call < append_call);
    assert(append_call < rebuild_call);
    assert(rebuild_call < invalidate_call);

    const std::string network_page = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/network/network_page_shell.cpp");
    assert(contains(network_page, "NetworkPageState* s_page_state = nullptr;"));
    assert(contains(network_page,
                    "heap_caps_malloc(sizeof(NetworkPageState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);"));
    assert(contains(network_page, "bool ensure_network_page_state()"));
    assert(contains(network_page, "if (!parent || !ensure_network_page_state()"));
    assert(contains(network_page, "void release_network_page_state()"));
    assert(contains(network_page, "release_network_page_state();"));
    assert(contains(network_page, "prepare_content_font_for_text(g_state.page_body.data(), true)"));
    assert(contains(network_page, "::ui::fonts::apply_content_font(label,"));
    assert(contains(network_page, "::ui::fonts::apply_content_font(text,"));

    const std::string map_tiles = read_file(
        repo_root / "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp");
    assert(not_contains(map_tiles, "s_map_tile_worker_task_stack"));
    assert(not_contains(map_tiles, "s_map_tile_worker_task_tcb"));
    assert(contains(map_tiles, "const BaseType_t ok = xTaskCreate(taskThunk,"));
    assert(contains(map_tiles, "stack=dynamic_internal bytes=%u internal_free=%u"));

    const std::string reticulum_page = read_file(
        repo_root / "platform/esp/arduino_common/src/platform_ui_reticulum_directory_runtime.cpp");
    assert(not_contains(reticulum_page, "s_page_cache_task_stack"));
    assert(not_contains(reticulum_page, "s_page_cache_task_tcb"));
    assert(contains(reticulum_page,
                    "const BaseType_t task_ok = xTaskCreate(page_cache_load_task_entry,"));
    assert(contains(reticulum_page, "stack=dynamic_internal bytes=%u"));

    const std::string gps_page = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp");
    assert(not_contains(gps_page, "UI_GPS_PAGE_STATE_RAM_ATTR"));
    assert(contains(gps_page, "::ui::map::MapOverlaySnapshot* s_overlay_snapshot = nullptr;"));
    assert(contains(gps_page,
                    "heap_caps_malloc(sizeof(::ui::map::MapOverlaySnapshot),"));
    assert(contains(gps_page, "bool ensure_overlay_snapshot()"));
    assert(contains(gps_page, "projection == Projection::Map && !ensure_overlay_snapshot()"));
    assert(contains(gps_page, "void release_overlay_snapshot()"));
    assert(contains(gps_page, "release_overlay_snapshot();"));

    const std::string feedback = read_file(
        repo_root / "modules/ui_shared/src/ui/runtime/ui_feedback.cpp");
    assert(not_contains(feedback, "UI_FEEDBACK_STATE_RAM_ATTR"));
    assert(contains(feedback, "struct FeedbackRuntimeState"));
    assert(contains(feedback, "FeedbackRuntimeState* s_feedback_state = nullptr;"));
    assert(contains(feedback,
                    "heap_caps_malloc(sizeof(FeedbackRuntimeState),"));
    assert(contains(feedback, "FeedbackRuntimeState* ensure_feedback_state()"));
    assert(contains(feedback, "FeedbackRuntimeState* state = ensure_feedback_state();"));

    const std::string font_utils = read_file(
        repo_root / "modules/ui_shared/src/ui/assets/fonts/font_utils.cpp");
    assert(not_contains(font_utils, "UI_FONT_STATE_RAM_ATTR"));
    assert(contains(font_utils,
                    "LocalizedFontBinding* s_localized_font_bindings = nullptr;"));
    assert(contains(font_utils,
                    "LocalizedFontBinding* ensure_localized_font_binding_storage()"));
    assert(contains(font_utils, "heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)"));
    assert(contains(font_utils, "return localized_font_binding_storage() ? kMaxLocalizedFontBindings : 0;"));

    const std::string energy_sweep = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp");
    assert(not_contains(energy_sweep, "UI_PACKET_PROBE_STATE_RAM_ATTR"));
    assert(contains(energy_sweep, "struct PacketProbePageState"));
    assert(contains(energy_sweep, "PacketProbePageState* s_page_state = nullptr;"));
    assert(contains(energy_sweep,
                    "heap_caps_malloc(sizeof(PacketProbePageState),"));
    assert(contains(energy_sweep, "bool ensure_page_state()"));
    assert(contains(energy_sweep, "if (!parent || !ensure_page_state())"));
    assert(contains(energy_sweep, "void release_page_state()"));
    assert(contains(energy_sweep, "release_page_state();"));

    const std::string gnss_skyplot = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp");
    assert(not_contains(gnss_skyplot, "UI_GNSS_STATE_RAM_ATTR"));
    assert(contains(gnss_skyplot, "struct SkyPlotPageState final"));
    assert(contains(gnss_skyplot, "SkyPlotPageState* s_page_state = nullptr;"));
    assert(contains(gnss_skyplot,
                    "heap_caps_malloc(sizeof(SkyPlotPageState),"));
    assert(contains(gnss_skyplot, "bool ensure_page_state()"));
    assert(contains(gnss_skyplot, "if (!parent || !ensure_page_state())"));
    assert(contains(gnss_skyplot, "void release_page_state()"));
    assert(contains(gnss_skyplot, "release_page_state();"));

    const std::string display_runtime = read_file(
        repo_root / "platform/esp/arduino_common/src/display_runtime.cpp");
    const std::size_t root_handler = position_of(display_runtime, "lv_timer_handler();");
    const std::size_t post_frame = position_of_after(
        display_runtime,
        "::ui::i18n::on_lvgl_frame_completed();",
        root_handler);
    assert(root_handler < post_frame);

    const std::string lv_helper = read_file(
        repo_root / "platform/esp/arduino_common/src/LV_Helper_v9.cpp");
    assert(contains(lv_helper, "bool lvgl_external_font_load_uses_strict_psram()"));
    const std::size_t strict_psram_scope = position_of(
        lv_helper, "if (::platform::ui::psramUiRequired() || lvgl_external_font_load_uses_strict_psram())");
    const std::size_t strict_psram_malloc = position_of_after(
        lv_helper,
        "heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);",
        strict_psram_scope);
    const std::size_t strict_psram_realloc = position_of_after(
        lv_helper,
        "heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);",
        strict_psram_scope);
    assert(strict_psram_scope < strict_psram_malloc);
    assert(strict_psram_malloc < strict_psram_realloc);

    const std::string presenter = read_file(
        repo_root / "modules/ui_shared/src/ui/widgets/progress_overlay_presenter.cpp");
    assert(contains(presenter, "ProgressOverlayPresenter::request_present"));
    assert(not_contains(presenter, "lv_timer_handler("));
    assert(not_contains(presenter, "lv_refr_now("));

    const std::string foreground_header = read_file(
        repo_root / "modules/ui_shared/include/ui/widgets/foreground_operation_overlay.h");
    assert(not_contains(foreground_header, "OverlayImmediate"));

    const std::string screen_saver = read_file(
        repo_root / "modules/ui_shared/src/ui/components/screen_saver_overlay.cpp");
    assert(contains(screen_saver, "void request_present()"));
    assert(not_contains(screen_saver, "lv_refr_now("));

    const std::string settings = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp");
    assert(contains(settings, "request_locale_by_index("));
    assert(contains(settings, "on_locale_change_completed"));

    const std::string lxmf = read_file(
        repo_root / "platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter.cpp");
    const std::size_t ping_entry = position_of(
        lxmf, "MeshActionResult LxmfAdapter::pingReticulumDestination(");
    const std::size_t ping_dispatch = position_of_after(
        lxmf, "return queuePendingReticulumPing(destination.destination_hash);", ping_entry);
    const std::size_t ping_sender = position_of_after(
        lxmf, "MeshActionResult LxmfAdapter::sendReticulumPingToPeer(", ping_entry);
    assert(ping_entry < ping_dispatch);
    assert(ping_dispatch < ping_sender);

    return 0;
}
