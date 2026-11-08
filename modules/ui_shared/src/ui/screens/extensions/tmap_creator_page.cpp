#include "ui/screens/extensions/tmap_creator_page.h"
#include "platform/ui/tmap_creation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/input/pager_navigation.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/gps/gps_page_runtime.h"
#include "ui/ui_theme.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include "ui_presentation/map/map_target_request.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace extensions::ui::tmap_creator
{
namespace
{
namespace tc = ::platform::ui::tmap_creation;
constexpr unsigned kCreation = 4;
struct Page
{
    lv_obj_t *root = nullptr, *body = nullptr, *error = nullptr, *status = nullptr, *progress = nullptr;
    lv_obj_t *cancel = nullptr, *new_map = nullptr, *forward = nullptr, *area_summary = nullptr, *view_map = nullptr, *map_surface = nullptr;
    std::array<lv_obj_t*, 8> fields{};
    std::array<lv_obj_t*, 3> layers{};
    std::vector<lv_obj_t*> focus;
    std::vector<tc::Source> sources;
    std::vector<tc::Area> areas;
    lv_timer_t* timer = nullptr;
    unsigned step = 0;
    bool coordinates_mode = false, advanced = false, awaiting_sources = false;
    bool map_open = false;
    ::ui::map::MapTargetRequest map_target{};
    ::gps::ui::shell::Host map_host{};
    uint32_t search_revision = 0, source_revision = 0;
    std::string last_status;
    int last_percent = -2;
    void (*closed)(void*) = nullptr;
    void* context = nullptr;
};
std::unique_ptr<Page> page;
tc::Plan draft;
tc::Area selected_area;
std::array<std::string, 4> coordinates;
std::string query_text, revision_text = "1";
unsigned quality = 0;
const char* titles[] = {"Area", "Map content", "Save map", "Review", "Creation"};
void render();
void search_area(lv_event_t*);
void update_area_summary();
void choose_quality();
void show_error(const std::string&);
void return_from_map(void*)
{
    if (!page || !page->map_open) return;
    ::gps::ui::runtime::exit(page->map_surface);
    if (page->map_surface) lv_obj_del(page->map_surface);
    page->map_surface = nullptr;
    page->map_open = false;
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_HIDDEN);
    set_default_group(app_g);
    if (page->timer) lv_timer_resume(page->timer);
    render();
    if (app_g && page->view_map) lv_group_focus_obj(page->view_map);
}
void view_map(lv_event_t*)
{
    if (!page || page->map_open || !tc::service() || tc::service()->snapshot().phase != tc::Phase::Succeeded) return;
    page->map_target = {};
    page->map_target.viewport = {(draft.north + draft.south) * .5, (draft.east + draft.west) * .5, draft.max_zoom};
    ::ui::copyText(page->map_target.label, draft.name.c_str());
    page->map_host.request_exit = [](void*)
    { lv_async_call_cancel(return_from_map, nullptr); lv_async_call(return_from_map, nullptr); };
    page->map_open = true;
    lv_obj_add_flag(page->root, LV_OBJ_FLAG_HIDDEN);
    if (page->timer) lv_timer_pause(page->timer);
    page->map_surface = lv_obj_create(lv_obj_get_parent(page->root));
    lv_obj_remove_style_all(page->map_surface);
    lv_obj_set_size(page->map_surface, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page->map_surface, 0, 0);
    lv_obj_add_flag(page->map_surface, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(page->map_surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(page->map_surface);
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) lv_indev_wait_release(input);
    ::gps::ui::runtime::enter(&page->map_host, page->map_surface, ::gps::ui::shell::Projection::Map, nullptr, &page->map_target);
    if (!page->map_target.entered)
    {
        return_from_map(nullptr);
        show_error("Map is unavailable; the completed files are retained.");
    }
}

std::string size(uint64_t bytes)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.1f MiB", double(bytes) / (1024 * 1024));
    return text;
}
void show_error(const std::string& message)
{
    if (!page || !page->error) return;
    lv_label_set_text(page->error, message.c_str());
    lv_obj_remove_flag(page->error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(page->root);
    lv_obj_scroll_to_view(page->error, LV_ANIM_OFF);
}
void suggest_zoom()
{
    unsigned layers = unsigned(draft.osm) + unsigned(draft.terrain) + unsigned(draft.satellite);
    layers = std::max(1U, layers);
    for (int z = 14; z >= 0; --z)
    {
        const auto tiles = [&](int level)
        {
            const double n = double(uint64_t(1) << level);
            const auto x = [&](double longitude)
            { return std::clamp<int64_t>(int64_t(std::floor((longitude + 180) / 360 * n)), 0, int64_t(n) - 1); };
            const auto y = [&](double latitude)
            {
                const double rad = latitude * 3.14159265358979323846 / 180;
                return std::clamp<int64_t>(int64_t(std::floor((1 - std::asinh(std::tan(rad)) / 3.14159265358979323846) * .5 * n)), 0, int64_t(n) - 1);
            };
            return uint64_t(x(draft.east) - x(draft.west) + 1) * uint64_t(y(draft.south) - y(draft.north) + 1);
        };
        if ((tiles(z) + (z ? tiles(z - 1) : 0)) * layers * 131072ULL > 96ULL * 1024 * 1024 && z) continue;
        draft.max_zoom = z;
        draft.min_zoom = std::max(0, z - 1);
        return;
    }
}
void remember_fields()
{
    if (!page) return;
    if (page->step == 1)
    {
        draft.osm = lv_obj_has_state(page->layers[0], LV_STATE_CHECKED);
        draft.terrain = lv_obj_has_state(page->layers[1], LV_STATE_CHECKED);
        draft.satellite = lv_obj_has_state(page->layers[2], LV_STATE_CHECKED);
        quality = lv_dropdown_get_selected(page->fields[0]);
        draft.min_zoom = lv_dropdown_get_selected(page->fields[1]);
        draft.max_zoom = lv_dropdown_get_selected(page->fields[2]);
        if (!quality) suggest_zoom();
    }
    if (page->step == 2 && page->advanced && page->fields[4])
    {
        constexpr uint64_t budgets[] = {128, 512, 1024, 2048};
        draft.maximum_bytes = budgets[lv_dropdown_get_selected(page->fields[4])] * 1024 * 1024;
        const auto source = lv_dropdown_get_selected(page->fields[5]);
        draft.download_source = source == 0;
        if (source && source <= page->sources.size()) draft.source = page->sources[source - 1].path;
        else if (!source) draft.source.clear();
    }
}
void finish()
{
    if (!page) return;
    auto callback = page->closed;
    auto context = page->context;
    shutdown();
    if (callback) callback(context);
}
void key(lv_event_t* event)
{
    if (lv_event_get_code(event) == LV_EVENT_FOCUSED)
    {
        lv_obj_scroll_to_view(lv_event_get_target_obj(event), LV_ANIM_OFF);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEY) return;
    const auto pressed = lv_event_get_key(event);
    if (page && lv_event_get_target_obj(event) == page->body && (pressed == LV_KEY_UP || pressed == LV_KEY_DOWN))
    {
        lv_obj_scroll_by(page->body, 0, pressed == LV_KEY_UP ? 24 : -24, LV_ANIM_OFF);
        lv_event_stop_processing(event);
        return;
    }
    if (pressed != LV_KEY_ESC) return;
    lv_event_stop_processing(event);
    if (auto* device = lv_indev_active()) lv_indev_reset(device, nullptr);
    if (page->step && page->step != kCreation)
    {
        remember_fields();
        --page->step;
        render();
    }
    else finish();
}
void focus(lv_obj_t* object)
{
    page->focus.push_back(object);
    lv_obj_add_event_cb(object, key, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(object, key, LV_EVENT_FOCUSED, nullptr);
    lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(object, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(object, ::ui::theme::accent(), LV_STATE_FOCUS_KEY);
    if (app_g) lv_group_add_obj(app_g, object);
}
lv_obj_t* label(lv_obj_t* parent, const std::string& text)
{
    auto* object = lv_label_create(parent);
    lv_obj_set_width(object, LV_PCT(100));
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    lv_label_set_text(object, text.c_str());
    lv_obj_set_style_text_color(object, ::ui::theme::text(), 0);
    return object;
}
lv_obj_t* row(lv_obj_t* parent)
{
    auto* object = lv_obj_create(parent);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_pad_column(object, 4, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback)
{
    auto* object = lv_btn_create(parent);
    const bool dense = ::ui::page_profile::current().dense;
    lv_obj_set_height(object, dense ? 20 : 36);
    lv_obj_set_style_pad_hor(object, dense ? 7 : 12, 0);
    lv_obj_set_style_pad_ver(object, 0, 0);
    lv_obj_set_style_radius(object, dense ? 4 : 6, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_text_color(object, ::ui::theme::text(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::accent(), LV_STATE_PRESSED);
    auto* caption = lv_label_create(object);
    lv_label_set_text(caption, ::ui::i18n::tr(text));
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, nullptr);
    focus(object);
    return object;
}
lv_obj_t* field(const char* title, const std::string& value, unsigned index, size_t maximum = 96)
{
    auto* line = row(page->body);
    auto* caption = label(line, ::ui::i18n::tr(title));
    lv_obj_set_width(caption, ::ui::page_profile::current().dense ? 62 : 160);
    auto* object = page->fields[index] = lv_textarea_create(line);
    lv_obj_set_flex_grow(object, 1);
    lv_obj_set_height(object, ::ui::page_profile::current().dense ? 22 : 40);
    lv_obj_set_style_pad_all(object, ::ui::page_profile::current().dense ? 2 : 6, 0);
    lv_obj_set_style_radius(object, 3, 0);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface(), 0);
    lv_obj_set_style_text_color(object, ::ui::theme::text(), 0);
    lv_textarea_set_one_line(object, true);
    lv_textarea_set_max_length(object, maximum);
    lv_textarea_set_text(object, value.c_str());
    ::ui::widgets::attach_touch_text_editor(object);
    focus(object);
    lv_obj_add_event_cb(
        object, [](lv_event_t* event)
        {
        if (!page) return;
        const auto index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
        if (page->error) lv_obj_add_flag(page->error, LV_OBJ_FLAG_HIDDEN);
        const std::string text = lv_textarea_get_text(lv_event_get_target_obj(event));
        if (page->step == 0)
        {
            if (page->coordinates_mode && index < 4) coordinates[index] = text;
            else if (!page->coordinates_mode && index == 0) query_text = text;
        }
        if (page->step == 2)
        {
            if (index == 0) draft.name = text;
            else if (index == 1) draft.filename = text;
            else if (index == 2) draft.package_key = text;
            else if (index == 3) revision_text = text;
        } },
        LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(uintptr_t(index)));
    return object;
}
lv_obj_t* dropdown(lv_obj_t* parent, const char* options, unsigned selected)
{
    auto* object = lv_dropdown_create(parent);
    lv_dropdown_set_options(object, options);
    lv_dropdown_set_selected(object, selected);
    lv_obj_set_height(object, ::ui::page_profile::current().dense ? 22 : 40);
    lv_obj_set_style_pad_all(object, ::ui::page_profile::current().dense ? 2 : 6, 0);
    lv_obj_set_style_radius(object, 3, 0);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface(), 0);
    lv_obj_set_style_text_color(object, ::ui::theme::text(), 0);
    lv_obj_add_event_cb(
        object, [](lv_event_t* event)
        {
        auto* list = lv_dropdown_get_list(lv_event_get_target_obj(event));
        if (!list) return;
        const auto& profile = ::ui::page_profile::current();
        lv_obj_set_style_text_font(list, profile.dense ? profile.tiny_font : profile.body_font, 0);
        lv_obj_set_style_bg_color(list, ::ui::theme::surface(), 0);
        lv_obj_set_style_text_color(list, ::ui::theme::text(), 0);
        lv_obj_set_style_border_color(list, ::ui::theme::border(), 0);
        lv_obj_set_style_shadow_width(list, 0, 0);
        lv_obj_set_style_bg_color(list, ::ui::theme::accent(), LV_PART_SELECTED);
        lv_obj_set_style_bg_color(list, ::ui::theme::accent(), lv_style_selector_t(LV_PART_SELECTED) | lv_style_selector_t(LV_STATE_CHECKED));
        lv_obj_set_style_text_color(list, ::ui::theme::text(), LV_PART_SELECTED); },
        LV_EVENT_READY, nullptr);
    focus(object);
    return object;
}
void update_area_summary()
{
    if (!page || !page->area_summary || page->areas.empty()) return;
    const auto index = lv_dropdown_get_selected(page->fields[1]);
    if (index >= page->areas.size()) return;
    const auto& area = page->areas[index];
    char dimensions[96];
    std::snprintf(dimensions, sizeof(dimensions), "\nEnclosing area %.1f x %.1f km",
                  (area.east - area.west) * 111.32 * std::cos((area.north + area.south) * .5 * 3.14159265358979323846 / 180),
                  (area.north - area.south) * 111.32);
    lv_label_set_text(page->area_summary, (area.description + dimensions).c_str());
}
void search_area(lv_event_t*)
{
    if (!page || !tc::service() || tc::service()->areaSearch().busy) return;
    if (!tc::service()->searchAreas(query_text))
    {
        show_error("Enter a city, district, state or country name.");
        return;
    }
    page->search_revision = tc::service()->areaSearch().revision;
    render();
}
bool collect()
{
    remember_fields();
    if (page->step == 0)
    {
        if (page->coordinates_mode)
        {
            double* values[] = {&draft.west, &draft.south, &draft.east, &draft.north};
            bool changed = false;
            for (unsigned i = 0; i < 4; ++i)
            {
                char* end = nullptr;
                const double value = std::strtod(coordinates[i].c_str(), &end);
                if (coordinates[i].empty() || !end || *end || !std::isfinite(value))
                {
                    show_error("Enter all four coordinates as decimal degrees.");
                    return false;
                }
                changed |= *values[i] != value;
                *values[i] = value;
            }
            if (draft.west < -180 || draft.east > 180 || draft.south < -85.051128 || draft.north > 85.051128 ||
                draft.west >= draft.east || draft.south >= draft.north)
            {
                show_error("West must be below East; South below North. Split date-line areas.");
                return false;
            }
            if (changed)
            {
                quality = 0;
                suggest_zoom();
            }
        }
        else
        {
            if (page->areas.empty())
            {
                show_error("Search for an area and select a result.");
                return false;
            }
            const auto index = lv_dropdown_get_selected(page->fields[1]);
            if (index >= page->areas.size()) return false;
            const auto& area = page->areas[index];
            draft.west = area.west;
            draft.south = area.south;
            draft.east = area.east;
            draft.north = area.north;
            if (selected_area.id != area.id)
            {
                draft.name = area.name;
                draft.country = area.country;
                draft.admin = area.id;
                draft.package_key = "area-" + area.id;
                draft.filename = draft.package_key + ".tmap";
                quality = 0;
                suggest_zoom();
            }
            selected_area = area;
            const double values[]{draft.west, draft.south, draft.east, draft.north};
            for (unsigned i = 0; i < 4; ++i)
            {
                char text[32];
                std::snprintf(text, sizeof(text), "%.6f", values[i]);
                coordinates[i] = text;
            }
        }
    }
    if (page->step == 1 && ((!draft.osm && !draft.terrain && !draft.satellite) || draft.min_zoom > draft.max_zoom))
    {
        show_error("Choose a map layer and an increasing zoom range.");
        return false;
    }
    if (page->step == 2)
    {
        if (draft.name.empty() || draft.package_key.empty())
        {
            show_error("A map name and package key are required.");
            return false;
        }
        char* end = nullptr;
        const auto revision = std::strtoull(revision_text.c_str(), &end, 10);
        if (!end || *end || !revision || revision_text.front() == '-')
        {
            show_error("Enter a positive package revision.");
            return false;
        }
        draft.revision = revision;
    }
    return true;
}
void next(lv_event_t*)
{
    if (!page || !tc::service()) return;
    if (page->step == kCreation)
    {
        if (!tc::service()->snapshot().busy)
        {
            draft.filename = draft.package_key + "-r" + std::to_string(++draft.revision) + ".tmap";
            revision_text = std::to_string(draft.revision);
            page->step = 0;
            render();
        }
        return;
    }
    if (!collect()) return;
    if (page->step == kCreation - 1)
    {
        std::string error;
        if (!tc::service()->start(draft, error))
        {
            show_error(error);
            return;
        }
    }
    ++page->step;
    render();
}
void previous(lv_event_t*)
{
    if (!page) return;
    if (page->step && page->step != kCreation)
    {
        remember_fields();
        --page->step;
        render();
    }
    else finish();
}
void progress()
{
    if (!page || page->step != kCreation || !tc::service()) return;
    const auto state = tc::service()->snapshot();
    std::string text = state.stage;
    if (state.total) text += "\n" + std::to_string(state.completed) + " / " + std::to_string(state.total);
    if (!state.error.empty()) text += "\n" + state.error;
    if (state.phase == tc::Phase::Succeeded) text += "\n" + draft.name + "\nView map opens the completed area.";
    if (text != page->last_status)
    {
        lv_label_set_text(page->status, text.c_str());
        page->last_status = text;
    }
    const int percent = state.phase == tc::Phase::Succeeded ? 100 : std::clamp(state.percent, 0, 100);
    if (percent != page->last_percent)
    {
        lv_bar_set_value(page->progress, percent, LV_ANIM_OFF);
        page->last_percent = percent;
    }
    if (page->new_map)
    {
        if (state.busy) lv_obj_add_state(page->new_map, LV_STATE_DISABLED);
        else lv_obj_remove_state(page->new_map, LV_STATE_DISABLED);
        auto* caption = lv_obj_get_child(page->new_map, 0);
        const char* text = state.phase == tc::Phase::Failed ? "Retry / edit" : "New map";
        if (std::strcmp(lv_label_get_text(caption), text)) lv_label_set_text(caption, text);
    }
    if (page->cancel)
    {
        if (state.busy) lv_obj_remove_state(page->cancel, LV_STATE_DISABLED);
        else lv_obj_add_state(page->cancel, LV_STATE_DISABLED);
    }
    if (page->view_map)
    {
        if (state.phase == tc::Phase::Succeeded && ::gps::ui::runtime::is_available()) lv_obj_remove_flag(page->view_map, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page->view_map, LV_OBJ_FLAG_HIDDEN);
    }
}
void choose_quality()
{
    if (!page || page->step != 1) return;
    quality = lv_dropdown_get_selected(page->fields[0]);
    draft.osm = lv_obj_has_state(page->layers[0], LV_STATE_CHECKED);
    draft.terrain = lv_obj_has_state(page->layers[1], LV_STATE_CHECKED);
    draft.satellite = lv_obj_has_state(page->layers[2], LV_STATE_CHECKED);
    if (!quality) suggest_zoom();
    else if (quality < 4)
    {
        constexpr uint8_t minimum[]{0, 8, 12, 14}, maximum[]{0, 11, 14, 16};
        draft.min_zoom = minimum[quality];
        draft.max_zoom = maximum[quality];
    }
    else return;
    if (page->fields[1]) lv_dropdown_set_selected(page->fields[1], draft.min_zoom);
    if (page->fields[2]) lv_dropdown_set_selected(page->fields[2], draft.max_zoom);
}
void render()
{
    if (!page || !tc::service()) return;
    if (app_g) lv_group_remove_all_objs(app_g);
    page->focus.clear();
    page->fields.fill(nullptr);
    page->layers.fill(nullptr);
    page->cancel = page->new_map = page->forward = page->area_summary = page->error = page->view_map = nullptr;
    page->last_status.clear();
    page->last_percent = -2;
    lv_obj_clean(page->root);
    const auto& profile = ::ui::page_profile::current();
    const bool dense = profile.dense;
    lv_obj_set_style_pad_all(page->root, dense ? 3 : 12, 0);
    lv_obj_set_style_pad_row(page->root, dense ? 3 : 8, 0);
    lv_obj_set_style_text_font(page->root, dense ? profile.tiny_font : profile.body_font, 0);
    lv_obj_set_style_text_color(page->root, ::ui::theme::text(), 0);
    lv_obj_set_flex_flow(page->root, LV_FLEX_FLOW_COLUMN);
    char heading[100];
    std::snprintf(heading, sizeof(heading), "%s %u/5 - %s", ::ui::i18n::tr("Make TMAP"), page->step + 1, ::ui::i18n::tr(titles[page->step]));
    auto* header = row(page->root);
    lv_obj_set_style_text_font(header, profile.title_font, 0);
    auto* title = label(header, heading);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    auto* help = label(header, "H Help");
    lv_obj_set_width(help, LV_SIZE_CONTENT);
    ::ui::input::pager::install_help(page->root, title);
    auto* body = page->body = lv_obj_create(page->root);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, dense ? 2 : 8, 0);
    lv_obj_set_style_pad_row(body, dense ? 3 : 10, 0);
    lv_obj_set_style_radius(body, dense ? 4 : 6, 0);
    lv_obj_set_style_border_color(body, ::ui::theme::separator(), 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(body, ::ui::theme::surface(), 0);
    lv_obj_set_style_text_font(body, profile.body_font, 0);
    if (page->step == 0)
    {
        if (page->coordinates_mode)
        {
            const char* names[]{"West", "South", "East", "North"};
            for (unsigned i = 0; i < 4; ++i) field(names[i], coordinates[i], i, 24);
        }
        else
        {
            const auto state = tc::service()->areaSearch();
            page->areas = state.results;
            auto* editor = field("Place", query_text, 0, 192);
            lv_textarea_set_placeholder_text(editor, "City / district / country");
            lv_obj_add_event_cb(editor, search_area, LV_EVENT_READY, nullptr);
            auto* line = row(body);
            auto* search = button(line, state.busy ? "Searching" : "Search", search_area);
            if (state.busy) lv_obj_add_state(search, LV_STATE_DISABLED);
            auto* hint = label(line, "Global search / Photon");
            lv_obj_set_width(hint, 0);
            lv_obj_set_flex_grow(hint, 1);
            lv_obj_set_style_text_font(hint, profile.tiny_font, 0);
            std::string options;
            unsigned selected = 0;
            for (size_t i = 0; i < page->areas.size(); ++i)
            {
                if (i) options += '\n';
                options += page->areas[i].name + " / " + page->areas[i].country;
                if (page->areas[i].id == selected_area.id) selected = i;
            }
            page->fields[1] = dropdown(body, options.empty() ? "Search, then select an area" : options.c_str(), selected);
            lv_obj_set_width(page->fields[1], LV_PCT(100));
            if (page->areas.empty() || state.busy) lv_obj_add_state(page->fields[1], LV_STATE_DISABLED);
            lv_obj_add_event_cb(
                page->fields[1], [](lv_event_t*)
                { update_area_summary(); },
                LV_EVENT_VALUE_CHANGED, nullptr);
            page->area_summary = label(body, state.error.empty() ? "Enter a place name, then press Enter or choose Search." : state.error);
            lv_obj_set_style_text_font(page->area_summary, profile.tiny_font, 0);
            if (!state.error.empty()) lv_obj_set_style_text_color(page->area_summary, ::ui::theme::error(), 0);
            if (!page->areas.empty()) update_area_summary();
        }
    }
    else if (page->step == 1)
    {
        auto* line = row(body);
        const char* names[]{"OSM", "Terrain", "Satellite"};
        const bool values[]{draft.osm, draft.terrain, draft.satellite};
        for (unsigned i = 0; i < 3; ++i)
        {
            auto* box = page->layers[i] = lv_checkbox_create(line);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_pad_column(box, 3, 0);
            lv_obj_set_style_pad_all(box, dense ? 1 : 3, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(box, ::ui::theme::surface(), LV_PART_INDICATOR);
            lv_obj_set_style_border_color(box, ::ui::theme::border(), LV_PART_INDICATOR);
            lv_obj_set_style_border_color(box, ::ui::theme::accent(), lv_style_selector_t(LV_PART_INDICATOR) | lv_style_selector_t(LV_STATE_CHECKED));
            lv_obj_set_style_border_color(box, ::ui::theme::accent(), lv_style_selector_t(LV_PART_INDICATOR) | lv_style_selector_t(LV_STATE_FOCUS_KEY));
            lv_obj_set_style_text_color(box, ::ui::theme::text(), lv_style_selector_t(LV_PART_INDICATOR) | lv_style_selector_t(LV_STATE_CHECKED));
            lv_obj_set_style_bg_color(box, ::ui::theme::accent(), lv_style_selector_t(LV_PART_INDICATOR) | lv_style_selector_t(LV_STATE_CHECKED));
            lv_checkbox_set_text(box, names[i]);
            if (values[i]) lv_obj_add_state(box, LV_STATE_CHECKED);
            focus(box);
            lv_obj_add_event_cb(
                box, [](lv_event_t*)
                { choose_quality(); },
                LV_EVENT_VALUE_CHANGED, nullptr);
        }
        page->fields[0] = dropdown(body, "Balanced (automatic)\nOverview (z8-11)\nWalking (z12-14)\nDetailed (z14-16)\nCustom zoom", quality);
        lv_obj_set_width(page->fields[0], LV_PCT(100));
        lv_obj_add_event_cb(
            page->fields[0], [](lv_event_t*)
            { choose_quality(); },
            LV_EVENT_VALUE_CHANGED, nullptr);
        std::string zooms;
        for (unsigned z = 0; z <= 18; ++z)
        {
            if (z) zooms += '\n';
            zooms += std::to_string(z);
        }
        auto* zoom = row(body);
        auto* caption = label(zoom, "Zoom");
        lv_obj_set_width(caption, dense ? 50 : 120);
        for (unsigned i = 1; i <= 2; ++i)
        {
            page->fields[i] = dropdown(zoom, zooms.c_str(), i == 1 ? draft.min_zoom : draft.max_zoom);
            lv_obj_set_width(page->fields[i], dense ? 72 : 120);
            lv_obj_add_event_cb(
                page->fields[i], [](lv_event_t*)
                { quality = 4; lv_dropdown_set_selected(page->fields[0], quality); },
                LV_EVENT_VALUE_CHANGED, nullptr);
        }
        auto* hint = label(body, "Map data downloads automatically. POI, search and labels are included.");
        lv_obj_set_style_text_font(hint, profile.tiny_font, 0);
    }
    else if (page->step == 2)
    {
        field("Name", draft.name, 0);
        field("Filename", draft.filename, 1);
        button(body, page->advanced ? "Hide advanced" : "Advanced", [](lv_event_t*)
               {
            remember_fields(); page->advanced = !page->advanced;
            if (page->advanced) { page->awaiting_sources = true; page->source_revision = tc::service()->snapshot().source_revision; }
            render(); });
        if (page->advanced)
        {
            field("Key", draft.package_key, 2);
            field("Revision", revision_text, 3, 18);
            auto* line = row(body);
            auto* caption = label(line, "Limit");
            lv_obj_set_width(caption, dense ? 62 : 160);
            const unsigned selected = draft.maximum_bytes <= 128ULL * 1024 * 1024 ? 0 : draft.maximum_bytes <= 512ULL * 1024 * 1024 ? 1
                                                                                    : draft.maximum_bytes <= 1024ULL * 1024 * 1024  ? 2
                                                                                                                                    : 3;
            page->fields[4] = dropdown(line, "128 MiB\n512 MiB\n1 GiB\n2 GiB", selected);
            lv_obj_set_width(page->fields[4], dense ? 110 : 160);
            page->sources = tc::service()->sources();
            std::string sources = "Online download";
            unsigned source = 0;
            for (size_t i = 0; i < page->sources.size(); ++i)
            {
                sources += "\n" + page->sources[i].name;
                if (!draft.download_source && page->sources[i].path == draft.source) source = i + 1;
            }
            page->fields[5] = dropdown(body, sources.c_str(), source);
            lv_obj_set_width(page->fields[5], LV_PCT(100));
            auto* hint = label(body, "Online is the default. Local PBF is optional.");
            lv_obj_set_style_text_font(hint, profile.tiny_font, 0);
        }
    }
    else if (page->step == kCreation - 1)
    {
        const auto estimate = tc::service()->estimate(draft);
        label(body, draft.name + "\n" + (draft.download_source ? "Download selected area" : "Use local PBF") +
                        "; zoom " + std::to_string(draft.min_zoom) + "-" + std::to_string(draft.max_zoom) +
                        "\n" + std::to_string(estimate.tiles) + " tiles; raw pixels " + size(estimate.raster_bytes) +
                        "\nWorking space " + size(estimate.working_bytes) + " / free " + size(estimate.available_bytes));
        auto* attribution = label(body, "Map data: OpenStreetMap contributors. Photon area search. Estimates exclude indexes/fonts.");
        lv_obj_set_style_text_font(attribution, profile.tiny_font, 0);
        if (!estimate.valid)
        {
            auto* error = label(body, estimate.error);
            lv_obj_set_style_text_color(error, ::ui::theme::error(), 0);
        }
    }
    else
    {
        page->status = label(body, "Preparing map creation...");
        page->progress = lv_bar_create(body);
        lv_obj_set_style_bg_color(page->progress, ::ui::theme::separator(), 0);
        lv_obj_set_style_bg_color(page->progress, ::ui::theme::accent(), LV_PART_INDICATOR);
        lv_obj_set_width(page->progress, LV_PCT(100));
        lv_obj_set_height(page->progress, 10);
        progress();
    }
    page->error = label(body, "");
    lv_obj_set_style_text_color(page->error, ::ui::theme::error(), 0);
    lv_obj_add_flag(page->error, LV_OBJ_FLAG_HIDDEN);
    if (page->step >= kCreation - 1) focus(body);
    auto* navigation = row(page->root);
    button(navigation, page->step && page->step != kCreation ? "Back" : "Close", previous);
    if (page->step == kCreation)
    {
        page->cancel = button(navigation, "Cancel", [](lv_event_t*)
                              { if (tc::service()) tc::service()->cancel(); });
        page->view_map = button(navigation, "View map", view_map);
        page->new_map = button(navigation, "New map", next);
        progress();
    }
    else
    {
        page->forward = button(navigation, page->step == kCreation - 1 ? "Create" : "Next", next);
        if ((page->step == 0 && !page->coordinates_mode && (page->areas.empty() || tc::service()->areaSearch().busy)) ||
            (page->step == kCreation - 1 && !tc::service()->estimate(draft).valid))
            lv_obj_add_state(page->forward, LV_STATE_DISABLED);
        if (!page->step) button(navigation, page->coordinates_mode ? "Region search" : "Coordinates", [](lv_event_t*)
                                { page->coordinates_mode = !page->coordinates_mode; render(); });
    }
    auto* hint = label(navigation, "Tab / Enter");
    lv_obj_set_width(hint, 0);
    lv_obj_set_flex_grow(hint, 1);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(hint, ::ui::theme::text_muted(), 0);
    if (app_g && !page->focus.empty()) lv_group_focus_obj(page->focus.front());
}
} // namespace
void open(lv_obj_t* parent, void (*closed)(void*), void* context)
{
    shutdown();
    if (!tc::supported()) return;
    if (draft.name.empty())
    {
        draft.name = "Local map";
        draft.filename = "local-area.tmap";
        draft.package_key = "local-area";
        draft.download_source = true;
        draft.maximum_bytes = 128ULL * 1024 * 1024;
    }
    page = std::make_unique<Page>();
    page->closed = closed;
    page->context = context;
    page->search_revision = tc::service()->areaSearch().revision;
    page->root = lv_obj_create(parent);
    lv_obj_set_size(page->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_add_flag(page->root, LV_OBJ_FLAG_FLOATING);
    lv_obj_move_foreground(page->root);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(page->root, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_border_width(page->root, 0, 0);
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_SCROLLABLE);
    if (tc::service()->snapshot().phase != tc::Phase::Idle) page->step = kCreation;
    render();
    page->timer = lv_timer_create([](lv_timer_t*)
                                  {
        if (!page || !tc::service() || (app_g && lv_group_get_default() != app_g)) return;
        if (page->step == 0 && !page->coordinates_mode)
        {
            const auto state = tc::service()->areaSearch();
            if (state.revision != page->search_revision && !state.busy)
            {
                page->search_revision = state.revision; render();
                if (app_g && !page->areas.empty() && query_text == state.query) lv_group_focus_obj(page->fields[1]);
            }
        }
        if (page && page->step == 2 && page->advanced && page->awaiting_sources &&
            tc::service()->snapshot().source_revision != page->source_revision)
        {
            remember_fields(); page->awaiting_sources = false; render();
        }
        progress(); },
                                  250, nullptr);
}
void shutdown()
{
    if (!page) return;
    lv_async_call_cancel(return_from_map, nullptr);
    if (page->map_open) ::gps::ui::runtime::exit(page->map_surface);
    if (page->map_surface) lv_obj_del(page->map_surface);
    if (page->timer) lv_timer_del(page->timer);
    if (app_g) lv_group_remove_all_objs(app_g);
    if (page->root && lv_obj_is_valid(page->root)) lv_obj_del(page->root);
    page.reset();
}
} // namespace extensions::ui::tmap_creator
