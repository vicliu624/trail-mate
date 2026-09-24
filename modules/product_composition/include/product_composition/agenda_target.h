#pragma once

#include "product_composition/target_profile.h"
#include "ui_presentation/page/page_manifest.h"

namespace product_composition
{
// Capability comes from the product manifest, never from framebuffer size.
inline bool targetHasAgenda(const TargetProfile* target)
{
    if (!target || target->renderer != TargetRenderer::Lvgl || !target->has_display) return false;
    const auto* manifest = ui::presentation::findPageManifest(target->page_manifest_id);
    if (!manifest) return false;
    for (std::size_t i = 0; i < manifest->item_count; ++i)
    {
        const auto& item = manifest->items[i];
        if (item.page_id == ui::presentation::PageId::Calendar)
            return item.enabled && item.visible_in_menu;
    }
    return false;
}
} // namespace product_composition
