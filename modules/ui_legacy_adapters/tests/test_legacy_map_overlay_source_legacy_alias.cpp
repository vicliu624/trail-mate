#include "ui_legacy_adapters/legacy_map_overlay_source.h"

#include <type_traits>

int main()
{
    static_assert(
        std::is_same<ui::presentation_sources::IMapOverlayGpsSource,
                     ui::map_overlay::IMapOverlayGpsSource>::value,
        "IMapOverlayGpsSource must stay an alias only");
    static_assert(
        std::is_same<ui::presentation_sources::IMapOverlayTeamSource,
                     ui::map_overlay::IMapOverlayTeamSource>::value,
        "IMapOverlayTeamSource must stay an alias only");
    static_assert(
        std::is_same<ui::presentation_sources::LegacyMapOverlaySource,
                     ui::map_overlay::MapOverlaySnapshotSource>::value,
        "LegacyMapOverlaySource must stay an alias only");
    return 0;
}
