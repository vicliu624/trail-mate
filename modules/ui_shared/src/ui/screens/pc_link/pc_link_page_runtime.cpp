#include "ui/screens/pc_link/pc_link_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/hostlink_runtime.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/fonts.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <cstdio>

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_16
#endif

namespace
{

const pc_link::ui::shell::Host* s_host = nullptr;
lv_obj_t* s_root = nullptr;
lv_obj_t* s_status_label = nullptr;
lv_obj_t* s_count_label = nullptr;
lv_timer_t* s_timer = nullptr;
ui::widgets::TopBar s_top_bar;

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

bool use_rnode_bridge()
{
    return app::appFacade().getConfig().mesh_protocol == chat::MeshProtocol::RNode;
}

const char* page_title()
{
    return use_rnode_bridge() ? "RNode Bridge" : "Data Exchange";
}

const char* page_subtitle()
{
    return use_rnode_bridge() ? "USB CDC KISS modem for Reticulum" : "Data Exchange";
}

const char* status_text(platform::ui::hostlink::LinkState state)
{
    if (use_rnode_bridge())
    {
        switch (state)
        {
        case platform::ui::hostlink::LinkState::Stopped:
        case platform::ui::hostlink::LinkState::Waiting:
            return "Waiting for Reticulum host...";
        case platform::ui::hostlink::LinkState::Connected:
        case platform::ui::hostlink::LinkState::Handshaking:
            return "Host connected, probing modem...";
        case platform::ui::hostlink::LinkState::Ready:
            return "RNode modem ready";
        case platform::ui::hostlink::LinkState::Error:
            return "Bridge error";
        default:
            return "Waiting for Reticulum host...";
        }
    }

    switch (state)
    {
    case platform::ui::hostlink::LinkState::Stopped:
        return "Waiting for host...";
    case platform::ui::hostlink::LinkState::Waiting:
        return "Waiting for host...";
    case platform::ui::hostlink::LinkState::Connected:
        return "Connected, handshaking...";
    case platform::ui::hostlink::LinkState::Handshaking:
        return "Connected, handshaking...";
    case platform::ui::hostlink::LinkState::Ready:
        return "Connected";
    case platform::ui::hostlink::LinkState::Error:
        return "Error";
    default:
        return "Waiting for host...";
    }
}

void refresh_status_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!s_status_label)
    {
        return;
    }
    ui_update_top_bar_battery(s_top_bar);
    platform::ui::hostlink::Status st = platform::ui::hostlink::get_status();
    if (st.state == platform::ui::hostlink::LinkState::Error && st.last_error != 0)
    {
        char err_buf[24];
        snprintf(err_buf, sizeof(err_buf), "Error: %lu",
                 static_cast<unsigned long>(st.last_error));
        lv_label_set_text(s_status_label, err_buf);
    }
    else
    {
        lv_label_set_text(s_status_label, status_text(st.state));
    }
    if (s_count_label)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "RX: %lu  TX: %lu",
                 static_cast<unsigned long>(st.rx_count),
                 static_cast<unsigned long>(st.tx_count));
        lv_label_set_text(s_count_label, buf);
    }
}

void on_back(void*)
{
    request_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE)
    {
        return;
    }
    request_exit();
}

} // namespace

namespace pc_link::ui::runtime
{

bool is_available()
{
    return platform::ui::hostlink::is_supported();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_host = host;
    platform::ui::hostlink::start();

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    ::ui::widgets::top_bar_init(s_top_bar, s_root);
    ::ui::widgets::top_bar_set_title(s_top_bar, page_title());
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);
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

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* stack = lv_obj_create(content);
    lv_obj_set_size(stack, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_set_style_pad_row(stack, 6, 0);
    lv_obj_center(stack);

    lv_obj_t* title = lv_label_create(stack);
    lv_label_set_text(title, page_subtitle());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    s_status_label = lv_label_create(stack);
    lv_label_set_text(s_status_label, "Waiting for host...");
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_18, 0);

    s_count_label = lv_label_create(stack);
    lv_label_set_text(s_count_label, "RX: 0  TX: 0");
    lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);

    if (!s_timer)
    {
        s_timer = lv_timer_create(refresh_status_cb, 300, nullptr);
    }
    refresh_status_cb(nullptr);
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    platform::ui::hostlink::stop();

    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    s_status_label = nullptr;
    s_count_label = nullptr;
    s_host = nullptr;
}

} // namespace pc_link::ui::runtime

#else

namespace pc_link::ui::runtime
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

} // namespace pc_link::ui::runtime

#endif
