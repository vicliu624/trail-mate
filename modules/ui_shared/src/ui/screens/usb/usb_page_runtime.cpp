#include "ui/screens/usb/usb_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)

#include "platform/ui/device_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_14
#endif

using Host = usb_storage::ui::shell::Host;

namespace
{

const Host* s_host = nullptr;
lv_obj_t* s_root = nullptr;
lv_obj_t* s_content = nullptr;
lv_obj_t* s_status_label = nullptr;
lv_obj_t* s_loading_overlay = nullptr;
lv_obj_t* s_loading_box = nullptr;
lv_timer_t* s_status_timer = nullptr;
lv_timer_t* s_exit_timer = nullptr;
lv_timer_t* s_start_timer = nullptr;
ui::widgets::TopBar s_top_bar;
bool s_exit_started = false;

void refresh_status_cb(lv_timer_t* timer);

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

void clear_loading_overlay()
{
    if (s_loading_overlay)
    {
        lv_obj_del(s_loading_overlay);
        s_loading_overlay = nullptr;
        s_loading_box = nullptr;
    }
}

void show_loading(const char* message)
{
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer)
    {
        return;
    }

    clear_loading_overlay();

    s_loading_overlay = lv_obj_create(top_layer);
    lv_obj_set_size(s_loading_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_loading_overlay, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_loading_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_loading_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_loading_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_loading_overlay, LV_OBJ_FLAG_CLICKABLE);

    s_loading_box = lv_obj_create(s_loading_overlay);
    lv_obj_set_size(s_loading_box, 180, 80);
    lv_obj_center(s_loading_box);
    lv_obj_set_style_bg_color(s_loading_box, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_loading_box, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_loading_box, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_loading_box, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_radius(s_loading_box, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_loading_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(s_loading_box);
    ::ui::i18n::set_label_text(label, message ? message : "Loading...");
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
}

void update_status_label()
{
    if (!s_status_label)
    {
        return;
    }

    const platform::ui::usb_support::Status st = platform::ui::usb_support::get_status();
    const char* message = (st.message && st.message[0] != '\0') ? st.message : (st.active ? "USB Active" : "USB Idle");
    ::ui::i18n::set_label_text(s_status_label, message);
}

void finish_exit_cb(lv_timer_t* timer)
{
    if (timer)
    {
        lv_timer_del(timer);
    }
    s_exit_timer = nullptr;
    request_exit();
}

void start_usb_cb(lv_timer_t* timer)
{
    if (timer)
    {
        lv_timer_del(timer);
    }
    s_start_timer = nullptr;

    if (s_root == nullptr || s_exit_started)
    {
        return;
    }

    const bool started = platform::ui::usb_support::start();
    update_status_label();
    ui_set_overlay_active(started);

    if (!s_status_timer)
    {
        s_status_timer = lv_timer_create(refresh_status_cb, 250, nullptr);
    }
    refresh_status_cb(nullptr);
}

void begin_exit()
{
    if (s_exit_started)
    {
        return;
    }
    s_exit_started = true;

    show_loading(::ui::i18n::tr("Exiting USB..."));
    if (s_start_timer)
    {
        lv_timer_del(s_start_timer);
        s_start_timer = nullptr;
    }
    platform::ui::usb_support::stop();
    ui_set_overlay_active(false);

    if (!s_exit_timer)
    {
        s_exit_timer = lv_timer_create(finish_exit_cb, 60, nullptr);
    }
}

void back_event_handler(void* user_data)
{
    (void)user_data;
    begin_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE)
    {
        begin_exit();
    }
}

void refresh_status_cb(lv_timer_t* timer)
{
    (void)timer;
    ui_update_top_bar_battery(s_top_bar);
    update_status_label();

    const platform::ui::usb_support::Status st = platform::ui::usb_support::get_status();
    if (st.stop_requested)
    {
        begin_exit();
    }
}

} // namespace

namespace usb_storage::ui::runtime
{

bool is_available()
{
    return platform::ui::usb_support::is_supported();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_host = host;
    s_exit_started = false;

    if (s_status_timer)
    {
        lv_timer_del(s_status_timer);
        s_status_timer = nullptr;
    }
    if (s_exit_timer)
    {
        lv_timer_del(s_exit_timer);
        s_exit_timer = nullptr;
    }
    if (s_start_timer)
    {
        lv_timer_del(s_start_timer);
        s_start_timer = nullptr;
    }
    clear_loading_overlay();
    ui_set_overlay_active(false);

    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    s_content = nullptr;
    s_status_label = nullptr;
    s_top_bar = {};

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    ::ui::widgets::top_bar_init(s_top_bar, s_root);
    ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("USB Disk"));
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, back_event_handler, nullptr);
    if (s_top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_top_bar);

    if (app_g && s_top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_top_bar.back_btn);
        lv_group_focus_obj(s_top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    s_content = lv_obj_create(s_root);
    lv_obj_set_size(s_content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    if (!platform::ui::device::card_ready())
    {
        lv_obj_t* error_label = lv_label_create(s_content);
        ::ui::i18n::set_label_text(error_label, "SD Card Not Found\nPlease insert SD card");
        lv_obj_center(error_label);
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xCC0000), LV_PART_MAIN);
        return;
    }

    s_status_label = lv_label_create(s_content);
    ::ui::i18n::set_label_text(s_status_label, "Initializing...");
    lv_obj_center(s_status_label);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* info_label = lv_label_create(s_content);
    ::ui::i18n::set_label_text(info_label, "Press Back to exit USB mode");
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x6A5646), LV_PART_MAIN);
    lv_obj_set_style_text_opa(info_label, LV_OPA_80, LV_PART_MAIN);

    update_status_label();

    if (s_start_timer)
    {
        lv_timer_del(s_start_timer);
        s_start_timer = nullptr;
    }
    s_start_timer = lv_timer_create(start_usb_cb, 1, nullptr);
    if (s_start_timer)
    {
        lv_timer_set_repeat_count(s_start_timer, 1);
    }
    else
    {
        start_usb_cb(nullptr);
    }
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (s_status_timer)
    {
        lv_timer_del(s_status_timer);
        s_status_timer = nullptr;
    }
    if (s_exit_timer)
    {
        lv_timer_del(s_exit_timer);
        s_exit_timer = nullptr;
    }
    if (s_start_timer)
    {
        lv_timer_del(s_start_timer);
        s_start_timer = nullptr;
    }

    platform::ui::usb_support::stop();
    ui_set_overlay_active(false);
    clear_loading_overlay();

    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    s_content = nullptr;
    s_status_label = nullptr;
    s_top_bar = {};
    s_exit_started = false;
    s_host = nullptr;
}

} // namespace usb_storage::ui::runtime

void ui_usb_enter(lv_obj_t* parent)
{
    usb_storage::ui::shell::enter(nullptr, parent);
}

void ui_usb_exit(lv_obj_t* parent)
{
    usb_storage::ui::shell::exit(nullptr, parent);
}

bool ui_usb_is_active()
{
    return platform::ui::usb_support::get_status().active;
}

#else

namespace usb_storage::ui::runtime
{

bool is_available()
{
    return false;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    (void)host;
    (void)parent;
}

void exit(lv_obj_t* parent)
{
    (void)parent;
}

} // namespace usb_storage::ui::runtime

#endif
