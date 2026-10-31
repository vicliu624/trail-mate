#include "ui/input/pager_navigation.h"
#include "platform/ui/radio_iq_runtime.h"
#include "platform/ui/tmap_creation_runtime.h"
#include "platform/ui/walkie_runtime.h"
#if defined(ARDUINO_T_LORA_PAGER) || defined(__linux__)
#include "platform/memory/psram_ptr.h"
#include "platform/ui/psram_ui_lease.h"
#include "ui/app_runtime.h"
#include "ui/components/shortcut_help_modal.h"
#include "ui/localization.h"
#include "ui/menu/menu_runtime.h"
#include "ui/widgets/ime/ime_widget.h"
#include <cstring>

namespace ui::input::pager
{
namespace
{
namespace help = ui::components::shortcut_help_modal;
struct PageHelp
{
    lv_obj_t* root = nullptr;
    lv_obj_t* title = nullptr;
    PageHelp* next = nullptr;
    help::State modal{};
};
PageHelp* pages = nullptr;
uint32_t consumed_key = 0;

bool group_exists(lv_group_t* target)
{
    for (uint32_t i = 0; i < lv_group_get_count(); ++i)
        if (lv_group_by_index(i) == target) return true;
    return false;
}
bool group_bound(lv_group_t* target)
{
    for (auto* device = lv_indev_get_next(nullptr); device; device = lv_indev_get_next(device))
        if (lv_indev_get_type(device) == LV_INDEV_TYPE_KEYPAD && lv_indev_get_group(device) == target) return true;
    return false;
}
void close_help(PageHelp& page)
{
    // Some app exits destroy their input group before deleting page objects.
    if (!group_exists(page.modal.previous_group)) page.modal.previous_group = nullptr;
    help::close(page.modal);
}
void deleted(lv_event_t* event)
{
    auto* page = static_cast<PageHelp*>(lv_event_get_user_data(event));
    close_help(*page);
    auto** link = &pages;
    while (*link && *link != page) link = &(*link)->next;
    if (*link) *link = page->next;
    platform::memory::destroyPsram(page);
}
PageHelp* page_for(lv_obj_t* focused)
{
    // The closest registered ancestor owns nested pages. A modal outside the
    // page's subtree keeps its own shortcuts and input group.
    for (auto* ancestor = focused; ancestor; ancestor = lv_obj_get_parent(ancestor))
        for (auto* page = pages; page; page = page->next)
            if (page->root == ancestor) return page;
    return nullptr;
}
bool in_text_editor(lv_obj_t* focused)
{
    for (auto* object = focused; object; object = lv_obj_get_parent(object))
        if (lv_obj_check_type(object, &lv_textarea_class)) return true;
    return false;
}
bool contains_editor(lv_obj_t* root)
{
    if (lv_obj_check_type(root, &lv_textarea_class)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (contains_editor(lv_obj_get_child(root, i))) return true;
    return false;
}
bool native_help(const PageHelp& page, const char* id)
{
    if (!id) return false;
    if (!std::strcmp(id, "tracker") && page.title &&
        !std::strcmp(lv_label_get_text(page.title), ui::i18n::tr("Route Preview"))) return true;
    if (!std::strcmp(id, "map") || !std::strcmp(id, "network") || !std::strcmp(id, "contacts") || !std::strcmp(id, "node_info")) return true;
    return !std::strcmp(id, "chat") && page.title &&
           std::strcmp(lv_label_get_text(page.title), ui::i18n::tr("MESSAGES")) && !contains_editor(page.root);
}
bool simple_navigation(const char* id)
{
    return id && (!std::strcmp(id, "team") || !std::strcmp(id, "sky_plot") || !std::strcmp(id, "usb_mass_storage") ||
                  !std::strcmp(id, "walkie_talkie") || !std::strcmp(id, "sstv") || !std::strcmp(id, "extensions"));
}
void show_help(PageHelp& page, lv_group_t* group)
{
    help::Row rows[32] = {
#if defined(__linux__)
        {"Tab", nullptr, "Select next control"},
        {"Enter", nullptr, "Activate / enter editing"},
        {"Esc", nullptr, "Back / close"},
#else
        {"Wheel", nullptr, "Select control"},
        {"Press", "Enter", "Activate / enter editing"},
        {"Hold wheel", nullptr, "Leave editing / select controls"},
#endif
        {"Back", nullptr, "Back (delete text inside editor)"},
        {"H", nullptr, "Help outside text editors"},
    };
    unsigned count = 5;
    const auto* app = ui_get_active_app();
    const char* id = app ? app->stable_id() : "";
    if (id && !std::strcmp(id, "walkie_talkie")) platform::ui::walkie::set_ptt(false);
    if (simple_navigation(id)) rows[count++] = {"Up/Down", "W/S", "Previous / next control"};
    const char* title = page.title ? lv_label_get_text(page.title) : "";
    const bool creator = std::strncmp(title, ui::i18n::tr("Make TMAP"), std::strlen(ui::i18n::tr("Make TMAP"))) == 0;
    if (id && !std::strcmp(id, "oneseg_tv"))
    {
        rows[count++] = {"Space", nullptr, "Start / stop onboard receiver"};
        rows[count++] = {"R / Retry", nullptr, "Release and reacquire selected channel"};
        rows[count++] = {"P", nullptr, "Select next received service in this channel"};
        rows[count++] = {"F", nullptr, "Toggle fullscreen video"};
        rows[count++] = {"CH -/+", nullptr, "Tune channel / retune receiver"};
        rows[count++] = {"Station -/+", nullptr, "Previous / next saved station"};
        rows[count++] = {"M / Menu", nullptr, "Saved channels and automatic scan"};
        rows[count++] = {"Menu > Programme info", nullptr, "Received titles, descriptions and JST schedule"};
        rows[count++] = {"Swipe / Up/Down", nullptr, "Scroll programme information"};
        rows[count++] = {"Close info / Esc", nullptr, "Return to television view"};
        rows[count++] = {"Scan channels", nullptr, "Search UHF 13-62; audio muted"};
        rows[count++] = {"Cancel / Esc", nullptr, "Cancel scan; retain previous channels"};
        rows[count++] = {"Vol -/+", nullptr, "Change television volume"};
    }
    else if (id && !std::strcmp(id, "fm_radio"))
    {
        rows[count++] = {"Space", nullptr, "Start / stop independent FM receiver"};
        rows[count++] = {"Stop", nullptr, "Stop / retry cleanup when offered"};
        rows[count++] = {"[ / ]", nullptr, "Tune -/+ 0.1 MHz (100-108 MHz)"};
        rows[count++] = {"- / +", nullptr, "Change FM volume by 5 percent"};
        rows[count++] = {"IMG_CAL", nullptr, "Explicit exception; toggle only while stopped"};
        rows[count++] = {"Back", nullptr, "Cancel receiver and return to Menu"};
    }
    else if (creator)
        rows[count++] = {"Enter", nullptr, "Activate Next / Back / Create"};
    else if (id && std::strcmp(id, "music") == 0)
    {
        rows[count++] = {"Space", nullptr, "Play / pause"};
        rows[count++] = {"A/D", nullptr, "Previous / next track in player"};
        rows[count++] = {"W/S", nullptr, "Volume in player; browse in lists"};
        rows[count++] = {"B", "L", "Library / lyrics"};
    }
    else if (id && std::strcmp(id, "calculator") == 0)
    {
        rows[1] = {"Enter", "=", "Calculate expression"};
        rows[3] = {"Backspace", nullptr, "Delete preceding character"};
        rows[count++] = {"0-9", "+ - * /", "Enter expression"};
        rows[count++] = {"Left/Right", nullptr, "Move expression cursor"};
        rows[count++] = {"( )", ". %", "Parentheses / decimal / percent"};
        rows[count++] = {"s/c/t", nullptr, "sin / cos / tan"};
        rows[count++] = {"S/C/T", nullptr, "asin / acos / atan (Shift)"};
        rows[count++] = {"l", "G", "Natural / common logarithm"};
        rows[count++] = {"I", nullptr, "Reciprocal"};
        rows[count++] = {"F", "M", "Second functions / DEG-RAD"};
        rows[count++] = {"P", "A", "Pi / previous answer"};
        rows[count++] = {"Q", "R", "Square / square root"};
    }
    else if (id && std::strcmp(id, "energy_sweep") == 0)
    {
        rows[count++] = {"Up/Down", nullptr, platform::ui::radio_iq::is_supported() ? "Select profile / scroll IQ" : "Select observed profile"};
        rows[count++] = {"Enter", nullptr, "Confirm selected profile"};
        rows[count++] = {"S", nullptr, "Start / stop protocol probe"};
        if (platform::ui::radio_iq::is_supported()) rows[count++] = {"I", nullptr, "Finite IQ snapshot"};
    }
    else if (id && std::strcmp(id, "chat") == 0)
    {
        rows[count++] = {"M", "F", "Open / close conversation filter"};
        rows[count++] = {"/", "S", "Search conversations"};
        rows[count++] = {"Enter", "Space", "Selected conversation actions"};
        rows[count++] = {"C", "I", "Open conversation / information"};
        rows[count++] = {"P", nullptr, "Ping selected Reticulum destination"};
        rows[count++] = {"D", nullptr, "Confirm conversation deletion"};
    }
    else if (id && (std::strcmp(id, "contacts") == 0 || std::strcmp(id, "settings") == 0 ||
                    std::strcmp(id, "tracker") == 0 || std::strcmp(id, "extensions") == 0 || std::strcmp(id, "network") == 0))
        rows[count++] = {"M", nullptr, "Open / close page menu"};
    if (!creator && id && (std::strcmp(id, "chat") == 0 || std::strcmp(id, "settings") == 0 || std::strcmp(id, "tracker") == 0 || std::strcmp(id, "extensions") == 0))
        rows[count++] = {"Up/Down", "Enter", "Choose / activate an open menu item"};
    if (id && std::strcmp(id, "walkie_talkie") == 0)
    {
#if defined(TRAIL_MATE_UI_P4_TOUCH)
        rows[count++] = {"Enter", nullptr, "Toggle focused monitor control"};
        rows[count++] = {"Space", nullptr, "Hold to talk; release to listen"};
#else
        rows[count++] = {"Enter", "Space", "Toggle focused monitor control"};
#endif
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        rows[count++] = {"T", nullptr, "Talk / listen; Help and Back stop TX"};
#endif
    }
    if (id && std::strcmp(id, "sky_plot") == 0)
        rows[count++] = {"Tab", "Enter", "Select and toggle satellite status"};
    if (id && std::strcmp(id, "calendar") == 0)
        rows[count++] = {"Tab", "Enter", "Choose New / Today / Date / More"};
    if (id && std::strcmp(id, "geocaching") == 0)
        rows[count++] = {"Tab", "Enter", "Choose tab, cache or available action"};
    if (!creator && id && std::strcmp(id, "extensions") == 0 && platform::ui::tmap_creation::supported())
        rows[count++] = {"T", nullptr, "Make TMAP"};
    rows[count++] = {"Up/Down", "W/S", "Scroll help"};
    rows[count++] = {"H", "Esc", "Close help; restore focus"};
    platform::ui::PsramUiLease memory;
    help::Config config;
    config.title = page.title ? lv_label_get_text(page.title) : "Page Help";
    config.rows = rows;
    config.row_count = count;
    config.width = 360;
    // Fit the rows on larger screens; the modal clamps to the host on small ones.
    config.height = static_cast<lv_coord_t>(44 + count * 20);
    config.restore_group = group;
    help::open(page.modal, page.root, config);
}
} // namespace

bool help_open()
{
    for (auto* page = pages; page; page = page->next)
        if (help::is_open(page->modal)) return true;
    return false;
}

bool open_help(lv_obj_t* root, lv_group_t* group)
{
    auto* page = page_for(root);
    if (!page || ui_is_overlay_active()) return false;
    if (help::is_open(page->modal)) close_help(*page);
    else
    {
        auto* app = ui_get_active_app();
        const char* id = app ? app->stable_id() : nullptr;
        if (native_help(*page, id))
        {
            uint32_t key = 'h';
            lv_obj_send_event(page->root, LV_EVENT_KEY, &key);
        }
        else show_help(*page, group);
    }
    return true;
}

void install_help(lv_obj_t* root, lv_obj_t* title)
{
    if (!root) return;
    auto* bar = title ? lv_obj_get_parent(title) : root;
    lv_obj_update_layout(root);
    // Some pages wrap the bar in a fixed-height header. Register that header's
    // page, but keep nested page/dialog roots separate from the outer app.
    if (lv_obj_get_parent(root) && lv_obj_get_height(root) <= lv_obj_get_height(bar))
        root = lv_obj_get_parent(root);
    auto* page = platform::memory::createPsram<PageHelp>();
    if (!page) return;
    page->root = root;
    page->title = title;
    page->next = pages;
    pages = page;
    lv_obj_add_event_cb(bar, deleted, LV_EVENT_DELETE, page);
}
void prepare_group(lv_group_t* group)
{
    if (!group) return;
    // LVGL's encoder processing returns immediately with a null focus, even
    // when its group has navigable objects. Repair that before dispatch.
    auto* focused = lv_group_get_focused(group);
    if (ui::widgets::ime_owns_text_focus(focused)) return;
    bool hidden = false;
    for (auto* object = focused; object; object = lv_obj_get_parent(object))
        if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) hidden = true;
    if (!focused || hidden || lv_obj_has_state(focused, LV_STATE_DISABLED))
    {
        lv_group_set_editing(group, false);
        lv_group_focus_next(group);
    }
    else
    {
        // A clipped focus is still valid. Complete its scroll before routing
        // the next key instead of silently skipping that control.
        lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
    }
    platform::ui::PsramUiLease memory;
    for (unsigned i = 0; i < lv_group_get_obj_count(group); ++i)
    {
        auto* object = lv_group_get_obj_by_index(group, i);
        // An inset outline is visible without joining the top bar or clipping
        // adjacent rows. Preserve each page's ordinary pointer/checked style.
        lv_style_value_t value;
        if (lv_obj_get_local_style_prop(object, LV_STYLE_OUTLINE_WIDTH, &value, LV_STATE_FOCUS_KEY) != LV_STYLE_RES_FOUND || value.num != 2)
            lv_obj_set_style_outline_width(object, 2, LV_STATE_FOCUS_KEY);
        if (lv_obj_get_local_style_prop(object, LV_STYLE_OUTLINE_PAD, &value, LV_STATE_FOCUS_KEY) != LV_STYLE_RES_FOUND || value.num != -2)
            lv_obj_set_style_outline_pad(object, -2, LV_STATE_FOCUS_KEY);
        const auto color = lv_color_hex(0x6B4A1E);
        if (lv_obj_get_local_style_prop(object, LV_STYLE_OUTLINE_COLOR, &value, LV_STATE_FOCUS_KEY) != LV_STYLE_RES_FOUND ||
            lv_color_to_int(value.color) != lv_color_to_int(color))
            lv_obj_set_style_outline_color(object, color, LV_STATE_FOCUS_KEY);
    }
}
bool handle_key(uint32_t key, bool pressed, lv_group_t* group)
{
    if (!pressed && consumed_key != 0 && consumed_key == key)
    {
        consumed_key = 0;
        return true;
    }
    if (!pressed ||
        ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::App || !group) return false;
    auto* focused = lv_group_get_focused(group);
    auto* page = page_for(focused);
    if (!page || in_text_editor(focused) || ui::widgets::ime_owns_text_focus(focused)) return false;
    auto* app = ui_get_active_app();
    const char* id = app ? app->stable_id() : nullptr;
    if (!help::is_open(page->modal) && simple_navigation(id) &&
        !lv_group_get_editing(group) &&
        (key == 'w' || key == 'W' || key == 's' || key == 'S' || key == LV_KEY_UP || key == LV_KEY_DOWN))
    {
        lv_group_set_editing(group, false);
        if (key == 'w' || key == 'W' || key == LV_KEY_UP) lv_group_focus_prev(group);
        else lv_group_focus_next(group);
        if (auto* next = lv_group_get_focused(group)) lv_obj_scroll_to_view(next, LV_ANIM_OFF);
        consumed_key = key;
        return true;
    }
    if (key != 'h' && key != 'H') return false;
    if (help::is_open(page->modal)) close_help(*page);
    else
    {
        if (ui_is_overlay_active()) return false;
        // Keep mature page-specific help (Map, Contacts, Network, chat
        // history), including their context-sensitive modal behavior.
        if (native_help(*page, id))
        {
            lv_group_send_data(group, key);
            // Some focusable containers have no key bubbling route to the
            // page (e.g. Network's browser rail). The page's own handler still
            // owns its detailed help, regardless of which control was selected.
            if (group_bound(group) && group_exists(group) && lv_group_get_focused(group) == focused &&
                (std::strcmp(id, "network") == 0 || std::strcmp(id, "contacts") == 0 || std::strcmp(id, "map") == 0))
                lv_obj_send_event(page->root, LV_EVENT_KEY, &key);
        }
        else show_help(*page, group);
    }
    consumed_key = key;
    return true;
}
} // namespace ui::input::pager
#endif
