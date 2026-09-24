#include "ui/components/two_pane_styles.h"
#include "ui/page/page_profile.h"
#include "ui/screens/geocaching/geocaching_page_shell.h"
#include "ui/screens/gps/gps_page_runtime.h"
#include "ui/widgets/top_bar_power_presenter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace gps::ui::runtime
{
const shell::Host* test_map_host = nullptr;
MapTarget test_map_target;
bool enter_target(const shell::Host* host, lv_obj_t*, const MapTarget& target)
{
    test_map_host = host;
    test_map_target = target;
    return true;
}
void exit(lv_obj_t*) { test_map_host = nullptr; }
} // namespace gps::ui::runtime

namespace ui::widgets::top_bar_power
{
void bind(TopBar& bar) { lv_label_set_text(bar.right_label, LV_SYMBOL_BATTERY_FULL " 100%"); }
void unbind(TopBar&) {}
} // namespace ui::widgets::top_bar_power
struct TestSource : ui::geocaching::Source
{
    ui::geocaching::DraftInput draft;
    std::string draft_name, draft_description, draft_hint;
    bool has_draft = false, fail_save = false, snapshot_busy = false;
    unsigned pending_reads = 0;
    unsigned cancelled_reads = 0;
    bool fail_read = false;
    unsigned publications = 0;
    uint8_t current_author = 7;
    unsigned reads = 0, opens = 0, refreshes = 0, downloads = 0;
    size_t saved_index = SIZE_MAX;
    size_t last_open = 0;
    uint64_t generation = 1;
    size_t requested_offset = 0, requested_count = 0;
    unsigned pending_windows = 0;
    ui::geocaching::Section requested_section = ui::geocaching::Section::Discover;
    void requestWindow(ui::geocaching::Section section, size_t offset, size_t count) override
    {
        requested_section = section;
        requested_offset = offset;
        requested_count = count;
    }
    void snapshot(ui::geocaching::Section section, ui::geocaching::Snapshot& out) override
    {
        out = {};
        if (snapshot_busy)
        {
            out.busy = true;
            std::snprintf(out.status.data(), out.status.size(), "Updating...");
            return;
        }
        out.generation = generation;
        out.count = 20;
        out.can_refresh = true;
        if (section == ui::geocaching::Section::Published)
        {
            out.count = has_draft ? 1 : 0;
            out.can_create = true;
        }
        std::snprintf(out.status.data(), out.status.size(), "20 shared caches");
    }
    bool item(ui::geocaching::Section section, size_t index, uint64_t expected, ui::geocaching::Item& out) override
    {
        ++reads;
        if (!requested_count || requested_count > 4 ||
            (section == ui::geocaching::Section::Published && requested_section != section)) return false;
        if (pending_windows)
        {
            --pending_windows;
            return false;
        }
        if (index >= 20 || expected != generation) return false;
        out = {};
        if (section == ui::geocaching::Section::Published)
        {
            if (!has_draft || index) return false;
            out.is_draft = true;
            out.edit_generation = draft.generation;
            std::memcpy(out.id.data(), draft.id.data(), 16);
            std::snprintf(out.name.data(), out.name.size(), "%s", draft_name.c_str());
            return true;
        }
        out.id[0] = static_cast<uint8_t>(index);
        out.downloaded = saved_index == index;
        out.can_download = !out.downloaded;
        std::snprintf(out.name.data(), out.name.size(), "Cache %02u - woodland trail", unsigned(index + 1));
        std::snprintf(out.detail.data(), out.detail.size(), "30.5000000, 120.5000000\nDifficulty 2.0 / Terrain 2.5\nDirectory preview - not yet downloaded");
        return true;
    }
    void refresh(ui::geocaching::Section) override
    {
        ++refreshes;
        ++generation;
    }
    void open(const ui::geocaching::Item& item, uint64_t expected) override
    {
        if (expected == generation)
        {
            ++opens;
            last_open = item.id[0];
        }
    }
    bool download(const ui::geocaching::Item& item, uint64_t expected) override
    {
        if (expected != generation) return false;
        ++downloads;
        saved_index = item.id[0];
        ++generation;
        return true;
    }
    bool saveDraft(ui::geocaching::DraftInput& input) override
    {
        if (fail_save) return true;
        input.id.fill(0x42);
        draft = input;
        ++draft.generation;
        draft_name = input.name;
        draft_description = input.description;
        draft_hint = input.hint;
        draft.name = draft_name;
        draft.description = draft_description;
        draft.hint = draft_hint;
        has_draft = true;
        ++generation;
        return true;
    }
    ui::geocaching::DraftSaveStatus draftSaveStatus(const std::array<uint8_t, 16>&, uint64_t) override
    {
        return fail_save ? ui::geocaching::DraftSaveStatus::Failed : ui::geocaching::DraftSaveStatus::Saved;
    }
    ui::geocaching::DraftReadStatus readDraft(const std::array<uint8_t, 16>& id, void (*sink)(const ui::geocaching::DraftInput&, void*), void* context) override
    {
        if (pending_reads)
        {
            --pending_reads;
            return ui::geocaching::DraftReadStatus::Pending;
        }
        if (fail_read) return ui::geocaching::DraftReadStatus::Failed;
        if (!has_draft || id != draft.id) return ui::geocaching::DraftReadStatus::Failed;
        sink(draft, context);
        return ui::geocaching::DraftReadStatus::Ready;
    }
    void cancelDraftRead(const std::array<uint8_t, 16>& id) override
    {
        if (id == draft.id) ++cancelled_reads;
    }
    bool publicationAuthor(const std::array<uint8_t, 16>& id, uint64_t version, std::array<uint8_t, 64>& author, uint32_t* from, uint32_t* to) override
    {
        if (!has_draft || id != draft.id || version != draft.generation) return false;
        author.fill(current_author);
        if (from) *from = 0;
        if (to) *to = 1;
        return true;
    }
    bool publishDraft(const std::array<uint8_t, 16>& id, uint64_t version, const std::array<uint8_t, 64>& expected, uint32_t revision) override
    {
        std::array<uint8_t, 64> current;
        if (!publicationAuthor(id, version, current, nullptr, nullptr) || current != expected || revision != 1) return false;
        ++publications;
        return true;
    }
};
bool save(const std::string& path, lv_obj_t* screen)
{
    lv_obj_update_layout(screen);
    auto* image = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB888);
    if (!image) return false;
    std::ofstream out(path, std::ios::binary);
    out << "P6\n"
        << image->header.w << " " << image->header.h << "\n255\n";
    for (unsigned y = 0; y < image->header.h; ++y)
        for (unsigned x = 0; x < image->header.w; ++x)
        {
            const auto* pixel = image->data + y * image->header.stride + x * 3;
            const char rgb[] = {char(pixel[2]), char(pixel[1]), char(pixel[0])};
            out.write(rgb, 3);
        }
    lv_draw_buf_destroy(image);
    return out.good();
}
int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    const int width = std::atoi(argv[1]), height = std::atoi(argv[2]);
    lv_init();
    auto* display = lv_display_create(width, height);
    std::vector<uint8_t> pixels(width * 32 * 4);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* display, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(display); });
    const auto profile = width == 480 ? ui::page_profile::make_pager_profile() : ui::page_profile::make_tdeck_profile();
    ui::page_profile::set_active_profile(&profile);
    auto* screen = lv_screen_active();
    auto* old_group = lv_group_create();
    lv_group_set_default(old_group);
    unsigned exits = 0;
    ui::page::Host host{&exits, [](void* context)
                        { ++*static_cast<unsigned*>(context); }};
    geocaching::ui::shell::enter(&host, screen);
    auto* root = lv_obj_get_child(screen, 0);
    if (!root || !save(std::string(argv[3]) + "-empty.ppm", screen)) return 2;
    auto* tabs = lv_obj_get_child(root, 1);
    for (unsigned i = 0; i < 3; ++i)
    {
        auto* tab = lv_obj_get_child(tabs, i);
        if (lv_color_to_u32(lv_obj_get_style_bg_color(tab, LV_PART_MAIN)) == lv_color_to_u32(lv_palette_main(LV_PALETTE_BLUE))) return 3;
    }
    TestSource source;
    geocaching::ui::shell::bind(&source);
    lv_tick_inc(501);
    lv_timer_handler();
    auto* list = lv_obj_get_child(root, 3);
    const auto visible = lv_obj_get_child_count(list);
    if (visible < 2 || visible > 4 || source.reads != visible) return 4;
    if (!save(std::string(argv[3]) + "-list.ppm", screen)) return 5;
    auto* first_row = lv_obj_get_child(list, 0);
    const auto reads_before_busy = source.reads;
    source.snapshot_busy = true;
    lv_tick_inc(600);
    lv_timer_handler();
    if (lv_obj_get_child_count(list) != visible || lv_obj_get_child(list, 0) != first_row ||
        source.reads != reads_before_busy || std::strcmp(lv_label_get_text(lv_obj_get_child(root, 2)), "20 shared caches")) return 60;
    lv_obj_send_event(lv_obj_get_child(tabs, 1), LV_EVENT_CLICKED, nullptr);
    if (lv_obj_get_child_count(list) != 1 ||
        std::strcmp(lv_label_get_text(lv_obj_get_child(list, 0)), "Loading caches...")) return 61;
    lv_obj_send_event(lv_obj_get_child(tabs, 0), LV_EVENT_CLICKED, nullptr);
    source.snapshot_busy = false;
    lv_tick_inc(600);
    lv_timer_handler();
    if (lv_obj_get_child_count(list) != visible) return 62;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    if (source.opens != 1 || source.last_open != 0) return 6;
    if (!save(std::string(argv[3]) + "-detail.ppm", screen)) return 11;
    auto* details_footer = lv_obj_get_child(root, 4);
    const auto detail_reads = source.reads;
    source.snapshot_busy = true;
    lv_tick_inc(600);
    lv_timer_handler();
    if (source.reads != detail_reads || lv_obj_has_state(lv_obj_get_child(details_footer, 3), LV_STATE_DISABLED) ||
        std::strcmp(lv_label_get_text(lv_obj_get_child(list, 0)), "Cache 01 - woodland trail")) return 63;
    source.snapshot_busy = false;
    lv_obj_send_event(lv_obj_get_child(details_footer, 3), LV_EVENT_CLICKED, nullptr);
    if (source.downloads != 1 || source.saved_index != 0 ||
        lv_obj_has_state(lv_obj_get_child(details_footer, 3), LV_STATE_DISABLED)) return 14;
    lv_obj_send_event(lv_obj_get_child(details_footer, 3), LV_EVENT_CLICKED, nullptr);
    if (!gps::ui::runtime::test_map_host || !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN) ||
        std::strcmp(gps::ui::runtime::test_map_target.name, "Cache 01 - woodland trail")) return 15;
    source.saved_index = 1;
    ++source.generation;
    ui::map::MapOverlaySnapshot overlays;
    const auto& map_target = gps::ui::runtime::test_map_target;
    if (!map_target.append_overlays) return 17;
    map_target.append_overlays(map_target.overlay_context, overlays);
    if (overlays.item_count != 1 || overlays.items[0].kind != ui::map::MapOverlayKind::Geocache) return 18;
    overlays.item_count = ui::map::MapOverlaySnapshot::kMaxItems;
    map_target.append_overlays(map_target.overlay_context, overlays);
    if (!overlays.truncated || overlays.item_count != ui::map::MapOverlaySnapshot::kMaxItems) return 19;
    if (!map_target.select_overlay || overlays.items[0].stable_id != 2) return 20;
    map_target.select_overlay(map_target.overlay_context, overlays.items[0].stable_id);
    ui::page::request_exit(gps::ui::runtime::test_map_host);
    ui::page::request_exit(gps::ui::runtime::test_map_host);
    lv_tick_inc(10);
    lv_timer_handler();
    if (gps::ui::runtime::test_map_host || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN) || exits) return 16;
    if (std::strcmp(lv_label_get_text(lv_obj_get_child(list, 0)), "Cache 02 - woodland trail")) return 21;
    lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(root, 0), 0), LV_EVENT_CLICKED, nullptr);
    if (exits || lv_obj_get_child_count(list) != visible) return 12;
    auto* footer = lv_obj_get_child(root, 4);
    source.pending_windows = 1;
    lv_obj_send_event(lv_obj_get_child(footer, 3), LV_EVENT_CLICKED, nullptr);
    if (source.requested_offset != visible || source.requested_count != visible ||
        !lv_obj_has_state(lv_obj_get_child(footer, 3), LV_STATE_DISABLED)) return 51;
    lv_tick_inc(600);
    lv_timer_handler();
    if (lv_obj_get_child_count(list) != visible || source.requested_offset != visible ||
        lv_obj_has_state(lv_obj_get_child(footer, 3), LV_STATE_DISABLED)) return 52;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    if (source.opens != 2 || source.last_open != visible) return 7;
    lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(root, 0), 0), LV_EVENT_CLICKED, nullptr);
    if (exits) return 13;
    lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(root, 0), 0), LV_EVENT_CLICKED, nullptr);
    if (exits != 1) return 8;
    geocaching::ui::shell::exit(nullptr, screen);
    if (lv_obj_get_child_count(screen) || lv_group_get_default() != old_group) return 9;
    geocaching::ui::shell::enter(&host, screen);
    root = lv_obj_get_child(screen, 0);
    tabs = lv_obj_get_child(root, 1);
    lv_obj_send_event(lv_obj_get_child(tabs, 2), LV_EVENT_CLICKED, nullptr);
    footer = lv_obj_get_child(root, 4);
    lv_obj_send_event(lv_obj_get_child(footer, 0), LV_EVENT_CLICKED, nullptr);
    list = lv_obj_get_child(root, 3);
    auto* name_field = lv_obj_get_child(list, 1);
    lv_textarea_set_text(name_field, "林间宝藏");
    lv_textarea_set_text(lv_obj_get_child(list, 3), "30.5");
    lv_textarea_set_text(lv_obj_get_child(list, 5), "120.5");
    const std::string description = std::string(63, 'a') + "宝藏";
    lv_textarea_set_text(lv_obj_get_child(list, 7), description.c_str());
    if (!save(std::string(argv[3]) + "-editor.ppm", screen)) return 27;
    source.fail_save = true;
    lv_obj_send_event(lv_obj_get_child(footer, 3), LV_EVENT_CLICKED, nullptr);
    lv_tick_inc(600);
    lv_timer_handler();
    if (lv_obj_has_state(name_field, LV_STATE_DISABLED) || std::strcmp(lv_textarea_get_text(name_field), "林间宝藏")) return 22;
    source.fail_save = false;
    lv_obj_send_event(lv_obj_get_child(footer, 3), LV_EVENT_CLICKED, nullptr);
    lv_tick_inc(600);
    lv_timer_handler();
    if (!source.has_draft || source.draft.latitude_e7 != 305000000 || source.draft.longitude_e7 != 1205000000) return 23;
    source.pending_reads = 1;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    if (!lv_obj_has_state(lv_obj_get_child(list, 1), LV_STATE_DISABLED) ||
        !lv_obj_has_state(lv_obj_get_child(footer, 3), LV_STATE_DISABLED)) return 33;
    lv_tick_inc(600);
    lv_timer_handler();
    if (lv_obj_has_state(lv_obj_get_child(list, 1), LV_STATE_DISABLED)) return 34;
    if (std::strcmp(lv_textarea_get_text(lv_obj_get_child(list, 1)), "林间宝藏") ||
        description != lv_textarea_get_text(lv_obj_get_child(list, 7))) return 24;
    lv_textarea_set_text(lv_obj_get_child(list, 1), "Discard this");
    lv_obj_send_event(lv_obj_get_child(footer, 0), LV_EVENT_CLICKED, nullptr);
    if (lv_obj_has_flag(lv_obj_get_child(footer, 2), LV_OBJ_FLAG_HIDDEN)) return 25;
    lv_obj_send_event(lv_obj_get_child(footer, 2), LV_EVENT_CLICKED, nullptr);
    if (source.draft_name != "林间宝藏") return 26;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    lv_tick_inc(600);
    lv_timer_handler();
    lv_obj_send_event(lv_obj_get_child(footer, 2), LV_EVENT_CLICKED, nullptr);
    if (!std::strstr(lv_label_get_text(lv_obj_get_child(list, 0)), "publicly") || source.publications) return 28;
    if (!save(std::string(argv[3]) + "-publish.ppm", screen)) return 29;
    lv_obj_send_event(lv_obj_get_child(footer, 0), LV_EVENT_CLICKED, nullptr);
    if (source.publications || lv_obj_has_state(lv_obj_get_child(list, 1), LV_STATE_DISABLED)) return 30;
    lv_obj_send_event(lv_obj_get_child(footer, 2), LV_EVENT_CLICKED, nullptr);
    ++source.current_author;
    lv_obj_send_event(lv_obj_get_child(footer, 3), LV_EVENT_CLICKED, nullptr);
    if (source.publications || !std::strstr(lv_label_get_text(lv_obj_get_child(root, 2)), "not started")) return 31;
    lv_obj_send_event(lv_obj_get_child(footer, 0), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(footer, 2), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(footer, 3), LV_EVENT_CLICKED, nullptr);
    if (source.publications != 1 || lv_obj_get_child_count(list) != 1) return 32;
    source.pending_reads = 2;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(lv_obj_get_child(footer, 0), LV_EVENT_CLICKED, nullptr);
    lv_tick_inc(600);
    lv_timer_handler();
    if (lv_obj_get_child_count(list) != 1 || source.pending_reads != 1 || source.cancelled_reads != 1) return 35;
    source.pending_reads = 0;
    source.fail_read = true;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    if (lv_obj_get_child_count(list) != 1 || !std::strstr(lv_label_get_text(lv_obj_get_child(root, 2)), "Cannot read")) return 36;
    source.fail_read = false;
    source.pending_reads = 2;
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    geocaching::ui::shell::bind(nullptr);
    if (source.cancelled_reads != 2 || lv_obj_get_child_count(list) != 1) return 37;
    geocaching::ui::shell::bind(&source);
    source.pending_reads = 2;
    lv_tick_inc(600);
    lv_timer_handler();
    lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, nullptr);
    geocaching::ui::shell::exit(nullptr, screen);
    if (source.cancelled_reads != 3) return 38;
    // Warm LVGL style caches before checking repeated page lifetime cleanup.
    geocaching::ui::shell::enter(&host, screen);
    geocaching::ui::shell::exit(nullptr, screen);
    lv_mem_monitor_t baseline{}, after{};
    lv_mem_monitor(&baseline);
    for (unsigned i = 0; i < 20; ++i)
    {
        geocaching::ui::shell::enter(&host, screen);
        geocaching::ui::shell::exit(nullptr, screen);
    }
    lv_mem_monitor(&after);
    if (after.free_size != baseline.free_size || lv_obj_get_child_count(screen)) return 10;
    geocaching::ui::shell::bind(nullptr);
    lv_group_delete(old_group);
    std::printf("%dx%d: visible=%u, no LVGL growth after 20 enter/exit cycles\n", width, height, unsigned(visible));
    return 0;
}
