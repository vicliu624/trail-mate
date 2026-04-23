#include "ui/presentation/boot_splash_layout.h"

#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::boot_splash_layout
{
namespace
{

void apply_component_profile(lv_obj_t* obj, ::ui::theme::ComponentSlot slot)
{
    ::ui::theme::ComponentProfile profile{};
    if (::ui::theme::resolve_component_profile(slot, profile))
    {
        ::ui::theme::apply_component_profile_to_obj(obj, profile);
    }
}

lv_align_t to_lv_align(SchemaAlign align)
{
    switch (align)
    {
    case SchemaAlign::Center:
        return LV_ALIGN_BOTTOM_MID;
    case SchemaAlign::End:
        return LV_ALIGN_BOTTOM_RIGHT;
    case SchemaAlign::Start:
    default:
        return LV_ALIGN_BOTTOM_LEFT;
    }
}

lv_text_align_t to_lv_text_align(SchemaAlign align)
{
    switch (align)
    {
    case SchemaAlign::Center:
        return LV_TEXT_ALIGN_CENTER;
    case SchemaAlign::End:
        return LV_TEXT_ALIGN_RIGHT;
    case SchemaAlign::Start:
    default:
        return LV_TEXT_ALIGN_LEFT;
    }
}

void apply_hero_schema(HeroSpec& spec)
{
    BootSplashSchema schema{};
    if (!resolve_active_boot_splash_schema(schema))
    {
        return;
    }

    if (schema.has_hero_offset_x)
    {
        spec.offset_x = schema.hero_offset_x;
    }
    if (schema.has_hero_offset_y)
    {
        spec.offset_y = schema.hero_offset_y;
    }
}

void apply_log_schema(LogSpec& spec, SchemaAlign& out_align)
{
    out_align = SchemaAlign::Start;

    BootSplashSchema schema{};
    if (!resolve_active_boot_splash_schema(schema))
    {
        return;
    }

    if (schema.has_log_inset_x)
    {
        spec.inset_x = schema.log_inset_x;
    }
    if (schema.has_log_bottom_inset)
    {
        spec.bottom_inset = schema.log_bottom_inset;
    }
    if (schema.has_log_align)
    {
        out_align = schema.log_align;
    }
}

} // namespace

void make_non_scrollable(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, spec.width, spec.height);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, spec.bg_opa, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(spec.bg_hex), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    apply_component_profile(root, ::ui::theme::ComponentSlot::BootSplashRoot);
    make_non_scrollable(root);
    return root;
}

void align_hero(lv_obj_t* hero, const HeroSpec& spec)
{
    if (!hero)
    {
        return;
    }

    HeroSpec resolved = spec;
    apply_hero_schema(resolved);
    lv_obj_align(hero, LV_ALIGN_CENTER, resolved.offset_x, resolved.offset_y);
}

lv_obj_t* create_log_label(lv_obj_t* parent, const LogSpec& spec)
{
    LogSpec resolved = spec;
    SchemaAlign align = SchemaAlign::Start;
    apply_log_schema(resolved, align);

    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, resolved.width);
    lv_obj_set_style_text_color(label, resolved.text_color, 0);
    if (resolved.font)
    {
        lv_obj_set_style_text_font(label, resolved.font, 0);
    }
    lv_obj_set_style_text_align(label, to_lv_text_align(align), 0);
    apply_component_profile(label, ::ui::theme::ComponentSlot::BootSplashLog);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    const lv_align_t lv_align = to_lv_align(align);
    const lv_coord_t x =
        lv_align == LV_ALIGN_BOTTOM_MID ? 0
        : (lv_align == LV_ALIGN_BOTTOM_LEFT ? resolved.inset_x : -resolved.inset_x);
    lv_obj_align(label, lv_align, x, -resolved.bottom_inset);
    return label;
}

} // namespace ui::presentation::boot_splash_layout
