#include "ui/presentation/watch_face_layout.h"

#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::watch_face_layout
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

void apply_primary_schema(PrimaryRegionSpec& spec)
{
    WatchFaceSchema schema{};
    if (!resolve_active_watch_face_schema(schema))
    {
        return;
    }

    if (schema.has_primary_width_pct)
    {
        spec.width_pct = schema.primary_width_pct;
    }
    if (schema.has_primary_height_pct)
    {
        spec.height_pct = schema.primary_height_pct;
    }
    if (schema.has_primary_offset_x)
    {
        spec.offset_x = schema.primary_offset_x;
    }
    if (schema.has_primary_offset_y)
    {
        spec.offset_y = schema.primary_offset_y;
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
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(spec.bg_hex), 0);
    lv_obj_set_style_bg_opa(root, spec.bg_opa, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    apply_component_profile(root, ::ui::theme::ComponentSlot::WatchFaceRoot);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_primary_region(lv_obj_t* parent, const PrimaryRegionSpec& spec)
{
    PrimaryRegionSpec resolved = spec;
    apply_primary_schema(resolved);

    lv_obj_t* primary = lv_obj_create(parent);
    lv_obj_set_size(primary,
                    LV_PCT(resolved.width_pct),
                    LV_PCT(resolved.height_pct));
    lv_obj_align(primary, LV_ALIGN_CENTER, resolved.offset_x, resolved.offset_y);
    lv_obj_set_style_bg_opa(primary, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(primary, 0, 0);
    lv_obj_set_style_radius(primary, 0, 0);
    lv_obj_set_style_pad_all(primary, 0, 0);
    apply_component_profile(primary, ::ui::theme::ComponentSlot::WatchFacePrimaryRegion);
    make_non_scrollable(primary);
    return primary;
}

} // namespace ui::presentation::watch_face_layout
