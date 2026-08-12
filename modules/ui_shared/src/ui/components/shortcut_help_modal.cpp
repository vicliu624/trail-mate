#include "ui/components/shortcut_help_modal.h"

#include "ui/app_runtime.h"

#if defined(ARDUINO_T_DECK_PRO)
#include "ui/assets/fonts/font_utils.h"
#include "ui/tdeck_pro/text_font.h"
#endif

#include <cstring>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif

namespace ui::components::shortcut_help_modal
{
namespace
{

constexpr uint32_t kScrim = 0x1C1812;
constexpr uint32_t kPanel = 0xFFF3DF;
constexpr uint32_t kKeycap = 0xF8E6C3;
constexpr uint32_t kBorder = 0x8A6E43;
constexpr uint32_t kText = 0x25170D;
constexpr uint32_t kTextDim = 0x3E2B18;
constexpr uint32_t kPagerRotateUpKey = 19;
constexpr uint32_t kPagerRotateDownKey = 20;

bool close_key(uint32_t key)
{
    return key == LV_KEY_BACKSPACE ||
           key == LV_KEY_ESC ||
           key == LV_KEY_ENTER ||
           key == 'h' ||
           key == 'H';
}

bool up_key(uint32_t key)
{
    return key == LV_KEY_UP || key == kPagerRotateUpKey || key == 'w' || key == 'W';
}

bool down_key(uint32_t key)
{
    return key == LV_KEY_DOWN || key == kPagerRotateDownKey || key == 's' || key == 'S';
}

void consume(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
}

lv_coord_t fitted_size(lv_coord_t requested, lv_coord_t available, lv_coord_t minimum)
{
    lv_coord_t value = requested > 0 ? requested : available;
    if (available > 0 && value > available)
    {
        value = available;
    }
    if (value < minimum)
    {
        value = minimum;
    }
    return value;
}

lv_coord_t keycap_width(const char* text, bool compact)
{
    const std::size_t len = text ? std::strlen(text) : 0;
    if (compact)
    {
        return len > 4 ? 48 : (len > 2 ? 34 : 22);
    }
    return 72;
}

const lv_font_t* help_font()
{
#if defined(ARDUINO_T_DECK_PRO)
    return ::ui::tdeck_pro::text_font();
#else
    return &lv_font_montserrat_10;
#endif
}

void apply_help_text(lv_obj_t* label, const char* text)
{
#if defined(ARDUINO_T_DECK_PRO)
    ::ui::fonts::apply_localized_font(label, text, help_font());
#else
    (void)text;
    lv_obj_set_style_text_font(label, help_font(), 0);
#endif
}

lv_obj_t* create_keycap(lv_obj_t* parent, const char* text, lv_coord_t width)
{
    lv_obj_t* keycap = lv_label_create(parent);
    lv_obj_set_size(keycap, width,
#if defined(ARDUINO_T_DECK_PRO)
                    16
#else
                    14
#endif
    );
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_bg_color(keycap, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keycap, 1, 0);
    lv_obj_set_style_border_color(keycap, lv_color_black(), 0);
    lv_obj_set_style_radius(keycap, 0, 0);
    lv_obj_set_style_text_color(keycap, lv_color_black(), 0);
#else
    lv_obj_set_style_bg_color(keycap, lv_color_hex(kKeycap), 0);
    lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keycap, 1, 0);
    lv_obj_set_style_border_color(keycap, lv_color_hex(kBorder), 0);
    lv_obj_set_style_radius(keycap, 3, 0);
    lv_obj_set_style_text_color(keycap, lv_color_hex(kText), 0);
#endif
    lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
    lv_label_set_text(keycap, text ? text : "");
    apply_help_text(keycap, text ? text : "");
    return keycap;
}

void add_row(lv_obj_t* parent, const Row& row)
{
    lv_obj_t* row_obj = lv_obj_create(parent);
    lv_obj_set_size(row_obj, LV_PCT(100),
#if defined(ARDUINO_T_DECK_PRO)
                    18
#else
                    15
#endif
    );
    lv_obj_set_flex_flow(row_obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_obj,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_obj, 0, 0);
    lv_obj_set_style_pad_all(row_obj, 0, 0);
    lv_obj_set_style_pad_column(row_obj, 3, 0);
    lv_obj_clear_flag(row_obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* keys = lv_obj_create(row_obj);
    lv_obj_set_size(keys, 76,
#if defined(ARDUINO_T_DECK_PRO)
                    18
#else
                    15
#endif
    );
    lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(keys,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keys, 0, 0);
    lv_obj_set_style_pad_all(keys, 0, 0);
    lv_obj_set_style_pad_column(keys, 2, 0);
    lv_obj_clear_flag(keys, LV_OBJ_FLAG_SCROLLABLE);

    if (row.secondary && row.secondary[0] != '\0')
    {
        create_keycap(keys, row.primary, keycap_width(row.primary, true));
        create_keycap(keys, row.secondary, keycap_width(row.secondary, true));
    }
    else
    {
        create_keycap(keys, row.primary, keycap_width(row.primary, false));
    }

    lv_obj_t* text = lv_label_create(row_obj);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
#else
    lv_obj_set_style_text_color(text, lv_color_hex(kTextDim), 0);
#endif
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_label_set_text(text, row.description ? row.description : "");
    apply_help_text(text, row.description ? row.description : "");
}

void on_key(lv_event_t* event)
{
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (!state || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (close_key(key))
    {
        close(*state);
        consume(event);
        return;
    }

    if ((up_key(key) || down_key(key)) && state->body && lv_obj_is_valid(state->body))
    {
        lv_obj_scroll_by(state->body, 0, up_key(key) ? 18 : -18, LV_ANIM_OFF);
        consume(event);
        return;
    }

    consume(event);
}

} // namespace

bool is_open(const State& state)
{
    return state.overlay && lv_obj_is_valid(state.overlay);
}

void focus(State& state)
{
    if (!is_open(state))
    {
        return;
    }
    if (state.panel && lv_obj_is_valid(state.panel))
    {
        lv_group_focus_obj(state.panel);
    }
}

bool open(State& state, lv_obj_t* parent, const Config& config)
{
    if (is_open(state))
    {
        focus(state);
        return true;
    }

    lv_obj_t* host = parent ? parent : lv_screen_active();
    if (!host)
    {
        return false;
    }

    lv_obj_update_layout(host);
    const lv_coord_t host_w = lv_obj_get_width(host);
    const lv_coord_t host_h = lv_obj_get_height(host);
#if defined(ARDUINO_T_DECK_PRO)
    const lv_coord_t panel_w = fitted_size(config.width, host_w - 16, 200);
    const lv_coord_t panel_h = fitted_size(config.height, host_h - 16, 144);
#else
    const lv_coord_t panel_w = fitted_size(config.width, host_w - 8, 220);
    const lv_coord_t panel_h = fitted_size(config.height, host_h - 8, 132);
#endif

    state.previous_group = config.restore_group ? config.restore_group : lv_group_get_default();
    state.group = lv_group_create();
    if (state.group)
    {
        set_default_group(state.group);
    }

    state.overlay = lv_obj_create(host);
    lv_obj_set_size(state.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_align(state.overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(state.overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(state.overlay, LV_OBJ_FLAG_CLICKABLE);
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_bg_color(state.overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(state.overlay, LV_OPA_COVER, 0);
#else
    lv_obj_set_style_bg_color(state.overlay, lv_color_hex(kScrim), 0);
    lv_obj_set_style_bg_opa(state.overlay, LV_OPA_70, 0);
#endif
    lv_obj_set_style_border_width(state.overlay, 0, 0);
    lv_obj_set_style_pad_all(state.overlay, 4, 0);
    lv_obj_clear_flag(state.overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(state.overlay, on_key, LV_EVENT_KEY, &state);

    state.panel = lv_obj_create(state.overlay);
    lv_obj_set_size(state.panel, panel_w, panel_h);
    lv_obj_center(state.panel);
    lv_obj_set_flex_flow(state.panel, LV_FLEX_FLOW_COLUMN);
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_bg_color(state.panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(state.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.panel, 1, 0);
    lv_obj_set_style_border_color(state.panel, lv_color_black(), 0);
    lv_obj_set_style_radius(state.panel, 0, 0);
#else
    lv_obj_set_style_bg_color(state.panel, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(state.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.panel, 1, 0);
    lv_obj_set_style_border_color(state.panel, lv_color_hex(kBorder), 0);
    lv_obj_set_style_radius(state.panel, 4, 0);
#endif
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_pad_left(state.panel, 6, 0);
    lv_obj_set_style_pad_right(state.panel, 6, 0);
    lv_obj_set_style_pad_top(state.panel, 6, 0);
    lv_obj_set_style_pad_bottom(state.panel, 6, 0);
#else
    lv_obj_set_style_pad_left(state.panel, 7, 0);
    lv_obj_set_style_pad_right(state.panel, 7, 0);
    lv_obj_set_style_pad_top(state.panel, 5, 0);
    lv_obj_set_style_pad_bottom(state.panel, 5, 0);
#endif
    lv_obj_set_style_pad_row(state.panel, 3, 0);
    lv_obj_add_flag(state.panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(state.panel, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(state.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(state.panel, on_key, LV_EVENT_KEY, &state);

    lv_obj_t* title_row = lv_obj_create(state.panel);
    lv_obj_set_size(title_row, LV_PCT(100),
#if defined(ARDUINO_T_DECK_PRO)
                    18
#else
                    20
#endif
    );
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(title_row);
    lv_label_set_text(title, config.title ? config.title : "Help");
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
#if defined(ARDUINO_T_DECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(kText), 0);
#endif
    apply_help_text(title, config.title ? config.title : "Help");

    state.body = lv_obj_create(state.panel);
    lv_obj_set_width(state.body, LV_PCT(100));
    lv_obj_set_height(state.body, 0);
    lv_obj_set_flex_grow(state.body, 1);
    lv_obj_set_flex_flow(state.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(state.body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state.body, 0, 0);
    lv_obj_set_style_pad_all(state.body, 0, 0);
    lv_obj_set_style_pad_row(state.body, 2, 0);
    lv_obj_add_flag(state.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(state.body, on_key, LV_EVENT_KEY, &state);

    for (std::size_t index = 0; index < config.row_count; ++index)
    {
        add_row(state.body, config.rows[index]);
    }

    lv_obj_move_foreground(state.overlay);
    if (state.group)
    {
        lv_group_add_obj(state.group, state.panel);
        lv_group_focus_obj(state.panel);
        lv_group_set_editing(state.group, true);
    }
    return true;
}

void close(State& state)
{
    if (state.overlay && lv_obj_is_valid(state.overlay))
    {
        lv_obj_del(state.overlay);
    }
    state.overlay = nullptr;
    state.panel = nullptr;
    state.body = nullptr;
    if (state.previous_group)
    {
        set_default_group(state.previous_group);
    }
    if (state.group)
    {
        lv_group_del(state.group);
    }
    state.group = nullptr;
    state.previous_group = nullptr;
}

} // namespace ui::components::shortcut_help_modal
