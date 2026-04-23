#include "ui/components/info_card.h"

#include <algorithm>

#include "ui/components/two_pane_styles.h"
#include "ui/theme/theme_component_style.h"
#include "ui/ui_theme.h"

namespace ui::components::info_card
{
namespace
{
constexpr lv_coord_t kTDeckBodyRowMinHeight = 18;

bool s_inited = false;
uint32_t s_theme_revision = 0;
lv_style_t s_item_base;
lv_style_t s_item_active;
lv_style_t s_header_row;

void init_once()
{
    if (s_inited)
    {
        if (s_theme_revision == ::ui::theme::active_theme_revision())
        {
            return;
        }

        lv_style_reset(&s_item_base);
        lv_style_reset(&s_item_active);
        lv_style_reset(&s_header_row);
    }
    else
    {
        s_inited = true;
    }
    s_theme_revision = ::ui::theme::active_theme_revision();

    lv_style_init(&s_item_base);
    lv_style_set_bg_opa(&s_item_base, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_base, ::ui::theme::surface_alt());
    lv_style_set_border_width(&s_item_base, 1);
    lv_style_set_border_color(&s_item_base, ::ui::theme::border());
    lv_style_set_radius(&s_item_base, 10);
    ::ui::theme::ComponentProfile item_profile{};
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::InfoCardBase,
                                               item_profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_item_base, item_profile);
    }

    lv_style_init(&s_item_active);
    lv_style_set_bg_opa(&s_item_active, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_active, ::ui::theme::surface());
    lv_style_set_border_width(&s_item_active, 2);
    lv_style_set_border_color(&s_item_active,
                              item_profile.accent_color.present ? item_profile.accent_color.value
                                                                : ::ui::theme::accent());

    lv_style_init(&s_header_row);
    lv_style_set_bg_opa(&s_header_row, LV_OPA_COVER);
    lv_style_set_bg_color(&s_header_row, ::ui::theme::accent());
    lv_style_set_border_width(&s_header_row, 0);
    lv_style_set_radius(&s_header_row, 8);
    lv_style_set_pad_left(&s_header_row, 8);
    lv_style_set_pad_right(&s_header_row, 8);
    lv_style_set_pad_top(&s_header_row, 2);
    lv_style_set_pad_bottom(&s_header_row, 2);
    ::ui::theme::ComponentProfile header_profile{};
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::InfoCardHeader,
                                               header_profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_header_row, header_profile);
    }
}

void prepare_row(lv_obj_t* row, bool transparent_background)
{
    if (!row)
    {
        return;
    }

    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(
        row,
        static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                   LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                   LV_OBJ_FLAG_SCROLL_ON_FOCUS));
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (transparent_background)
    {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    }
}

lv_obj_t* create_flex_label(lv_obj_t* parent, bool flex_grow, lv_text_align_t align)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, align, 0);
    if (flex_grow)
    {
        lv_obj_set_width(label, 0);
        lv_obj_set_flex_grow(label, 1);
    }
    else
    {
        lv_obj_set_width(label, LV_SIZE_CONTENT);
    }
    return label;
}

} // namespace

bool use_tdeck_layout()
{
#if defined(ARDUINO_T_DECK)
    return true;
#else
    return false;
#endif
}

lv_coord_t resolve_height(lv_coord_t base_height)
{
    if (!use_tdeck_layout())
    {
        return base_height;
    }

    return std::max<lv_coord_t>(46, base_height + 10);
}

void configure_item(lv_obj_t* obj, lv_coord_t base_height)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_size(obj, LV_PCT(100), resolve_height(base_height));
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    if (!use_tdeck_layout())
    {
        return;
    }

    lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(obj, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

void apply_item_style(lv_obj_t* obj)
{
    if (!use_tdeck_layout() || !obj)
    {
        return;
    }

    init_once();
    lv_obj_add_style(obj, &s_item_base, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_item_active, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(obj, &s_item_active, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_add_style(obj, &s_item_active, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(obj, &s_item_active, LV_PART_MAIN | LV_STATE_PRESSED);
}

void apply_single_line_label(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }

    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    const lv_font_t* font = static_cast<const lv_font_t*>(
        lv_obj_get_style_text_font(label, LV_PART_MAIN));
    if (!font)
    {
        return;
    }

    const lv_coord_t line_height =
        static_cast<lv_coord_t>(lv_font_get_line_height(font));
    lv_obj_set_height(label, line_height);
    lv_obj_set_style_max_height(label, line_height, 0);
}

ContentSlots create_content(lv_obj_t* parent, const ContentOptions& options)
{
    ContentSlots slots{};
    if (!parent)
    {
        return slots;
    }

    slots.header_row = lv_obj_create(parent);
    prepare_row(slots.header_row, false);
    init_once();
    lv_obj_add_style(slots.header_row, &s_header_row, LV_PART_MAIN);
    slots.header_main_label = create_flex_label(slots.header_row, true, LV_TEXT_ALIGN_LEFT);
    if (options.header_meta)
    {
        slots.header_meta_label = create_flex_label(slots.header_row, false, LV_TEXT_ALIGN_RIGHT);
    }

    slots.body_row = lv_obj_create(parent);
    prepare_row(slots.body_row, true);
    if (use_tdeck_layout())
    {
        lv_obj_set_style_min_height(slots.body_row, kTDeckBodyRowMinHeight, LV_PART_MAIN);
        lv_obj_set_style_pad_top(slots.body_row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(slots.body_row, 1, LV_PART_MAIN);
    }
    slots.body_main_label = create_flex_label(slots.body_row, true, LV_TEXT_ALIGN_LEFT);
    if (options.body_meta)
    {
        slots.body_meta_label = create_flex_label(slots.body_row, false, LV_TEXT_ALIGN_RIGHT);
    }

    return slots;
}

} // namespace ui::components::info_card
