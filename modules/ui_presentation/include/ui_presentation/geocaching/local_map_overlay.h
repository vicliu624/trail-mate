#pragma once
#include "ui_presentation/geocaching/geocaching_source.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui::geocaching
{
// Map-owned, bounded projection. Requests four metadata rows at a time; Source
// does all storage work asynchronously. Never activates directory discovery.
// Keep this object off the task stack, just like MapOverlaySnapshot.
class LocalMapOverlay
{
  public:
    void update(Source& source, double latitude, double longitude, uint8_t zoom)
    {
        const double span = std::ldexp(360.0, -std::min<unsigned>(zoom, 22));
        // Finish the current bounded scan before reselecting for a new viewport.
        // GPS movement and zoom updates must not cancel every pending page.
        if (!started_ || (finished_ && (zoom != zoom_ || std::abs(latitude - latitude_) > span / 4 ||
                                        std::abs(longitudeDelta(longitude, longitude_)) > span / 4)))
        {
            started_ = true;
            latitude_ = latitude;
            longitude_ = longitude;
            zoom_ = zoom;
            section_ = Section::Downloaded;
            offset_ = count_ = 0;
            finished_ = truncated_ = false;
        }
        if (finished_) return;
        source.requestWindow(section_, offset_, 4);
        Snapshot snapshot;
        source.snapshot(section_, snapshot);
        if (snapshot.busy || !snapshot.ready) return;
        const auto end = std::min(snapshot.count, offset_ + 4);
        for (; offset_ < end; ++offset_)
        {
            Item item;
            if (!source.item(section_, offset_, snapshot.generation, item)) return;
            if ((!item.downloaded && !item.has_coordinates) || item.latitude_e7 < -900000000 ||
                item.latitude_e7 > 900000000 || item.longitude_e7 < -1800000000 || item.longitude_e7 >= 1800000000) continue;
            retain(item);
        }
        if (offset_ < snapshot.count) return;
        if (section_ == Section::Downloaded)
        {
            section_ = Section::Published;
            offset_ = 0;
        }
        else finished_ = true;
    }

    void append(map::MapOverlaySnapshot& out) const
    {
        if (count_)
        {
            out.header.valid = true;
            out.header.version = 1;
        }
        out.truncated = out.truncated || truncated_;
        for (size_t i = 0; i < count_; ++i)
        {
            if (out.item_count == map::MapOverlaySnapshot::kMaxItems)
            {
                out.truncated = true;
                break;
            }
            out.items[out.item_count++] = entries_[i].marker;
        }
    }

  private:
    struct Entry
    {
        std::array<uint8_t, 32> id{};
        bool draft = false;
        double distance = 0;
        map::MapOverlayItem marker;
    };
    static double longitudeDelta(double a, double b)
    {
        return std::remainder(a - b, 360.0);
    }
    void retain(const Item& item)
    {
        for (size_t i = 0; i < count_; ++i)
            if (entries_[i].id == item.id && entries_[i].draft == item.is_draft) return;
        const double lat = item.latitude_e7 / 10000000.0, lon = item.longitude_e7 / 10000000.0;
        const double dy = lat - latitude_;
        const double dx = longitudeDelta(lon, longitude_) * std::cos(latitude_ * 0.017453292519943295);
        const double distance = dx * dx + dy * dy;
        size_t at = 0;
        while (at < count_ && entries_[at].distance <= distance) ++at;
        if (count_ == entries_.size()) truncated_ = true;
        if (at == entries_.size()) return;
        if (count_ < entries_.size()) ++count_;
        for (size_t i = count_ - 1; i > at; --i) entries_[i] = entries_[i - 1];
        auto& entry = entries_[at];
        entry = {};
        entry.id = item.id;
        entry.draft = item.is_draft;
        entry.distance = distance;
        auto& marker = entry.marker;
        marker.kind = map::MapOverlayKind::Geocache;
        marker.style = item.is_draft && !item.publication_confirmed ? map::MapOverlayStyle::Warning : map::MapOverlayStyle::Default;
        marker.point = {lat, lon, true};
        marker.stable_id = UINT32_MAX; // Not a row index in the Geocaching page.
        ui::copyText(marker.label, item.name.data());
        ui::copyText(marker.detail, item.is_draft ? item.publication_confirmed ? "Published cache" : "Local cache - publication unconfirmed" : "Downloaded cache");
    }
    std::array<Entry, map::MapOverlaySnapshot::kMaxItems> entries_{};
    size_t offset_ = 0, count_ = 0;
    double latitude_ = 0, longitude_ = 0;
    uint8_t zoom_ = 0;
    Section section_ = Section::Published;
    bool started_ = false, finished_ = false, truncated_ = false;
};
} // namespace ui::geocaching
