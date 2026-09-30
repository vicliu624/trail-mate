#include "ui_presentation/geocaching/local_map_overlay.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

using namespace ui::geocaching;
namespace
{
enum class TrackOverlayFileKind
{
    Track,
    Route
};
bool s_track_overlay_active = false;
bool s_route_image_strip_visible = false;
TrackOverlayFileKind s_track_overlay_kind = TrackOverlayFileKind::Track;
// Compile the actual shared Map filter, rather than duplicating its rules.
#include "geocaching_map_filter.inc"
} // namespace
struct LocalSource : Source
{
    std::vector<Item> drafts, downloaded;
    Section section = Section::Published;
    size_t offset = 0;
    bool ready = false, busy = false;
    unsigned requests = 0;
    void snapshot(Section part, Snapshot& out) override
    {
        assert(part != Section::Discover);
        out = {};
        out.ready = ready;
        out.busy = busy;
        out.generation = 1;
        out.count = part == Section::Published ? drafts.size() : downloaded.size();
    }
    void requestWindow(Section part, size_t first, size_t count) override
    {
        assert(part != Section::Discover && count == 4);
        section = part;
        offset = first;
        ++requests;
    }
    bool item(Section part, size_t index, uint64_t, Item& out) override
    {
        assert(part == section && index >= offset && index < offset + 4);
        auto& rows = part == Section::Published ? drafts : downloaded;
        if (index >= rows.size()) return false;
        out = rows[index];
        return true;
    }
    void refresh(Section) override { assert(false); }
    void open(const Item&, uint64_t) override { assert(false); }
};
Item marker(unsigned id, bool draft, bool located = true)
{
    Item row;
    row.id[0] = uint8_t(id);
    row.is_draft = draft;
    row.has_coordinates = located;
    row.downloaded = !draft;
    row.latitude_e7 = int32_t(id) * 1000000;
    row.longitude_e7 = 1020000000;
    std::snprintf(row.name.data(), row.name.size(), "Cache %u", id);
    return row;
}
int main()
{
    {
        LocalSource available;
        available.ready = true;
        available.downloaded.push_back(marker(1, false));
        available.drafts.push_back(marker(2, true));
        auto immediate = std::make_unique<LocalMapOverlay>();
        immediate->update(available, 0, 102, 10);
        auto result = std::make_unique<ui::map::MapOverlaySnapshot>();
        immediate->append(*result);
        assert(result->item_count == 2); // No extra timer tick for cached drafts.
    }
    LocalSource source;
    source.drafts.push_back(marker(1, true, false));
    for (unsigned id = 2; id <= 10; ++id) source.drafts.push_back(marker(id, true));
    source.downloaded.push_back(marker(11, false));
    auto projection = std::make_unique<LocalMapOverlay>();
    auto out = std::make_unique<ui::map::MapOverlaySnapshot>();
    projection->update(source, 0, 102, 10);
    projection->append(*out);
    assert(out->item_count == 0); // Initial loading is not an empty catalog.
    source.ready = true;
    for (int i = 0; i < 10; ++i) projection->update(source, 0, 102, 10);
    out = std::make_unique<ui::map::MapOverlaySnapshot>();
    projection->append(*out);
    assert(out->item_count == 10); // Includes drafts beyond the first four rows.
    assert(out->header.valid);     // Local markers render even without a GPS fix.
    keep_only_current_position_overlay(*out);
    assert(out->item_count == 10); // Hiding map chrome must retain saved places.
    assert(out->items[0].style == ui::map::MapOverlayStyle::Warning);
    assert(out->items[9].style == ui::map::MapOverlayStyle::Default);
    const auto requests = source.requests;
    projection->update(source, 0, 102, 10);
    assert(source.requests == requests); // A stationary map does not rescan storage.

    // Moving the viewport while metadata is loading must not repeatedly restart
    // at Downloaded and starve the later local-draft pages.
    auto moving = std::make_unique<LocalMapOverlay>();
    for (int i = 0; i < 4; ++i)
    {
        moving->update(source, i * 0.1, 102, 10);
        out = std::make_unique<ui::map::MapOverlaySnapshot>();
        moving->append(*out);
        if (out->item_count == 10) break;
    }
    assert(out->item_count == 10);

    for (unsigned id = 12; id <= 60; ++id) source.downloaded.push_back(marker(id, false));
    // Panning reselects the nearest bounded set, including later catalog pages.
    for (int i = 0; i < 30; ++i) projection->update(source, 6, 102, 10);
    out = std::make_unique<ui::map::MapOverlaySnapshot>();
    projection->append(*out);
    assert(out->item_count == 32 && out->truncated);
    assert(std::strcmp(out->items[0].label.c_str(), "Cache 60") == 0);
    out = std::make_unique<ui::map::MapOverlaySnapshot>();
    out->item_count = 31;
    projection->append(*out);
    assert(out->item_count == 32 && out->truncated);

    // Unknown coordinates must not manufacture a marker at (0, 0), while an
    // explicitly chosen (0, 0) remains valid.
    source.drafts = {marker(1, true, false), marker(2, true)};
    source.drafts[1].latitude_e7 = source.drafts[1].longitude_e7 = 0;
    source.downloaded.clear();
    projection = std::make_unique<LocalMapOverlay>();
    source.busy = true;
    projection->update(source, 0, 0, 10);
    source.busy = false;
    for (int i = 0; i < 4; ++i) projection->update(source, 0, 0, 10);
    out = std::make_unique<ui::map::MapOverlaySnapshot>();
    projection->append(*out);
    assert(out->item_count == 1 && out->items[0].point.valid && out->items[0].point.lon == 0);
}
