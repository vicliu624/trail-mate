#include "ui/screens/extensions/tmap_creator_page.h"
#include "platform/ui/tmap_creation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_theme.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include "ui/input/pager_navigation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace extensions::ui::tmap_creator
{
namespace
{
namespace tc = ::platform::ui::tmap_creation;
struct Page
{
    lv_obj_t *root = nullptr, *body = nullptr, *error = nullptr, *status = nullptr, *progress = nullptr;
    lv_obj_t *cancel = nullptr, *new_map = nullptr;
    std::array<lv_obj_t*, 6> fields{};
    std::array<lv_obj_t*, 3> layers{};
    std::vector<lv_obj_t*> focus;
    std::vector<tc::Source> sources;
    lv_timer_t* timer = nullptr;
    unsigned step = 0;
    bool awaiting_sources = false;
    uint32_t source_revision = 0;
    void (*closed)(void*) = nullptr;
    void* context = nullptr;
};
std::unique_ptr<Page> page;
tc::Plan draft;
std::array<std::string, 4> coordinates;
const char* titles[] = {"Source", "Area", "Layers and zoom", "Map identity", "Output", "Review", "Creation"};
void render();
void remember_source()
{
    if (!page || !page->fields[0] || page->step != 0) return;
    const auto index = lv_dropdown_get_selected(page->fields[0]);
    if (index < page->sources.size()) draft.source = page->sources[index].path;
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
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED)
    {
        lv_obj_scroll_to_view(lv_event_get_target_obj(event), LV_ANIM_OFF);
        return;
    }
    if (code != LV_EVENT_KEY) return;
    const auto pressed = lv_event_get_key(event);
    if (page && lv_event_get_target_obj(event) == page->body && (pressed == LV_KEY_UP || pressed == LV_KEY_DOWN))
    {
        lv_obj_scroll_by(page->body, 0, pressed == LV_KEY_UP ? 24 : -24, LV_ANIM_OFF);
        lv_event_stop_processing(event);
        return;
    }
    if (pressed != LV_KEY_ESC) return;
    lv_event_stop_processing(event);
    // LVGL sends CANCEL after KEY/ESC. Reset before replacing its focused
    // object so that follow-up event cannot target the deleted step editor.
    if (auto* device = lv_indev_active()) lv_indev_reset(device, nullptr);
    if (page->step && page->step != 6)
    {
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
    lv_obj_set_height(object, ::ui::page_profile::current().dense ? 25 : 36);
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
    lv_obj_set_width(caption, ::ui::page_profile::current().dense ? 78 : 160);
    auto* object = page->fields[index] = lv_textarea_create(line);
    lv_obj_set_flex_grow(object, 1);
    lv_obj_set_height(object, ::ui::page_profile::current().dense ? 28 : 40);
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
        const std::string text = lv_textarea_get_text(lv_event_get_target_obj(event));
        if (page->step == 1 && index < 4) coordinates[index] = text;
        if (page->step == 3) { if (index == 0) draft.name = text; else draft.package_key = text; }
        if (page->step == 4 && index == 0) draft.filename = text; },
        LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(uintptr_t(index)));
    return object;
}
lv_obj_t* dropdown(lv_obj_t* parent, const char* options, unsigned selected)
{
    auto* object = lv_dropdown_create(parent);
    lv_dropdown_set_options(object, options);
    lv_dropdown_set_selected(object, selected);
    lv_obj_set_height(object, ::ui::page_profile::current().dense ? 28 : 40);
    focus(object);
    return object;
}
std::string size(uint64_t bytes)
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.1f MiB", double(bytes) / (1024 * 1024));
    return text;
}
bool collect()
{
    if (page->step == 0)
    {
        if (page->sources.empty())
        {
            lv_label_set_text(page->error, "Add a local .osm.pbf file to map-sources, then Refresh.");
            return false;
        }
        const auto index = lv_dropdown_get_selected(page->fields[0]);
        if (index >= page->sources.size()) return false;
        draft.source = page->sources[index].path;
    }
    if (page->step == 1)
    {
        double* values[] = {&draft.west, &draft.south, &draft.east, &draft.north};
        for (unsigned i = 0; i < 4; ++i)
        {
            char* end = nullptr;
            const double value = std::strtod(coordinates[i].c_str(), &end);
            if (coordinates[i].empty() || !end || *end || !std::isfinite(value))
            {
                lv_label_set_text(page->error, "Enter all four coordinates as decimal degrees.");
                return false;
            }
            *values[i] = value;
        }
        if (draft.west < -180 || draft.east > 180 || draft.south < -85.051128 || draft.north > 85.051128 || draft.west >= draft.east || draft.south >= draft.north)
        {
            lv_label_set_text(page->error, "West must be below East; South below North. Split date-line areas.");
            return false;
        }
    }
    if (page->step == 2)
    {
        draft.osm = lv_obj_has_state(page->layers[0], LV_STATE_CHECKED);
        draft.terrain = lv_obj_has_state(page->layers[1], LV_STATE_CHECKED);
        draft.satellite = lv_obj_has_state(page->layers[2], LV_STATE_CHECKED);
        draft.min_zoom = lv_dropdown_get_selected(page->fields[0]);
        draft.max_zoom = lv_dropdown_get_selected(page->fields[1]);
        if ((!draft.osm && !draft.terrain && !draft.satellite) || draft.min_zoom > draft.max_zoom)
        {
            lv_label_set_text(page->error, "Choose a base layer and an increasing zoom range.");
            return false;
        }
    }
    if (page->step == 3 && (draft.name.empty() || draft.package_key.empty()))
    {
        lv_label_set_text(page->error, "A name and stable package key are required.");
        return false;
    }
    if (page->step == 4)
    {
        char* end = nullptr;
        const auto revision = std::strtoull(lv_textarea_get_text(page->fields[1]), &end, 10);
        if (!end || *end || !revision || lv_textarea_get_text(page->fields[1])[0] == '-')
        {
            lv_label_set_text(page->error, "Enter a positive package revision.");
            return false;
        }
        draft.revision = revision;
        constexpr uint64_t budgets[] = {128, 512, 1024, 2048};
        draft.maximum_bytes = budgets[lv_dropdown_get_selected(page->fields[2])] * 1024 * 1024;
    }
    return true;
}
void next(lv_event_t*)
{
    if (!page || !tc::service()) return;
    if (page->step == 6)
    {
        if (!tc::service()->snapshot().busy)
        {
            page->step = 0;
            render();
        }
        return;
    }
    if (!collect()) return;
    if (page->step == 5)
    {
        std::string error;
        if (!tc::service()->start(draft, error))
        {
            lv_label_set_text(page->error, error.c_str());
            return;
        }
    }
    ++page->step;
    render();
}
void previous(lv_event_t*)
{
    if (!page) return;
    if (page->step && page->step != 6)
    {
        --page->step;
        render();
    }
    else finish();
}
void progress()
{
    if (!page || page->step != 6 || !tc::service()) return;
    const auto state = tc::service()->snapshot();
    std::string text = state.stage;
    if (state.total) text += "\n" + std::to_string(state.completed) + " / " + std::to_string(state.total);
    if (!state.error.empty()) text += "\n" + state.error;
    if (!state.output.empty()) text += "\n" + state.output;
    lv_label_set_text(page->status, text.c_str());
    lv_bar_set_value(page->progress, state.percent < 0 ? 0 : state.percent, LV_ANIM_OFF);
    if (page->new_map)
    {
        if (state.busy) lv_obj_add_state(page->new_map, LV_STATE_DISABLED);
        else lv_obj_remove_state(page->new_map, LV_STATE_DISABLED);
    }
    if (page->cancel)
    {
        if (state.busy) lv_obj_remove_state(page->cancel, LV_STATE_DISABLED);
        else lv_obj_add_state(page->cancel, LV_STATE_DISABLED);
    }
}
void render()
{
    if (!page || !tc::service()) return;
    if (app_g) lv_group_remove_all_objs(app_g);
    page->focus.clear();
    page->fields.fill(nullptr);
    page->layers.fill(nullptr);
    page->cancel = page->new_map = nullptr;
    lv_obj_clean(page->root);
    const bool dense = ::ui::page_profile::current().dense;
    lv_obj_set_style_pad_all(page->root, dense ? 3 : 12, 0);
    lv_obj_set_flex_flow(page->root, LV_FLEX_FLOW_COLUMN);
    char heading[100];
    std::snprintf(heading, sizeof(heading), "%s  %u/7 - %s", ::ui::i18n::tr("Make TMAP"), page->step + 1, ::ui::i18n::tr(titles[page->step]));
    auto* header = row(page->root);
    auto* title = label(header, heading);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    auto* help_hint = label(header, "H Help");
    lv_obj_set_width(help_hint, LV_SIZE_CONTENT);
    ::ui::input::pager::install_help(page->root, title);
    auto* body = page->body = lv_obj_create(page->root);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, dense ? 2 : 8, 0);
    lv_obj_set_style_pad_row(body, dense ? 3 : 10, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(body, ::ui::theme::surface(), 0);
    lv_obj_set_style_text_font(body, ::ui::page_profile::resolve_body_font(), 0);
    if (page->step == 0)
    {
        focus(body);
        page->sources = tc::service()->sources();
        const auto directory = tc::service()->snapshot().source_directory;
        label(body, directory.empty() ? "Select a local OSM PBF." : "Local .osm.pbf folder:\n" + directory);
        std::string options;
        unsigned selected = 0;
        for (size_t i = 0; i < page->sources.size(); ++i)
        {
            if (i) options += '\n';
            options += page->sources[i].name;
            if (page->sources[i].path == draft.source) selected = i;
        }
        page->fields[0] = dropdown(body, options.empty() ? "No local PBF files" : options.c_str(), selected);
        lv_obj_set_width(page->fields[0], LV_PCT(100));
        button(body, "Refresh", [](lv_event_t*)
               { remember_source(); page->awaiting_sources = true; page->source_revision = tc::service()->snapshot().source_revision; render(); });
        if (!tc::service()->snapshot().producer_ready) label(body, "TMAP producer is not installed or is still being checked.");
    }
    else if (page->step == 1)
    {
        const char* names[] = {"West", "South", "East", "North"};
        for (unsigned i = 0; i < 4; ++i) field(names[i], coordinates[i], i, 24);
    }
    else if (page->step == 2)
    {
        auto* line = row(body);
        const char* names[] = {"OSM", "Terrain", "Satellite"};
        const bool values[] = {draft.osm, draft.terrain, draft.satellite};
        for (unsigned i = 0; i < 3; ++i)
        {
            auto* box = page->layers[i] = lv_checkbox_create(line);
            lv_checkbox_set_text(box, names[i]);
            if (values[i]) lv_obj_add_state(box, LV_STATE_CHECKED);
            focus(box);
        }
        std::string zooms;
        for (unsigned z = 0; z <= 18; ++z)
        {
            if (z) zooms += '\n';
            zooms += std::to_string(z);
        }
        auto* zoom = row(body);
        label(zoom, "Zoom");
        lv_obj_set_width(lv_obj_get_child(zoom, 0), dense ? 50 : 120);
        page->fields[0] = dropdown(zoom, zooms.c_str(), draft.min_zoom);
        page->fields[1] = dropdown(zoom, zooms.c_str(), draft.max_zoom);
        lv_obj_set_width(page->fields[0], dense ? 72 : 120);
        lv_obj_set_width(page->fields[1], dense ? 72 : 120);
        label(body, "POI, search and labels are included. Terrain/imagery may download source data.");
    }
    else if (page->step == 3)
    {
        field("Name", draft.name, 0);
        field("Package key", draft.package_key, 1);
        label(body, "Keep the package key stable for the same area. Separate areas use separate keys.");
    }
    else if (page->step == 4)
    {
        field("Filename", draft.filename, 0);
        field("Revision", std::to_string(draft.revision), 1, 18);
        auto* line = row(body);
        auto* caption = label(line, "Output limit");
        lv_obj_set_width(caption, dense ? 78 : 160);
        const unsigned selected = draft.maximum_bytes <= 128ULL * 1024 * 1024 ? 0 : draft.maximum_bytes <= 512ULL * 1024 * 1024 ? 1
                                                                                : draft.maximum_bytes <= 1024ULL * 1024 * 1024  ? 2
                                                                                                                                : 3;
        page->fields[2] = dropdown(line, "128 MiB\n512 MiB\n1 GiB\n2 GiB", selected);
    }
    else if (page->step == 5)
    {
        const auto estimate = tc::service()->estimate(draft);
        label(body, draft.name + "\n" + draft.filename + "\n" + std::to_string(estimate.tiles) + " tiles; raw pixels " + size(estimate.raster_bytes) + "\nWorking space " + size(estimate.working_bytes) + " / free " + size(estimate.available_bytes));
        label(body, dense ? "Estimate excludes indexes/fonts." : "Search, indexes and fonts add space; deduplication may reduce pixels. Existing output files are preserved.");
        if (!estimate.valid) label(body, estimate.error);
    }
    else
    {
        page->status = label(body, "Preparing map creation...");
        page->progress = lv_bar_create(body);
        lv_obj_set_width(page->progress, LV_PCT(100));
        lv_obj_set_height(page->progress, 10);
        progress();
    }
    page->error = label(body, "");
    if (page->step >= 5) focus(body);
    auto* navigation = row(page->root);
    button(navigation, page->step && page->step != 6 ? "Back" : "Close", previous);
    if (page->step == 6)
    {
        page->cancel = button(navigation, "Cancel", [](lv_event_t*)
                              { if (tc::service()) tc::service()->cancel(); });
        page->new_map = button(navigation, "New map", next);
        progress();
    }
    else button(navigation, page->step == 5 ? "Create" : "Next", next);
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
    }
    page = std::make_unique<Page>();
    page->closed = closed;
    page->context = context;
    page->root = lv_obj_create(parent);
    lv_obj_set_size(page->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_add_flag(page->root, LV_OBJ_FLAG_FLOATING);
    lv_obj_move_foreground(page->root);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(page->root, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_border_width(page->root, 0, 0);
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_SCROLLABLE);
    if (tc::service()->snapshot().phase != tc::Phase::Idle) page->step = 6;
    page->awaiting_sources = true;
    page->source_revision = tc::service()->snapshot().source_revision;
    render();
    page->timer = lv_timer_create([](lv_timer_t*)
                                  {
        if (page && app_g && lv_group_get_default() != app_g) return;
        if (page && page->step == 0 && page->awaiting_sources && tc::service() && tc::service()->snapshot().source_revision != page->source_revision)
        {
            const auto selected = app_g ? lv_group_get_focused(app_g) : nullptr;
            const auto index = std::find(page->focus.begin(), page->focus.end(), selected) - page->focus.begin();
            remember_source(); page->awaiting_sources = false; render();
            if (app_g && size_t(index) < page->focus.size()) lv_group_focus_obj(page->focus[index]);
        }
        progress(); },
                                  250, nullptr);
}
void shutdown()
{
    if (!page) return;
    if (page->timer) lv_timer_del(page->timer);
    if (app_g) lv_group_remove_all_objs(app_g);
    if (page->root && lv_obj_is_valid(page->root)) lv_obj_del(page->root);
    page.reset();
}
} // namespace extensions::ui::tmap_creator
