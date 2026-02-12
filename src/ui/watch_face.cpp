#if defined(ARDUINO_T_WATCH_S3)
#include "ui/watch_face.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace
{
constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;
constexpr uint32_t kColorAmber = 0xEBA341;
constexpr uint32_t kColorLine = 0xE7C98F;

struct WatchFaceUi
{
    lv_obj_t* root = nullptr;
    lv_obj_t* node_id_label = nullptr;
    lv_obj_t* battery_label = nullptr;
    lv_obj_t* hour_label = nullptr;
    lv_obj_t* minute_label = nullptr;
    lv_obj_t* date_label = nullptr;
    lv_obj_t* sep_line = nullptr;
    void (*unlock_cb)() = nullptr;
};

WatchFaceUi s_ui;

struct SwipeState
{
    bool pressed = false;
    bool dragging = false;
    lv_coord_t offset = 0;
    lv_point_t start = {0, 0};
    lv_point_t last = {0, 0};
};

SwipeState s_swipe;

constexpr int kSwipeUnlockThreshold = 36;
constexpr int kSwipeDirectionSlop = 8;
constexpr int kSwipeDragStart = 4;

const char* battery_symbol_for_level(int percent)
{
    if (percent >= 90)
    {
        return LV_SYMBOL_BATTERY_FULL;
    }
    if (percent >= 70)
    {
        return LV_SYMBOL_BATTERY_3;
    }
    if (percent >= 50)
    {
        return LV_SYMBOL_BATTERY_2;
    }
    if (percent >= 20)
    {
        return LV_SYMBOL_BATTERY_1;
    }
    return LV_SYMBOL_BATTERY_EMPTY;
}

void apply_label_style(lv_obj_t* label, const lv_font_t* font, uint32_t color)
{
    if (!label)
    {
        return;
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
}

void watch_face_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE)
    {
        lv_indev_t* indev = lv_indev_get_act();
        if (!indev)
        {
            return;
        }
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_TOP && s_ui.unlock_cb)
        {
            s_swipe.pressed = false;
            s_swipe.dragging = false;
            s_swipe.offset = 0;
            s_ui.unlock_cb();
        }
        return;
    }

    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED)
    {
        return;
    }

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
    {
        if (code == LV_EVENT_RELEASED)
        {
            s_swipe.pressed = false;
        }
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED)
    {
        s_swipe.pressed = true;
        s_swipe.dragging = false;
        s_swipe.offset = 0;
        s_swipe.start = point;
        s_swipe.last = point;
        return;
    }

    if (!s_swipe.pressed)
    {
        return;
    }

    if (code == LV_EVENT_PRESSING)
    {
        s_swipe.last = point;
        lv_coord_t dy = static_cast<lv_coord_t>(s_swipe.last.y - s_swipe.start.y);
        if (dy > -kSwipeDragStart && dy < kSwipeDragStart && !s_swipe.dragging)
        {
            return;
        }
        s_swipe.dragging = true;
        lv_coord_t offset = dy;
        if (offset > 0)
        {
            offset = 0;
        }
        lv_coord_t max_up = -lv_obj_get_height(s_ui.root);
        if (offset < max_up)
        {
            offset = max_up;
        }
        s_swipe.offset = offset;
        lv_obj_set_y(s_ui.root, offset);
        return;
    }

    if (code == LV_EVENT_RELEASED)
    {
        s_swipe.last = point;
        s_swipe.pressed = false;
        int dx = s_swipe.last.x - s_swipe.start.x;
        int dy = s_swipe.last.y - s_swipe.start.y;
        bool should_unlock = dy < -kSwipeUnlockThreshold &&
                             (std::abs(dy) > std::abs(dx) + kSwipeDirectionSlop);
        if (should_unlock && s_ui.unlock_cb)
        {
            s_swipe.dragging = false;
            s_swipe.offset = 0;
            s_ui.unlock_cb();
            return;
        }

        if (s_swipe.dragging)
        {
            s_swipe.dragging = false;
            s_swipe.offset = 0;
            lv_obj_set_y(s_ui.root, 0);
        }
    }
}
} // namespace

