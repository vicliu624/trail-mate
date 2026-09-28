#pragma once
#include "geocaching/usecase/query_client.h"

namespace platform::esp::arduino_common::geocaching
{
// Serialized browse-session boundary. Read-only page methods are UI-safe and
// must never perform SD I/O. Views borrow only the currently published page;
// implementations may keep disposable browsing entirely in memory.
class QueryBrowsePort : public ::geocaching::QueryClientPort
{
  public:
    virtual uint64_t generation() const = 0;
    virtual bool pageSource(::geocaching::Destination&) const = 0;
    virtual bool page(::geocaching::protocol::QueryPageView&) const = 0;
    virtual bool summary(size_t, ::geocaching::protocol::SummaryView&) const = 0;
    // Called from authenticated delivery, without I/O. A browse response may
    // be acknowledged once validated and retained for the current session.
    virtual bool accepted(const ::geocaching::Destination&, const ::geocaching::RequestId&, ::geocaching::ByteView) const = 0;
    virtual bool maintenancePending() const { return false; }
    virtual void maintenanceStep() {}
};
} // namespace platform::esp::arduino_common::geocaching
