#pragma once
#include "platform/esp/arduino_common/geocaching/query_browse_port.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
// UI projection of the same browse session driven by the maintenance owner.
// The runtime supplies its lock: UI reads never access SD or the router.
class QueryBrowseSource final : public ::ui::geocaching::Source
{
  public:
    using Lock = bool (*)(void*);
    using Unlock = void (*)(void*);
    QueryBrowseSource(::geocaching::QueryClient& client, QueryBrowsePort& store,
                      const ::geocaching::protocol::QueryRegion& region,
                      Lock lock = nullptr, Unlock unlock = nullptr, void* context = nullptr)
        : client_(client), store_(store), region_(region), lock_(lock), unlock_(unlock), context_(context) {}
    void snapshot(::ui::geocaching::Section section, ::ui::geocaching::Snapshot& out) override
    {
        out = {};
        Guard guard(*this);
        if (!guard.locked)
        {
            std::snprintf(out.status.data(), out.status.size(), "Updating...");
            return;
        }
        out.generation = generation();
        if (section != ::ui::geocaching::Section::Discover)
        {
            std::snprintf(out.status.data(), out.status.size(), "No cached items");
            return;
        }
        ::geocaching::protocol::QueryPageView page;
        if (store_.page(page)) out.count = page.count;
        const auto phase = client_.phase();
        out.can_refresh = phase == ::geocaching::QueryClientPhase::Idle || phase == ::geocaching::QueryClientPhase::PageReady ||
                          phase == ::geocaching::QueryClientPhase::Failed;
        out.has_more = client_.hasMore();
        std::snprintf(out.status.data(), out.status.size(), "%s", status(phase));
    }
    bool item(::ui::geocaching::Section section, size_t index, uint64_t expected, ::ui::geocaching::Item& out) override
    {
        out = {};
        Guard guard(*this);
        if (!guard.locked || section != ::ui::geocaching::Section::Discover || expected != generation()) return false;
        ::geocaching::protocol::SummaryView row;
        if (!store_.summary(index, row)) return false;
        out.id = row.id.bytes;
        out.revision_hash = row.hash.bytes;
        out.latitude_e7 = row.latitude_e7;
        out.longitude_e7 = row.longitude_e7;
        std::memcpy(out.name.data(), row.name.data(), row.name.size());
        const auto lat = row.latitude_e7 < 0 ? -int64_t(row.latitude_e7) : int64_t(row.latitude_e7);
        const auto lon = row.longitude_e7 < 0 ? -int64_t(row.longitude_e7) : int64_t(row.longitude_e7);
        std::snprintf(out.detail.data(), out.detail.size(),
                      "%s%ld.%07ld, %s%ld.%07ld\nDifficulty %u.%u / Terrain %u.%u\nDirectory preview - not yet downloaded",
                      row.latitude_e7 < 0 ? "-" : "", long(lat / 10000000), long(lat % 10000000),
                      row.longitude_e7 < 0 ? "-" : "", long(lon / 10000000), long(lon % 10000000),
                      unsigned(row.difficulty_x2 / 2), unsigned((row.difficulty_x2 % 2) * 5),
                      unsigned(row.terrain_x2 / 2), unsigned((row.terrain_x2 % 2) * 5));
        return true;
    }
    void refresh(::ui::geocaching::Section section) override
    {
        Guard guard(*this);
        if (guard.locked && section == ::ui::geocaching::Section::Discover) client_.query(region_);
    }
    bool loadMore() override
    {
        Guard guard(*this);
        return guard.locked && client_.loadMore();
    }
    void open(const ::ui::geocaching::Item&, uint64_t) override {}

  private:
    struct Guard
    {
        explicit Guard(QueryBrowseSource& source) : source(source), locked(!source.lock_ || source.lock_(source.context_)) {}
        ~Guard()
        {
            if (locked && source.unlock_) source.unlock_(source.context_);
        }
        QueryBrowseSource& source;
        bool locked;
    };
    uint64_t generation() const { return (store_.generation() << 8) | static_cast<uint8_t>(client_.phase()) | (static_cast<uint8_t>(client_.failure()) << 4); }
    const char* status(::geocaching::QueryClientPhase phase) const
    {
        using Phase = ::geocaching::QueryClientPhase;
        switch (phase)
        {
        case Phase::Idle:
            return "Refresh to discover shared caches";
        case Phase::FindingDirectory:
            return "Finding a public directory...";
        case Phase::CheckingCapabilities:
            return "Checking directory capabilities...";
        case Phase::Querying:
            return "Querying shared caches...";
        case Phase::PageReady:
            return "Shared caches - directory preview";
        case Phase::Failed:
            if (client_.failure() == ::geocaching::QueryFailure::Timeout) return "Directory did not reply - Refresh to retry";
            if (client_.failure() == ::geocaching::QueryFailure::Cancelled) return "Query cancelled";
            return "Storage failed - recovery required";
        case Phase::Cancelling:
            return "Stopping query...";
        default:
            return "Saving query progress...";
        }
    }
    ::geocaching::QueryClient& client_;
    QueryBrowsePort& store_;
    ::geocaching::protocol::QueryRegion region_;
    Lock lock_;
    Unlock unlock_;
    void* context_;
};
} // namespace platform::esp::arduino_common::geocaching
