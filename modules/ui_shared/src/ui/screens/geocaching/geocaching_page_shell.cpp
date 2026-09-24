#include "ui/screens/geocaching/geocaching_page_shell.h"
#include "ui/app_runtime.h"
#include "ui/components/two_pane_styles.h"
#include "ui/page/page_profile.h"
#include "ui/screens/gps/gps_page_runtime.h"
#include "ui/widgets/top_bar.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace geocaching::ui::shell
{
namespace
{
namespace styles = ::ui::components::two_pane_styles;
using Section = ::ui::geocaching::Section;
constexpr size_t kVisibleRows = 4;
struct Editor
{
    ::ui::geocaching::DraftInput input;
    std::array<lv_obj_t*, 7> fields{};
    lv_obj_t* container = nullptr;
    lv_obj_t* publication_notice = nullptr;
    std::array<uint8_t, 64> publication_author{};
    uint32_t previous_revision = 0, publication_revision = 0;
    bool public_confirmation = false;
    bool dirty = false, saving = false, confirming = false, loading = false;
};
struct PageState
{
    Editor* editor = nullptr;
    ::ui::geocaching::Snapshot snapshot;
    ::ui::widgets::TopBar topbar;
    const ::ui::page::Host* host = nullptr;
    lv_obj_t *root = nullptr, *list = nullptr, *status = nullptr;
    lv_obj_t *refresh = nullptr, *previous = nullptr, *next = nullptr, *range = nullptr;
    std::array<lv_obj_t*, 3> tabs{};
    std::array<lv_obj_t*, kVisibleRows> rows{};
    std::array<std::array<uint8_t, 32>, kVisibleRows> row_ids{};
    lv_group_t *group = nullptr, *previous_group = nullptr;
    lv_timer_t* timer = nullptr;
    ::ui::geocaching::Source* rendered_source = nullptr;
    Section section = Section::Discover, rendered_section = Section::Discover;
    size_t offset = 0, rendered_offset = 0, row_count = 0, window = kVisibleRows;
    size_t detail_index = 0;
    uint64_t detail_generation = 0;
    std::array<uint8_t, 32> detail_id{}, detail_hash{};
    bool valid = false, details = false;
};
static_assert(sizeof(PageState) <= 640, "Page state must not contain an entire result list");
::ui::geocaching::Source* source = nullptr;
PageState* page = nullptr;
struct MapVisit
{
    ::gps::ui::runtime::MapTarget target;
    std::array<uint8_t, 32> selected_id{};
    uint64_t overlay_generation = 0;
    ::ui::page::Host host;
    lv_obj_t* parent = nullptr;
};
MapVisit* map_visit = nullptr;

void refreshView();
void closeDetails();
void showDetails(const ::ui::geocaching::Item& item, size_t index);
void openEditor(const ::ui::geocaching::Item* item);
void editorBack();
void saveEditor();
void closeEditor();
void previewPublication();

void returnFromMap(void*)
{
    if (!map_visit) return;
    ::gps::ui::runtime::exit(map_visit->parent);
    delete map_visit;
    map_visit = nullptr;
    if (!page) return;
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_HIDDEN);
    set_default_group(page->group);
    if (page->timer) lv_timer_resume(page->timer);
    refreshView();
    if (page->group) lv_group_focus_obj(page->topbar.back_btn);
}

bool openMap(const ::ui::geocaching::Item& item)
{
    if (!page || map_visit || !item.downloaded) return false;
    map_visit = new (std::nothrow) MapVisit;
    if (!map_visit) return false;
    map_visit->target.latitude_e7 = item.latitude_e7;
    map_visit->target.longitude_e7 = item.longitude_e7;
    map_visit->selected_id = item.id;
    map_visit->target.overlay_context = map_visit;
    map_visit->target.append_overlays = [](void* context, ::ui::map::MapOverlaySnapshot& overlays)
    {
        if (!source) return;
        auto& visit = *static_cast<MapVisit*>(context);
        ::ui::geocaching::Snapshot saved;
        source->snapshot(Section::Downloaded, saved);
        visit.overlay_generation = saved.generation;
        ::ui::geocaching::Item cached;
        for (size_t index = 0; index < saved.count; ++index)
        {
            if (overlays.item_count == ::ui::map::MapOverlaySnapshot::kMaxItems)
            {
                overlays.truncated = true;
                break;
            }
            if (!source->item(Section::Downloaded, index, saved.generation, cached)) break;
            if (!cached.downloaded || cached.id == visit.selected_id) continue;
            auto& marker = overlays.items[overlays.item_count++];
            marker = ::ui::map::MapOverlayItem{};
            marker.kind = ::ui::map::MapOverlayKind::Geocache;
            marker.stable_id = static_cast<uint32_t>(index + 1);
            marker.point.valid = true;
            marker.point.lat = cached.latitude_e7 / 10000000.0;
            marker.point.lon = cached.longitude_e7 / 10000000.0;
            ::ui::copyText(marker.label, cached.name.data());
        }
    };
    map_visit->target.select_overlay = [](void* context, uint32_t id)
    {
        if (!page || !source) return;
        const auto& visit = *static_cast<MapVisit*>(context);
        if (id)
        {
            ::ui::geocaching::Item selected;
            if (!source->item(Section::Downloaded, id - 1, visit.overlay_generation, selected) || !selected.downloaded) return;
            page->section = Section::Downloaded;
            page->snapshot.generation = visit.overlay_generation;
            showDetails(selected, id - 1);
        }
        lv_async_call_cancel(returnFromMap, nullptr);
        lv_async_call(returnFromMap, nullptr);
    };
    std::snprintf(map_visit->target.name, sizeof(map_visit->target.name), "%s", item.name.data());
    map_visit->parent = lv_obj_get_parent(page->root);
    map_visit->host.request_exit = [](void*)
    { lv_async_call_cancel(returnFromMap, nullptr); lv_async_call(returnFromMap, nullptr); };
    lv_obj_add_flag(page->root, LV_OBJ_FLAG_HIDDEN);
    if (page->timer) lv_timer_pause(page->timer);
    if (::gps::ui::runtime::enter_target(&map_visit->host, map_visit->parent, map_visit->target)) return true;
    delete map_visit;
    map_visit = nullptr;
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_HIDDEN);
    if (page->timer) lv_timer_resume(page->timer);
    set_default_group(page->group);
    return false;
}

