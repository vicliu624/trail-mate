#ifndef GPS_PAGE_INPUT_H
#define GPS_PAGE_INPUT_H

#include "lvgl.h"
#include <cstdint>

// Encoder rotation keycodes (from LVGL encoder driver)
// These are the actual keycodes sent when the rotary encoder rotates.
#define ENCODER_KEY_ROTATE_DOWN 20 // Scroll down (clockwise)
#define ENCODER_KEY_ROTATE_UP 19   // Scroll up (counter‑clockwise)

enum class ControlId : uint8_t
{
    BackBtn,
    ZoomBtn,
    PosBtn,
    PanHBtn,
    PanVBtn,
    TrackerBtn,
    LayerBtn,
    RouteBtn,
    PanHIndicator,
    PanVIndicator,
    ZoomValueLabel,
    ZoomWin,
    Map,
    Page
};

struct ControlTag
{
    ControlId id;
};

ControlId ctrl_id(lv_obj_t* obj);
void set_control_id(lv_obj_t* obj, ControlId id);
void reset_control_tags(); // Reset control-tag pool (call when entering the page)

void on_ui_event(lv_event_t* e);
void pan_indicator_event_cb(lv_event_t* e);

void zoom_popup_handle_rotary(int32_t diff);
void zoom_popup_handle_key(lv_key_t key, lv_event_t* e);

#endif // GPS_PAGE_INPUT_H
