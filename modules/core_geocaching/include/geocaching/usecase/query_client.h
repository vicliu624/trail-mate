#pragma once
#include "geocaching/protocol/capabilities.h"
#include "geocaching/protocol/query_request.h"
#include "geocaching/protocol/query_response.h"
#include "geocaching/usecase/directory_pool.h"

namespace geocaching
{
enum class QueryPersistence : uint8_t
{
    Rejected,
    Pending,
    Committed
};

class QueryClientPort
{
  public:
    virtual ~QueryClientPort() = default;
    virtual bool newRequestId(RequestId& out) = 0;
    // Persist exact request and destination before queueing transport. Bytes
    // borrow the client buffer, kept immutable until Pending persistence ends.
    // The port may queue that lease before acquiring a shared I/O workspace.
    // Only Committed permits transport. pollPersistence advances one bounded step.
    virtual QueryPersistence submit(const DirectoryEntry& directory, const RequestId& id, ByteView request) = 0;
    virtual QueryPersistence commitCapabilities(const Destination& source, const RequestId& id,
                                                ByteView response) = 0;
    // Persist the exact response before reporting Committed. Summary views can
    // be rebuilt from that response; all spans are borrowed during this call.
    virtual QueryPersistence commitPage(const Destination& source, const RequestId& id, ByteView response,
                                        const protocol::QueryPageView& page) = 0;
    virtual QueryPersistence pollPersistence() = 0;
    virtual QueryPersistence cancel(const Destination& source, const RequestId& id) = 0;
};

enum class QueryClientPhase : std::uint8_t
{
    Idle,
    FindingDirectory,
    CheckingCapabilities,
    Querying,
    PageReady,
    PersistingRequest,
    PersistingCapabilities,
    PersistingPage,
    Cancelling,
    Failed
};
enum class QueryFailure : uint8_t
{
    None,
    Timeout,
    Storage,
    Cancelled
};

// Long-lived owner: allocate off task stack. One outstanding request at a time.
class QueryClient
{
  public:
    explicit QueryClient(QueryClientPort& port) : port_(port), directories_(directory_entries_.data(), directory_entries_.size()) {}
    static constexpr uint64_t kReplyTimeoutMs = 120000;
    QueryClient(const QueryClient&) = delete;
    QueryClient& operator=(const QueryClient&) = delete;
    bool observe(const Destination& discovery, const Destination& derived_delivery,
                 ByteView public_key, ByteView app_data, std::uint64_t now)
    {
        protocol::DirectoryAnnouncement announcement;
        return protocol::decodeDirectoryAnnouncement(app_data, derived_delivery, announcement) &&
               directories_.observe(discovery, announcement, public_key, now);
    }
    bool query(const protocol::QueryRegion& region)
    {
        if (phase_ != QueryClientPhase::Idle && phase_ != QueryClientPhase::PageReady && phase_ != QueryClientPhase::Failed) return false;
        std::size_t size = 0;
        if (!protocol::encodeQueryRequest(request_, region, 3, {}, 20, {}, 2048,
                                          request_bytes_.data(), request_bytes_.size(), size)) return false;
        region_ = region;
        cursor_size_ = 0;
        has_snapshot_ = false;
        has_last_id_ = false;
        failure_ = QueryFailure::None;
        timer_armed_ = false;
        phase_ = QueryClientPhase::FindingDirectory;
        return true;
    }
    bool tick(std::uint64_t now)
    {
        if (phase_ == QueryClientPhase::PersistingRequest || phase_ == QueryClientPhase::PersistingCapabilities ||
            phase_ == QueryClientPhase::PersistingPage || phase_ == QueryClientPhase::Cancelling)
        {
            const auto result = port_.pollPersistence();
            if (result == QueryPersistence::Pending) return false;
            if (result == QueryPersistence::Rejected)
            {
                if (selected_) selected_->in_flight = false;
                phase_ = QueryClientPhase::Failed;
                failure_ = QueryFailure::Storage;
                return false;
            }
            timer_armed_ = false;
            if (phase_ == QueryClientPhase::Cancelling)
            {
                selected_->in_flight = false;
                phase_ = QueryClientPhase::Failed;
            }
            else if (phase_ == QueryClientPhase::PersistingRequest) phase_ = after_persist_;
            else if (phase_ == QueryClientPhase::PersistingCapabilities) finishCapabilities();
            else
            {
                selected_->in_flight = false;
                phase_ = QueryClientPhase::PageReady;
            }
            return true;
        }
        if (phase_ == QueryClientPhase::FindingDirectory || phase_ == QueryClientPhase::CheckingCapabilities ||
            phase_ == QueryClientPhase::Querying)
        {
            if (!timer_armed_ || now < waiting_since_)
            {
                waiting_since_ = now;
                timer_armed_ = true;
            }
            if (now - waiting_since_ >= kReplyTimeoutMs)
            {
                if (selected_) selected_->retry_after = now > UINT64_MAX - 5000 ? UINT64_MAX : now + 5000;
                return cancel(QueryFailure::Timeout);
            }
        }
        if (phase_ != QueryClientPhase::FindingDirectory) return false;
        selected_ = directories_.next(now, true);
        const bool ready = selected_ != nullptr;
        if (!selected_) selected_ = directories_.next(now, false);
        if (!selected_ || !port_.newRequestId(request_)) return false;
        std::size_t size = 0;
        page_limit_ = ready ? (selected_->max_query_items < 20 ? selected_->max_query_items : 20) : 20;
        const bool encoded = ready
                                 ? protocol::encodeQueryRequest(request_, region_, 3, {}, page_limit_, {}, 2048, request_bytes_.data(), request_bytes_.size(), size)
                                 : protocol::encodeCapabilitiesRequest(request_, request_bytes_.data(), request_bytes_.size(), size);
        if (!encoded) return false;
        return submit(size, ready ? QueryClientPhase::Querying : QueryClientPhase::CheckingCapabilities);
    }
    // Caller must authenticate the LXMF source and match local destination.
    bool loadMore()
    {
        if (phase_ != QueryClientPhase::PageReady || !selected_ || cursor_size_ == 0) return false;
        if (!port_.newRequestId(request_)) return false;
        std::size_t size = 0;
        if (!protocol::encodeQueryRequest(request_, region_, 3, {}, page_limit_, {cursor_.data(), cursor_size_},
                                          2048, request_bytes_.data(), request_bytes_.size(), size)) return false;
        return submit(size, QueryClientPhase::Querying);
    }
    bool hasMore() const { return phase_ == QueryClientPhase::PageReady && cursor_size_ != 0; }