void keyEvent(lv_event_t* event)
{
    if (!page) return;
    const auto key = lv_event_get_key(event);
    if (key == LV_KEY_ESC)
    {
        lv_event_stop_processing(event);
        if (page->editor) editorBack();
        else if (page->details) closeDetails();
        else if (page->host) ::ui::page::request_exit(page->host);
    }
    else if (page->editor && page->group && lv_group_get_editing(page->group)) return;
    else if (page->details && (key == LV_KEY_DOWN || key == LV_KEY_UP))
        lv_obj_scroll_by(page->list, 0, key == LV_KEY_DOWN ? -28 : 28, LV_ANIM_OFF);
    else if (page->group && (key == LV_KEY_DOWN || key == LV_KEY_RIGHT)) lv_group_focus_next(page->group);
    else if (page->group && (key == LV_KEY_UP || key == LV_KEY_LEFT)) lv_group_focus_prev(page->group);
}
void addFocusable(lv_obj_t* object)
{
    if (page->group) lv_group_add_obj(page->group, object);
    lv_obj_add_event_cb(object, keyEvent, LV_EVENT_KEY, nullptr);
}
void setEnabled(lv_obj_t* object, bool enabled)
{
    if (page->group) lv_group_remove_obj(object);
    if (enabled)
    {
        lv_obj_remove_state(object, LV_STATE_DISABLED);
        if (page->group) lv_group_add_obj(page->group, object);
    }
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_coord_t height)
{
    auto* object = lv_button_create(parent);
    lv_obj_remove_style_all(object);
    styles::apply_btn_filter(object);
    lv_obj_set_style_pad_hor(object, 6, 0);
    lv_obj_set_style_pad_ver(object, 2, 0);
    lv_obj_set_style_opa(object, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_height(object, height);
    auto* label = lv_label_create(object);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    addFocusable(object);
    return object;
}
void refreshView()
{
    if (!page) return;
    if (page->editor)
    {
        auto& editor = *page->editor;
        if (editor.loading)
        {
            ::ui::geocaching::Item item;
            std::copy(editor.input.id.begin(), editor.input.id.end(), item.id.begin());
            openEditor(&item);
            return;
        }
        if (!editor.confirming && !editor.public_confirmation && !editor.saving && editor.input.generation)
        {
            lv_obj_remove_flag(page->previous, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lv_obj_get_child(page->previous, 0), "Publish");
            if (lv_obj_has_state(page->previous, LV_STATE_DISABLED) != editor.dirty) setEnabled(page->previous, !editor.dirty);
        }
        if (editor.saving && source)
        {
            const auto status = source->draftSaveStatus(editor.input.id, editor.input.generation);
            if (status == ::ui::geocaching::DraftSaveStatus::Saved)
            {
                closeEditor();
                return;
            }
            if (status == ::ui::geocaching::DraftSaveStatus::Failed)
            {
                editor.saving = false;
                for (auto* field : editor.fields) lv_obj_remove_state(field, LV_STATE_DISABLED);
                lv_obj_remove_state(editor.container, LV_STATE_DISABLED);
                lv_label_set_text(page->status, "Save failed; edits retained");
                setEnabled(page->next, true);
            }
        }
        return;
    }
    if (page->details)
    {
        if (!source) return;
        ::ui::geocaching::Snapshot current;
        source->snapshot(page->section, current);
        if (current.busy) return;
        if (current.generation == page->detail_generation) return;
        ::ui::geocaching::Item item;
        const bool same = source->item(page->section, page->detail_index, current.generation, item) &&
                          item.id == page->detail_id && item.revision_hash == page->detail_hash;
        lv_label_set_text(page->status, current.status.data());
        if (same) lv_label_set_text(lv_obj_get_child(page->list, 1), item.detail.data());
        lv_label_set_text(lv_obj_get_child(page->next, 0), same && item.downloaded ? "Map" : "Download");
        setEnabled(page->next, same && (item.can_download || item.downloaded));
        page->detail_generation = current.generation;
        return;
    }
    auto& p = *page;
    const auto old_generation = p.snapshot.generation;
    auto* focused = p.group ? lv_group_get_focused(p.group) : nullptr;
    std::array<uint8_t, 32> focused_id{};
    bool focused_row = false;
    for (size_t i = 0; i < p.row_count; ++i)
        if (focused == p.rows[i])
        {
            focused_id = p.row_ids[i];
            focused_row = true;
            break;
        }
    ::ui::geocaching::Snapshot current;
    if (source) source->snapshot(p.section, current);
    else std::snprintf(current.status.data(), current.status.size(), "Geocaching service unavailable");
    if (current.busy && p.valid && p.rendered_source == source && p.rendered_section == p.section &&
        p.rendered_offset == p.offset) return;
    p.snapshot = current;
    if (p.offset >= p.snapshot.count) p.offset = p.snapshot.count ? ((p.snapshot.count - 1) / p.window) * p.window : 0;
    if (source) source->requestWindow(p.section, p.offset, p.window);
    if (p.valid && p.rendered_source == source && p.rendered_section == p.section &&
        p.rendered_offset == p.offset && old_generation == p.snapshot.generation) return;
    p.valid = !current.busy;
    p.rendered_source = source;
    p.rendered_section = p.section;
    p.rendered_offset = p.offset;
    for (size_t i = 0; i < p.tabs.size(); ++i)
    {
        if (i == static_cast<size_t>(p.section)) lv_obj_add_state(p.tabs[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(p.tabs[i], LV_STATE_CHECKED);
    }
    lv_label_set_text(p.status, p.snapshot.status.data());
    lv_obj_clean(p.list);
    p.rows.fill(nullptr);
    p.row_count = 0;
    ::ui::geocaching::Item item;
    while (p.row_count < p.window && p.offset + p.row_count < p.snapshot.count)
    {
        const size_t i = p.row_count;
        if (!source || !source->item(p.section, p.offset + i, p.snapshot.generation, item))
        {
            p.valid = false;
            break;
        }
        auto* row = button(p.list, item.name.data(), ::ui::page_profile::current().list_item_height);
        lv_obj_set_width(row, LV_PCT(100));
        auto* label = lv_obj_get_child(row, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        if (item.is_draft && item.publication_revision)
            lv_label_set_text_fmt(label, "v%lu %s | %s", static_cast<unsigned long>(item.publication_revision),
                                  item.publication_confirmed ? "accepted" : "unconfirmed", item.name.data());
        p.rows[i] = row;
        p.row_ids[i] = item.id;
        ++p.row_count;
        lv_obj_add_event_cb(
            row, [](lv_event_t* event)
            {
            if (!page || !source) return;
            const auto index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
            ::ui::geocaching::Item selected;
            if (index < page->row_count &&
                source->item(page->section, page->offset + index, page->snapshot.generation, selected) &&
                selected.id == page->row_ids[index])
            { const auto row = page->offset + index; source->open(selected, page->snapshot.generation); if (page) showDetails(selected, row); }
            else { page->valid = false; refreshView(); } },
            LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    }
    if (!p.row_count)
    {
        auto* empty = lv_label_create(p.list);
        lv_label_set_text(empty, p.snapshot.busy ? "Loading caches..." : source ? "No caches in this view"
                                                                                : "No cached items");
        styles::apply_label_muted(empty);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty, 12, 0);
    }
    lv_label_set_text_fmt(p.range, "%lu-%lu / %lu",
                          static_cast<unsigned long>(p.row_count ? p.offset + 1 : 0),
                          static_cast<unsigned long>(p.offset + p.row_count), static_cast<unsigned long>(p.snapshot.count));
    setEnabled(p.refresh, p.section == Section::Published ? p.snapshot.can_create : p.snapshot.can_refresh);
    lv_label_set_text(lv_obj_get_child(p.refresh, 0), p.section == Section::Published ? "New" : "Refresh");
    setEnabled(p.previous, p.offset != 0);
    setEnabled(p.next, (p.offset + p.row_count < p.snapshot.count || p.snapshot.has_more) && p.row_count != 0 && p.valid);
    if (p.group)
    {
        if (focused_row)
        {
            auto* restore = p.tabs[static_cast<size_t>(p.section)];
            for (size_t i = 0; i < p.row_count; ++i)
                if (p.row_ids[i] == focused_id) restore = p.rows[i];
            lv_group_focus_obj(restore);
        }
        else if (focused && !lv_obj_has_state(focused, LV_STATE_DISABLED)) lv_group_focus_obj(focused);
        else lv_group_focus_obj(p.tabs[static_cast<size_t>(p.section)]);
    }
}
void closeDetails()
{
    if (!page) return;
    page->details = false;
    page->valid = false;
    lv_obj_remove_flag(page->previous, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(page->next, ::ui::page_profile::current().control_button_height);
    lv_label_set_text(lv_obj_get_child(page->next, 0), LV_SYMBOL_RIGHT);
    lv_obj_remove_flag(page->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_to_y(page->list, 0, LV_ANIM_OFF);
    refreshView();
}
void showDetails(const ::ui::geocaching::Item& item, size_t index)
{
    if (item.is_draft)
    {
        openEditor(&item);
        return;
    }
    auto& p = *page;
    p.details = true;
    p.detail_index = index;
    p.detail_generation = p.snapshot.generation;
    p.detail_id = item.id;
    p.detail_hash = item.revision_hash;
    lv_obj_clean(p.list);
    p.rows.fill(nullptr);
    p.row_count = 0;
    lv_obj_add_flag(p.list, LV_OBJ_FLAG_SCROLLABLE);
    auto* title = lv_label_create(p.list);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_text(title, item.name.data());
    styles::apply_label_primary(title);
    auto* body = lv_label_create(p.list);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_text(body, item.detail.data());
    styles::apply_label_muted(body);
    lv_label_set_text(p.range, "Preview");
    lv_label_set_text(lv_obj_get_child(p.refresh, 0), "Back");
    setEnabled(p.refresh, true);
    setEnabled(p.previous, false);
    lv_obj_add_flag(p.previous, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(p.next, ::ui::page_profile::current().control_button_min_width);
    lv_label_set_text(lv_obj_get_child(p.next, 0), item.downloaded ? "Map" : "Download");
    setEnabled(p.next, item.can_download || item.downloaded);
    if (p.group) lv_group_focus_obj(p.topbar.back_btn);
}
bool parseDecimal(const char* text, int32_t& out)
{
    if (!text || !*text) return false;
    const bool negative = *text == '-';
    if (negative) ++text;
    if (*text < '0' || *text > '9') return false;
    int64_t whole = 0, fraction = 0, scale = 1000000;
    while (*text >= '0' && *text <= '9')
    {
        whole = whole * 10 + (*text++ - '0');
        if (whole > 180) return false;
    }
    if (*text == '.')
    {
        ++text;
        if (*text < '0' || *text > '9') return false;
        while (*text >= '0' && *text <= '9')
        {
            if (!scale) return false;
            fraction += (*text++ - '0') * scale;
            scale /= 10;
        }
    }
    if (*text) return false;
    const auto value = whole * 10000000 + fraction;
    if (value > 1800000000) return false;
    out = static_cast<int32_t>(negative ? -value : value);
    return true;
}

void closeEditor()
{
    if (!page || !page->editor) return;
    if (page->editor->loading && source) source->cancelDraftRead(page->editor->input.id);
    delete page->editor;
    page->editor = nullptr;
    for (auto* tab : page->tabs) setEnabled(tab, true);
    lv_obj_set_width(page->previous, ::ui::page_profile::current().control_button_height);
    lv_label_set_text(lv_obj_get_child(page->previous, 0), LV_SYMBOL_LEFT);
    closeDetails();
}

void editorBack()
{
    if (!page || !page->editor) return;
    auto& editor = *page->editor;
    if (editor.public_confirmation)
    {
        editor.public_confirmation = false;
        if (editor.publication_notice) lv_obj_delete(editor.publication_notice);
        editor.publication_notice = nullptr;
        for (auto* field : editor.fields) lv_obj_remove_state(field, LV_STATE_DISABLED);
        lv_obj_remove_state(editor.container, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(page->refresh, 0), "Back");
        lv_label_set_text(lv_obj_get_child(page->next, 0), "Save");
        lv_label_set_text(page->status, "Editing local draft");
        refreshView();
        return;
    }
    if (editor.saving)
    {
        lv_label_set_text(page->status, "Saving; please wait");
        return;
    }
    if (!editor.dirty)
    {
        closeEditor();
        return;
    }
    editor.confirming = !editor.confirming;
    lv_label_set_text(lv_obj_get_child(page->previous, 0), "Discard");
    lv_label_set_text(page->status, editor.confirming ? "Unsaved edits: save, discard or keep editing" : "Editing local draft");
    lv_label_set_text(lv_obj_get_child(page->refresh, 0), editor.confirming ? "Keep" : "Back");
    if (editor.confirming) lv_obj_remove_flag(page->previous, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(page->previous, LV_OBJ_FLAG_HIDDEN);
    setEnabled(page->previous, editor.confirming);
}

void previewPublication()
{
    if (!page || !page->editor || !source) return;
    auto& editor = *page->editor;
    if (editor.dirty || editor.saving || !editor.input.generation ||
        !source->publicationAuthor(editor.input.id, editor.input.generation, editor.publication_author, &editor.previous_revision, &editor.publication_revision))
    {
        lv_label_set_text(page->status, "Save location/name; check author, clock and directory");
        return;
    }
    editor.public_confirmation = true;
    char author[129];
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < editor.publication_author.size(); ++i)
    {
        author[i * 2] = hex[editor.publication_author[i] >> 4];
        author[i * 2 + 1] = hex[editor.publication_author[i] & 15];
    }
    author[128] = 0;
    editor.publication_notice = lv_label_create(page->list);
    lv_obj_set_width(editor.publication_notice, LV_PCT(100));
    lv_label_set_long_mode(editor.publication_notice, LV_LABEL_LONG_WRAP);
    if (editor.previous_revision)
        lv_label_set_text_fmt(editor.publication_notice, "Publish this cache publicly on Reticulum?\nv%lu -> v%lu\nAuthor public identity:\n%s",
                              static_cast<unsigned long>(editor.previous_revision), static_cast<unsigned long>(editor.publication_revision), author);
    else
        lv_label_set_text_fmt(editor.publication_notice, "Publish this cache publicly on Reticulum?\nFirst publication: v%lu\nAuthor public identity:\n%s",
                              static_cast<unsigned long>(editor.publication_revision), author);
    styles::apply_label_primary(editor.publication_notice);
    lv_obj_move_to_index(editor.publication_notice, 0);
    for (auto* field : editor.fields) lv_obj_add_state(field, LV_STATE_DISABLED);
    lv_obj_add_state(editor.container, LV_STATE_DISABLED);
    lv_obj_scroll_to_y(page->list, 0, LV_ANIM_OFF);
    lv_label_set_text(page->status, "Review author and saved fields before publishing");
    lv_label_set_text(lv_obj_get_child(page->refresh, 0), "Cancel");
    lv_label_set_text(lv_obj_get_child(page->next, 0), "Publish");
    setEnabled(page->previous, false);
    lv_obj_add_flag(page->previous, LV_OBJ_FLAG_HIDDEN);
    if (page->group) lv_group_focus_obj(page->refresh);
}

void saveEditor()
{
    if (!page || !page->editor || !source || page->editor->saving || page->editor->loading) return;
    auto& editor = *page->editor;
    if (editor.public_confirmation)
    {
        if (source->publishDraft(editor.input.id, editor.input.generation, editor.publication_author, editor.publication_revision)) closeEditor();
        else lv_label_set_text(page->status, "Publication not started; review identity and draft again");
        return;
    }
    auto& input = editor.input;
    const char* lat = lv_textarea_get_text(editor.fields[1]);
    const char* lon = lv_textarea_get_text(editor.fields[2]);
    input.has_coordinates = *lat || *lon;
    int32_t difficulty = 0, terrain = 0;
    if ((input.has_coordinates && (!parseDecimal(lat, input.latitude_e7) || !parseDecimal(lon, input.longitude_e7) ||
                                   input.latitude_e7 < -900000000 || input.latitude_e7 > 900000000 || input.longitude_e7 >= 1800000000)) ||
        !parseDecimal(lv_textarea_get_text(editor.fields[5]), difficulty) || difficulty < 10000000 || difficulty > 50000000 || difficulty % 5000000 ||
        !parseDecimal(lv_textarea_get_text(editor.fields[6]), terrain) || terrain < 10000000 || terrain > 50000000 || terrain % 5000000)
    {
        lv_label_set_text(page->status, "Check coordinates and 1-5 ratings (step 0.5)");
        return;
    }
    input.difficulty_x2 = static_cast<uint8_t>(difficulty / 5000000);
    input.terrain_x2 = static_cast<uint8_t>(terrain / 5000000);
    input.container_size = static_cast<uint8_t>(lv_dropdown_get_selected(editor.container));
    input.name = lv_textarea_get_text(editor.fields[0]);
    input.description = lv_textarea_get_text(editor.fields[3]);
    input.hint = lv_textarea_get_text(editor.fields[4]);
    if (!source->saveDraft(input))
    {
        lv_label_set_text(page->status, "Cannot save now; edits retained");
        return;
    }
    editor.saving = true;
    for (auto* field : editor.fields) lv_obj_add_state(field, LV_STATE_DISABLED);
    lv_obj_add_state(editor.container, LV_STATE_DISABLED);
    setEnabled(page->next, false);
    setEnabled(page->previous, false);
    lv_label_set_text(page->status, "Saving local draft...");
}

void openEditor(const ::ui::geocaching::Item* item)
{
    if (!page || !source || (page->editor && !page->editor->loading)) return;
    if (!page->editor) page->editor = new (std::nothrow) Editor;
    if (!page->editor) return;
    auto& editor = *page->editor;
    if (!editor.loading)
    {
        page->details = false;
        page->row_count = 0;
        page->rows.fill(nullptr);
        lv_obj_clean(page->list);
        lv_obj_add_flag(page->list, LV_OBJ_FLAG_SCROLLABLE);
        const char* names[] = {"Name", "Latitude (blank if unset)", "Longitude", "Description", "Hint", "Difficulty (1-5)", "Terrain (1-5)"};
        const uint32_t limits[] = {96, 12, 13, 2048, 512, 3, 3};
        for (size_t i = 0; i < editor.fields.size(); ++i)
        {
            auto* label = lv_label_create(page->list);
            lv_label_set_text(label, names[i]);
            styles::apply_label_primary(label);
            auto* field = editor.fields[i] = lv_textarea_create(page->list);
            lv_obj_set_width(field, LV_PCT(100));
            lv_textarea_set_one_line(field, i != 3 && i != 4);
            lv_obj_set_height(field, i == 3 || i == 4 ? 64 : 32);
            lv_textarea_set_max_length(field, limits[i]);
            lv_textarea_set_text(field, i == 5 || i == 6 ? "1.0" : "");
            lv_obj_set_style_bg_color(field, lv_color_hex(styles::kSidePanelBg), 0);
            lv_obj_set_style_text_color(field, lv_color_hex(styles::kTextPrimary), 0);
            lv_obj_set_style_border_color(field, lv_color_hex(styles::kBorder), 0);
            lv_obj_set_style_border_color(field, lv_color_hex(styles::kAccent), LV_STATE_FOCUSED);
            lv_obj_set_style_outline_color(field, lv_color_hex(styles::kAccent), LV_STATE_FOCUSED);
            addFocusable(field);
            lv_obj_add_event_cb(
                field, [](lv_event_t*)
                { if (page && page->editor) page->editor->dirty = true; },
                LV_EVENT_VALUE_CHANGED, nullptr);
        }
        auto* label = lv_label_create(page->list);
        lv_label_set_text(label, "Container");
        editor.container = lv_dropdown_create(page->list);
        lv_dropdown_set_options(editor.container, "Unspecified\nMicro\nSmall\nRegular\nLarge\nOther");
        lv_obj_set_style_bg_color(editor.container, lv_color_hex(styles::kMainPanelBg), 0);
        lv_obj_set_style_text_color(editor.container, lv_color_hex(styles::kTextPrimary), 0);
        lv_obj_set_style_border_color(editor.container, lv_color_hex(styles::kAccent), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_color(editor.container, lv_color_hex(styles::kAccent), LV_STATE_FOCUSED);
        lv_obj_add_event_cb(
            editor.container, [](lv_event_t* event)
            {
        auto* list = lv_dropdown_get_list(lv_event_get_target_obj(event));
        if (!list) return;
        lv_obj_set_style_bg_color(list, lv_color_hex(styles::kMainPanelBg), 0);
        lv_obj_set_style_text_color(list, lv_color_hex(styles::kTextPrimary), 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(styles::kAccent), LV_PART_SELECTED | LV_STATE_CHECKED); },
            LV_EVENT_CLICKED, nullptr);
        addFocusable(editor.container);
        lv_obj_add_event_cb(
            editor.container, [](lv_event_t*)
            { if (page && page->editor) page->editor->dirty = true; },
            LV_EVENT_VALUE_CHANGED, nullptr);
    }
    if (item)
    {
        std::array<uint8_t, 16> id;
        std::copy_n(item->id.data(), id.size(), id.data());
        const auto result = source->readDraft(
            id, [](const ::ui::geocaching::DraftInput& input, void* context)
            {
            auto& e = *static_cast<Editor*>(context);
            e.input = input;
            const std::string_view texts[] = {input.name, input.description, input.hint};
            const size_t slots[] = {0, 3, 4};
            for (size_t i = 0; i < 3; ++i)
            {
                auto remaining = texts[i];
                while (!remaining.empty())
                {
                    size_t count = std::min<size_t>(64, remaining.size());
                    if (count < remaining.size())
                        while (count && (static_cast<uint8_t>(remaining[count]) & 0xc0) == 0x80) --count;
                    char chunk[65];
                    std::memcpy(chunk, remaining.data(), count); chunk[count] = 0;
                    lv_textarea_add_text(e.fields[slots[i]], chunk);
                    remaining.remove_prefix(count);
                }
            }
            char text[24];
            if (input.has_coordinates)
            {
                std::snprintf(text, sizeof(text), "%.7f", input.latitude_e7 / 10000000.0); lv_textarea_set_text(e.fields[1], text);
                std::snprintf(text, sizeof(text), "%.7f", input.longitude_e7 / 10000000.0); lv_textarea_set_text(e.fields[2], text);
            }
            std::snprintf(text, sizeof(text), "%.1f", input.difficulty_x2 / 2.0); lv_textarea_set_text(e.fields[5], text);
            std::snprintf(text, sizeof(text), "%.1f", input.terrain_x2 / 2.0); lv_textarea_set_text(e.fields[6], text);
            lv_dropdown_set_selected(e.container, input.container_size);
            e.input.name = e.input.description = e.input.hint = {}; },
            &editor);
        if (result == ::ui::geocaching::DraftReadStatus::Pending)
        {
            editor.loading = true;
            editor.input.id = id;
            editor.dirty = false;
            for (auto* field : editor.fields) setEnabled(field, false);
            setEnabled(editor.container, false);
            for (auto* tab : page->tabs) setEnabled(tab, false);
            setEnabled(page->previous, false);
            setEnabled(page->next, false);
            setEnabled(page->refresh, true);
            lv_label_set_text(lv_obj_get_child(page->refresh, 0), "Back");
            lv_label_set_text(page->status, "Loading local draft...");
            if (page->group) lv_group_focus_obj(page->refresh);
            return;
        }
        if (result == ::ui::geocaching::DraftReadStatus::Failed)
        {
            closeEditor();
            lv_label_set_text(page->status, "Cannot read local draft");
            return;
        }
    }
    editor.loading = false;
    for (auto* field : editor.fields) setEnabled(field, true);
    setEnabled(editor.container, true);
    editor.dirty = !item;
    for (auto* tab : page->tabs) setEnabled(tab, false);
    lv_label_set_text(page->status, item && item->detail[0] ? item->detail.data() : "Editing local draft");
    lv_label_set_text(page->range, "Draft");
    lv_label_set_text(lv_obj_get_child(page->refresh, 0), "Back");
    setEnabled(page->refresh, true);
    lv_label_set_text(lv_obj_get_child(page->previous, 0), "Discard");
    lv_obj_set_width(page->previous, ::ui::page_profile::current().control_button_min_width);
    setEnabled(page->previous, false);
    lv_obj_add_flag(page->previous, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lv_obj_get_child(page->next, 0), "Save");
    lv_obj_set_width(page->next, ::ui::page_profile::current().control_button_min_width);
    setEnabled(page->next, true);
    if (page->group) lv_group_focus_obj(editor.fields[0]);
}
} // namespace

void bind(::ui::geocaching::Source* value)
{
    if (value != source && page && page->editor && page->editor->loading) closeEditor();
    source = value;
    if (page) page->valid = false;
}
void enter(void* user_data, lv_obj_t* parent)
{
    if (page || !parent) return;
    page = new (std::nothrow) PageState;
    if (!page)
    {
        if (user_data) ::ui::page::request_exit(static_cast<const ::ui::page::Host*>(user_data));
        return;
    }
    auto& p = *page;
    const auto& profile = ::ui::page_profile::current();
    if (source) source->activate(true);
    p.previous_group = lv_group_get_default();
    p.group = lv_group_create();
    set_default_group(nullptr);
    p.host = static_cast<const ::ui::page::Host*>(user_data);
    p.root = lv_obj_create(parent);
    lv_obj_remove_style_all(p.root);
    lv_obj_set_size(p.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(p.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p.root, lv_color_hex(styles::kSidePanelBg), 0);
    lv_obj_set_style_text_color(p.root, lv_color_hex(styles::kTextPrimary), 0);
    lv_obj_set_style_text_font(p.root, ::ui::page_profile::resolve_body_font(), 0);
    lv_obj_set_style_pad_row(p.root, profile.top_content_gap, 0);
    lv_obj_remove_flag(p.root, LV_OBJ_FLAG_SCROLLABLE);
    ::ui::widgets::top_bar_init(p.topbar, p.root);
    ::ui::widgets::top_bar_set_title(p.topbar, "Geocaching");
    addFocusable(p.topbar.back_btn);
    ::ui::widgets::top_bar_set_back_callback(
        p.topbar, [](void*)
        {
        if (page && page->editor) editorBack();
        else if (page && page->details) closeDetails();
        else if (page && page->host) ::ui::page::request_exit(page->host); },
        nullptr);
    auto* tabs = lv_obj_create(p.root);
    lv_obj_remove_style_all(tabs);
    lv_obj_set_size(tabs, LV_PCT(100), profile.filter_button_height);
    lv_obj_set_style_pad_hor(tabs, 4, 0);
    lv_obj_set_style_pad_column(tabs, 4, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
    const char* names[] = {"Discover", "Downloaded", "My caches"};
    for (uintptr_t i = 0; i < 3; ++i)
    {
        p.tabs[i] = button(tabs, names[i], profile.filter_button_height);
        lv_obj_set_width(p.tabs[i], 0);
        lv_obj_set_flex_grow(p.tabs[i], 1);
        lv_obj_add_event_cb(
            p.tabs[i], [](lv_event_t* event)
            {
            if (!page) return;
            page->section = static_cast<Section>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
            page->offset = 0; closeDetails(); },
            LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    }
    p.status = lv_label_create(p.root);
    lv_obj_set_width(p.status, LV_PCT(100));
    lv_label_set_long_mode(p.status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_hor(p.status, 6, 0);
    styles::apply_label_muted(p.status);
    p.list = lv_obj_create(p.root);
    lv_obj_remove_style_all(p.list);
    styles::apply_container_main(p.list);
    lv_obj_set_width(p.list, LV_PCT(100));
    lv_obj_set_height(p.list, 0);
    lv_obj_set_flex_grow(p.list, 1);
    lv_obj_set_flex_flow(p.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p.list, 3, 0);
    lv_obj_remove_flag(p.list, LV_OBJ_FLAG_SCROLLABLE);
    auto* footer = lv_obj_create(p.root);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, LV_PCT(100), profile.control_button_height);
    lv_obj_set_style_pad_hor(footer, 4, 0);
    lv_obj_set_style_pad_column(footer, 4, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    p.refresh = button(footer, "Refresh", profile.control_button_height);
    lv_obj_set_width(p.refresh, profile.control_button_min_width);
    lv_obj_add_event_cb(
        p.refresh, [](lv_event_t*)
        {
        if (page && page->editor) editorBack();
        else if (page && page->details) closeDetails();
        else if (page && page->section == Section::Published && page->snapshot.can_create) openEditor(nullptr);
        else if (page && source) { source->refresh(page->section); refreshView(); } },
        LV_EVENT_CLICKED, nullptr);
    p.range = lv_label_create(footer);
    lv_obj_set_flex_grow(p.range, 1);
    lv_obj_set_style_text_align(p.range, LV_TEXT_ALIGN_CENTER, 0);
    styles::apply_label_muted(p.range);
    p.previous = button(footer, LV_SYMBOL_LEFT, profile.control_button_height);
    p.next = button(footer, LV_SYMBOL_RIGHT, profile.control_button_height);
    lv_obj_set_width(p.previous, profile.control_button_height);
    lv_obj_set_width(p.next, profile.control_button_height);
    lv_obj_add_event_cb(
        p.previous, [](lv_event_t*)
        {
        if (page && page->editor) { if (page->editor->confirming && !page->editor->saving) closeEditor(); else previewPublication(); }
        else if (page) { page->offset = page->offset >= page->window ? page->offset - page->window : 0; refreshView(); } },
        LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(
        p.next, [](lv_event_t*)
        {
        if (!page) return;
        if (page->editor) { saveEditor(); return; }
        if (page->details)
        {
            ::ui::geocaching::Item item;
            if (source && source->item(page->section, page->detail_index, page->detail_generation, item) &&
                item.id == page->detail_id && item.revision_hash == page->detail_hash &&
                (item.downloaded ? openMap(item) : item.can_download && source->download(item, page->detail_generation)))
            { if (!map_visit) refreshView(); }
            else lv_label_set_text(page->status, "Download could not start");
            return;
        }
        if (page->offset + page->row_count < page->snapshot.count) page->offset += page->window;
        else if (page->snapshot.has_more && source && source->loadMore()) { page->offset = 0; page->valid = false; }
        refreshView(); },
        LV_EVENT_CLICKED, nullptr);
    lv_label_set_text(p.status, " ");
    lv_obj_update_layout(p.root);
    const auto available = std::max<lv_coord_t>(0, lv_obj_get_content_height(p.list));
    p.window = std::max<size_t>(1, std::min<size_t>(kVisibleRows, (available + 3) / (profile.list_item_height + 3)));
    refreshView();
    if (p.group)
    {
        set_default_group(p.group);
        lv_group_set_editing(p.group, false);
        lv_group_focus_obj(p.tabs[static_cast<size_t>(p.section)]);
    }
    else set_default_group(p.previous_group);
    p.timer = lv_timer_create([](lv_timer_t*)
                              { refreshView(); },
                              500, nullptr);
}
void exit(void*, lv_obj_t*)
{
    if (!page) return;
    if (page->editor && page->editor->loading && source) source->cancelDraftRead(page->editor->input.id);
    delete page->editor;
    page->editor = nullptr;
    if (map_visit)
    {
        lv_async_call_cancel(returnFromMap, nullptr);
        ::gps::ui::runtime::exit(map_visit->parent);
        delete map_visit;
        map_visit = nullptr;
    }
    if (page->timer) lv_timer_delete(page->timer);
    if (page->group && lv_group_get_default() == page->group) set_default_group(page->previous_group);
    if (page->root) lv_obj_delete(page->root);
    if (page->group) lv_group_delete(page->group);
    if (source) source->activate(false);
    delete page;
    page = nullptr;
}
} // namespace geocaching::ui::shell
