#pragma once

#include "ui_lvgl_ux_packs/packs/compatibility_ux_pack.h"
#include "ui_presentation/page/page_manifest.h"

namespace ui_lvgl_ux
{
// Transitional target pack: preserve working compatibility screens while
// admitting new screens only through this target's manifest. It is not a
// second app catalog and is not the target's final native renderer pack.
class ManifestCompatibilityUxPack final : public IUxPack
{
  public:
    constexpr ManifestCompatibilityUxPack(const char* pack_id, const char* manifest_id,
                                          ScreenClass screen_class, InputModel input_model)
        : profile_{pack_id, screen_class, input_model, MapMode::Full, ChatMode::Full,
                   true, true, true, true},
          manifest_id_(manifest_id) {}
    const char* id() const override { return profile_.id; }
    const DeviceUxProfile& profile() const override { return profile_; }
    const UxFeatureSet& features() const override { return compatibility_.features(); }
    void buildInputBindings(InputBindingSet& out) const override { compatibility_.buildInputBindings(out); }
    void buildScreens(ScreenRegistry& out) const override
    {
        compatibility_.buildScreens(out);
        const auto* manifest = ui::presentation::findPageManifest(manifest_id_);
        if (!manifest) return;
        for (std::size_t i = 0; i < manifest->item_count; ++i)
        {
            const auto& item = manifest->items[i];
            if (!item.enabled || !item.visible_in_menu) continue;
            ScreenId screen{};
            if (!screenForPage(item.page_id, screen)) continue;
            bool exists = false;
            for (std::size_t j = 0; j < out.size(); ++j)
                if (out.items()[j].id == screen) exists = true;
            if (!exists && !out.add({screen, item.binding_id, true})) return;
        }
    }

  private:
    static bool screenForPage(ui::presentation::PageId page, ScreenId& out)
    {
        using ui::presentation::PageId;
        switch (page)
        {
        case PageId::Dashboard:
            out = ScreenId::Dashboard;
            return true;
        case PageId::Chat:
            out = ScreenId::Chat;
            return true;
        case PageId::Contacts:
            out = ScreenId::Contacts;
            return true;
        case PageId::Map:
            out = ScreenId::Map;
            return true;
        case PageId::Gps:
            out = ScreenId::Gps;
            return true;
        case PageId::SkyPlot:
            out = ScreenId::SkyPlot;
            return true;
        case PageId::Team:
            out = ScreenId::Team;
            return true;
        case PageId::Tracker:
            out = ScreenId::Tracker;
            return true;
        case PageId::EnergySweep:
            out = ScreenId::EnergySweep;
            return true;
        case PageId::Settings:
            out = ScreenId::Settings;
            return true;
        case PageId::WalkieTalkie:
            out = ScreenId::WalkieTalkie;
            return true;
        case PageId::Sstv:
            out = ScreenId::Sstv;
            return true;
        case PageId::Extensions:
            out = ScreenId::Extensions;
            return true;
        case PageId::Calendar:
            out = ScreenId::Calendar;
            return true;
        default:
            return false;
        }
    }
    CompatibilityUxPack compatibility_;
    DeviceUxProfile profile_;
    const char* manifest_id_;
};
} // namespace ui_lvgl_ux
