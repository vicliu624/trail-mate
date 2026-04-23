#pragma once

#include <string>

#include "lvgl.h"
#include "team/protocol/team_location_marker.h"
#include "ui/theme/theme_registry.h"

namespace ui::theme
{

struct AssetVisual
{
    std::string asset_path;
    const lv_image_dsc_t* builtin = nullptr;
    const char* fallback_text = nullptr;
};

inline bool resolve_asset_visual(AssetSlot slot, const char* fallback_text, AssetVisual& out_visual)
{
    out_visual.asset_path.clear();
    out_visual.builtin = nullptr;
    out_visual.fallback_text = fallback_text;

    (void)resolve_asset_path(asset_slot_id(slot), out_visual.asset_path);
    out_visual.builtin = builtin_asset(slot);
    return !out_visual.asset_path.empty() || out_visual.builtin != nullptr ||
           (fallback_text != nullptr && *fallback_text != '\0');
}

inline bool asset_visual_uses_image(const AssetVisual& visual)
{
    return !visual.asset_path.empty() || visual.builtin != nullptr;
}

inline bool resolve_team_location_asset_visual(uint8_t icon_id, AssetVisual& out_visual)
{
    switch (static_cast<team::proto::TeamLocationMarkerIcon>(icon_id))
    {
    case team::proto::TeamLocationMarkerIcon::AreaCleared:
        return resolve_asset_visual(AssetSlot::TeamLocationAreaCleared, "AC", out_visual);
    case team::proto::TeamLocationMarkerIcon::BaseCamp:
        return resolve_asset_visual(AssetSlot::TeamLocationBaseCamp, "BC", out_visual);
    case team::proto::TeamLocationMarkerIcon::GoodFind:
        return resolve_asset_visual(AssetSlot::TeamLocationGoodFind, "GF", out_visual);
    case team::proto::TeamLocationMarkerIcon::Rally:
        return resolve_asset_visual(AssetSlot::TeamLocationRally, "RP", out_visual);
    case team::proto::TeamLocationMarkerIcon::Sos:
        return resolve_asset_visual(AssetSlot::TeamLocationSos, "SOS", out_visual);
    default:
        out_visual = AssetVisual{};
        return false;
    }
}

inline bool resolve_map_self_marker_asset_visual(AssetVisual& out_visual)
{
    return resolve_asset_visual(AssetSlot::MapSelfMarker, "ME", out_visual);
}

inline void apply_asset_visual(lv_obj_t* image_obj,
                               lv_obj_t* label_obj,
                               const AssetVisual& visual,
                               lv_coord_t image_size,
                               lv_color_t fallback_text_color)
{
    const bool use_image = asset_visual_uses_image(visual);

    if (image_obj != nullptr)
    {
        if (!visual.asset_path.empty())
        {
            lv_image_set_src(image_obj, visual.asset_path.c_str());
        }
        else if (visual.builtin != nullptr)
        {
            lv_image_set_src(image_obj, visual.builtin);
        }

        lv_obj_set_size(image_obj, image_size, image_size);
        lv_image_set_inner_align(image_obj, LV_IMAGE_ALIGN_CONTAIN);
        if (use_image)
        {
            lv_obj_clear_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_center(image_obj);
    }

    if (label_obj != nullptr)
    {
        const bool show_text = !use_image && visual.fallback_text != nullptr && *visual.fallback_text != '\0';
        lv_label_set_text(label_obj, show_text ? visual.fallback_text : "");
        lv_obj_set_style_text_color(label_obj, fallback_text_color, 0);
        if (show_text)
        {
            lv_obj_clear_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_center(label_obj);
    }
}

} // namespace ui::theme