    bool accept(const Destination& source, ByteView response)
    {
        if (!selected_ || source.bytes != selected_->delivery.bytes) return false;
        if (phase_ == QueryClientPhase::CheckingCapabilities)
        {
            protocol::DirectoryCapabilities capabilities;
            if (!protocol::decodeDirectoryCapabilities(response, request_, capabilities)) return false;
            const auto result = port_.commitCapabilities(source, request_, response);
            if (result == QueryPersistence::Rejected) return false;
            pending_max_items_ = capabilities.max_query_items;
            if (result == QueryPersistence::Pending)
            {
                phase_ = QueryClientPhase::PersistingCapabilities;
                return false;
            }
            finishCapabilities();
            return true;
        }
        if (phase_ != QueryClientPhase::Querying) return false;
        protocol::QueryPageView page;
        if (!protocol::decodeQueryPage(response, request_, 2048, page_limit_, page)) return false;
        if (has_snapshot_ && std::memcmp(snapshot_.data(), page.snapshot_id.data, 16) != 0) return false;
        if (cursor_size_ && page.next_cursor.size == cursor_size_ &&
            std::memcmp(cursor_.data(), page.next_cursor.data, cursor_size_) == 0) return false;
        protocol::CmpReader rows(page.encoded_items);
        protocol::SummaryView point;
        for (std::size_t i = 0; i < page.count; ++i)
        {
            if (!protocol::decodeSummary(rows, point) || (i == 0 && has_last_id_ && !(last_id_.bytes < point.id.bytes))) return false;
            if (point.state == CacheState::Archived || point.latitude_e7 < region_.south_e7 ||
                point.latitude_e7 > region_.north_e7 || point.longitude_e7 < region_.west_e7 ||
                point.longitude_e7 > region_.east_e7) return false;
        }
        const auto result = port_.commitPage(source, request_, response, page);
        if (result == QueryPersistence::Rejected) return false;
        std::memcpy(snapshot_.data(), page.snapshot_id.data, 16);
        has_snapshot_ = true;
        if (page.count)
        {
            last_id_ = point.id;
            has_last_id_ = true;
        }
        cursor_size_ = page.next_cursor.size;
        if (cursor_size_) std::memcpy(cursor_.data(), page.next_cursor.data, cursor_size_);
        if (result == QueryPersistence::Pending)
        {
            phase_ = QueryClientPhase::PersistingPage;
            return false;
        }
        selected_->in_flight = false;
        phase_ = QueryClientPhase::PageReady;
        return true;
    }
    QueryClientPhase phase() const { return phase_; }
    QueryFailure failure() const { return failure_; }
    bool persistencePending() const
    {
        return phase_ == QueryClientPhase::PersistingRequest || phase_ == QueryClientPhase::PersistingCapabilities ||
               phase_ == QueryClientPhase::PersistingPage || phase_ == QueryClientPhase::Cancelling;
    }
    // Retry after the current commit finishes. The runtime never sends during
    // cancellation; Failed is exposed only after the stop is durably committed.
    bool cancel(QueryFailure reason = QueryFailure::Cancelled)
    {
        if (persistencePending()) return false;
        if (!selected_ || !selected_->in_flight)
        {
            failure_ = reason;
            phase_ = QueryClientPhase::Failed;
            return true;
        }
        const auto result = port_.cancel(selected_->delivery, request_);
        if (result == QueryPersistence::Rejected) return false;
        failure_ = reason;
        phase_ = result == QueryPersistence::Pending ? QueryClientPhase::Cancelling : QueryClientPhase::Failed;
        if (result == QueryPersistence::Committed) selected_->in_flight = false;
        return true;
    }