void watch_face_create(lv_obj_t* parent)
{
    if (!parent || s_ui.root)
    {
        return;
    }

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_ui.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.root, watch_face_event_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(s_ui.root, watch_face_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_ui.root, watch_face_event_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(s_ui.root, watch_face_event_cb, LV_EVENT_RELEASED, nullptr);

    s_ui.node_id_label = lv_label_create(s_ui.root);
    apply_label_style(s_ui.node_id_label, &lv_font_montserrat_14, kColorTextDim);
    lv_obj_set_width(s_ui.node_id_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_ui.node_id_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_ui.node_id_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_label_set_text(s_ui.node_id_label, "ID: -");

    s_ui.battery_label = lv_label_create(s_ui.root);
    apply_label_style(s_ui.battery_label, &lv_font_montserrat_14, kColorTextDim);
    lv_obj_set_width(s_ui.battery_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_ui.battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_ui.battery_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_text(s_ui.battery_label, "?%");

    s_ui.hour_label = lv_label_create(s_ui.root);
    apply_label_style(s_ui.hour_label, &lv_font_montserrat_48, kColorText);
    lv_obj_set_width(s_ui.hour_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_ui.hour_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.hour_label, LV_ALIGN_TOP_MID, 0, 52);
    lv_label_set_text(s_ui.hour_label, "--");

    s_ui.minute_label = lv_label_create(s_ui.root);
    apply_label_style(s_ui.minute_label, &lv_font_montserrat_48, kColorText);
    lv_obj_set_width(s_ui.minute_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_ui.minute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.minute_label, LV_ALIGN_TOP_MID, 0, 118);
    lv_label_set_text(s_ui.minute_label, "--");

    s_ui.sep_line = lv_obj_create(s_ui.root);
    lv_obj_set_size(s_ui.sep_line, 160, 1);
    lv_obj_set_style_bg_color(s_ui.sep_line, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_bg_opa(s_ui.sep_line, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_ui.sep_line, 0, 0);
    lv_obj_set_style_radius(s_ui.sep_line, 0, 0);
    lv_obj_clear_flag(s_ui.sep_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_ui.sep_line, LV_ALIGN_TOP_MID, 0, 102);

    s_ui.date_label = lv_label_create(s_ui.root);
    apply_label_style(s_ui.date_label, &lv_font_montserrat_18, kColorTextDim);
    lv_obj_set_width(s_ui.date_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_ui.date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.date_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_label_set_text(s_ui.date_label, "--.-- ---");
}

void watch_face_set_time(int hour, int minute, int month, int day, const char* weekday, int battery_percent)
{
    if (!s_ui.root)
    {
        return;
    }

    char buf[32];
    if (hour < 0)
    {
        lv_label_set_text(s_ui.hour_label, "--");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%02d", hour);
        lv_label_set_text(s_ui.hour_label, buf);
    }

    if (minute < 0)
    {
        lv_label_set_text(s_ui.minute_label, "--");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%02d", minute);
        lv_label_set_text(s_ui.minute_label, buf);
    }

    const char* weekday_text = (weekday && weekday[0]) ? weekday : "---";
    if (month <= 0 || day <= 0)
    {
        snprintf(buf, sizeof(buf), "--.-- %s", weekday_text);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%02d.%02d %s", month, day, weekday_text);
    }
    lv_label_set_text(s_ui.date_label, buf);

    int level = battery_percent;
    if (level < 0)
    {
        lv_label_set_text(s_ui.battery_label, "?%");
        lv_obj_set_style_text_color(s_ui.battery_label, lv_color_hex(kColorTextDim), 0);
        return;
    }
    if (level > 100)
    {
        level = 100;
    }
    const char* symbol = battery_symbol_for_level(level);
    snprintf(buf, sizeof(buf), "%s %d%%", symbol, level);
    lv_label_set_text(s_ui.battery_label, buf);
    uint32_t battery_color = level < 20 ? kColorAmber : kColorTextDim;
    lv_obj_set_style_text_color(s_ui.battery_label, lv_color_hex(battery_color), 0);
}

void watch_face_set_node_id(uint32_t node_id)
{
    if (!s_ui.root || !s_ui.node_id_label)
    {
        return;
    }
    char buf[24];
    if (node_id != 0)
    {
        snprintf(buf, sizeof(buf), "ID: !%08lX", static_cast<unsigned long>(node_id));
    }
    else
    {
        snprintf(buf, sizeof(buf), "ID: -");
    }
    lv_label_set_text(s_ui.node_id_label, buf);
}

void watch_face_set_placeholder()
{
    if (!s_ui.root)
    {
        return;
    }
    lv_label_set_text(s_ui.hour_label, "--");
    lv_label_set_text(s_ui.minute_label, "--");
    lv_label_set_text(s_ui.date_label, "--.-- ---");
    lv_label_set_text(s_ui.battery_label, "?%");
    lv_obj_set_style_text_color(s_ui.battery_label, lv_color_hex(kColorTextDim), 0);
}

void watch_face_show(bool show)
{
    if (!s_ui.root)
    {
        return;
    }
    if (show)
    {
        lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_ui.root, 0, 0);
        lv_obj_move_foreground(s_ui.root);
    }
    else
    {
        lv_obj_set_pos(s_ui.root, 0, 0);
        lv_obj_add_flag(s_ui.root, LV_OBJ_FLAG_HIDDEN);
    }
}

bool watch_face_is_ready()
{
    return s_ui.root != nullptr;
}

bool watch_face_is_visible()
{
    if (!s_ui.root)
    {
        return false;
    }
    return !lv_obj_has_flag(s_ui.root, LV_OBJ_FLAG_HIDDEN);
}

void watch_face_set_unlock_cb(void (*cb)(void))
{
    s_ui.unlock_cb = cb;
}

#endif
