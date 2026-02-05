#include "ui_pc_link.h"

#include "../hostlink/hostlink_service.h"
#include "assets/fonts/fonts.h"
#include "ui_common.h"
#include "widgets/top_bar.h"
#include <Arduino.h>

namespace
{
lv_obj_t* s_root = nullptr;
lv_obj_t* s_status_label = nullptr;
lv_obj_t* s_count_label = nullptr;
lv_timer_t* s_timer = nullptr;
ui::widgets::TopBar s_top_bar;

const char* status_text(hostlink::LinkState state)
{
    switch (state)
    {
    case hostlink::LinkState::Stopped:
        return "Waiting for host...";
    case hostlink::LinkState::Waiting:
        return "Waiting for host...";
    case hostlink::LinkState::Connected:
        return "Connected, handshaking...";
    case hostlink::LinkState::Handshaking:
        return "Connected, handshaking...";
    case hostlink::LinkState::Ready:
        return "Connected";
    case hostlink::LinkState::Error:
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
    hostlink::Status st = hostlink::get_status();
    if (st.state == hostlink::LinkState::Error && st.last_error != 0)
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
    ui_request_exit_to_menu();
}
} // namespace

void ui_pc_link_enter(lv_obj_t* parent)
{
    hostlink::start();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    ::ui::widgets::top_bar_init(s_top_bar, s_root);
    ::ui::widgets::top_bar_set_title(s_top_bar, "Data Exchange");
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);

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
    lv_label_set_text(title, "Data Exchange");
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

void ui_pc_link_exit(lv_obj_t* parent)
{
    (void)parent;
    hostlink::stop();

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
}