  private:
    bool submit(size_t size, QueryClientPhase next)
    {
        const auto result = port_.submit(*selected_, request_, {request_bytes_.data(), size});
        if (result == QueryPersistence::Rejected) return false;
        selected_->in_flight = true;
        after_persist_ = next;
        phase_ = result == QueryPersistence::Pending ? QueryClientPhase::PersistingRequest : next;
        timer_armed_ = false;
        return true;
    }
    void finishCapabilities()
    {
        selected_->capabilities_verified = true;
        selected_->max_query_items = pending_max_items_;
        selected_->in_flight = false;
        phase_ = QueryClientPhase::FindingDirectory;
        timer_armed_ = false;
    }
    QueryClientPhase after_persist_ = QueryClientPhase::Idle;
    uint8_t pending_max_items_ = 0;
    QueryClientPort& port_;
    std::array<DirectoryEntry, 4> directory_entries_{};
    DirectoryPool directories_;
    DirectoryEntry* selected_ = nullptr;
    protocol::QueryRegion region_;
    RequestId request_;
    std::array<std::uint8_t, 64> cursor_{};
    std::array<std::uint8_t, 16> snapshot_{};
    GeocacheId last_id_;
    std::size_t cursor_size_ = 0;
    std::uint8_t page_limit_ = 20;
    bool has_snapshot_ = false;
    bool has_last_id_ = false;
    std::array<std::uint8_t, 256> request_bytes_{};
    QueryClientPhase phase_ = QueryClientPhase::Idle;
    QueryFailure failure_ = QueryFailure::None;
    uint64_t waiting_since_ = 0;
    bool timer_armed_ = false;
};
} // namespace geocaching
