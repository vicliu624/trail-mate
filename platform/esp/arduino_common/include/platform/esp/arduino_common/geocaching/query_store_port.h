#pragma once
#include "platform/esp/arduino_common/geocaching/query_browse_port.h"
#include "platform/esp/arduino_common/geocaching/sd_request_store.h"

namespace platform::esp::arduino_common::geocaching
{
// The browse owner serializes this port, the dispatcher and UI projections.
// Only exact committed responses are visible; all returned views borrow state.
class QueryStorePort final : public QueryBrowsePort
{
  public:
    using RandomId = bool (*)(void*, uint8_t out[16]);
    using Now = ::geocaching::storage::StoredTime (*)(void*);
    QueryStorePort(SdRequestStore& store, ::geocaching::storage::LogicalState& state,
                   const ::geocaching::Destination& local, RandomId random, Now now, void* context)
        : store_(store), state_(state), local_(local), random_(random), now_(now), context_(context) {}

    bool newRequestId(::geocaching::RequestId& out) override
    {
        return random_ && random_(context_, out.bytes.data());
    }

    ::geocaching::QueryPersistence submit(const ::geocaching::DirectoryEntry& directory,
                                          const ::geocaching::RequestId& id, ::geocaching::ByteView request) override
    {
        if (pending_ != Pending::None || !random_ || !now_) return Result::Rejected;
        std::array<uint8_t, 16> task;
        if (!random_(context_, task.data())) return Result::Rejected;
        const auto result = store_.persistNewTask(local_, directory.delivery, id, task, 3, request, now_(context_));
        return started(result, Pending::Request, directory.delivery, id);
    }
    ::geocaching::QueryPersistence commitCapabilities(const ::geocaching::Destination& source,
                                                      const ::geocaching::RequestId& id, ::geocaching::ByteView response) override
    {
        if (pending_ != Pending::None) return Result::Rejected;
        return started(store_.commitDirectoryCapabilities(local_, source, id, response), Pending::Capabilities, source, id);
    }
    ::geocaching::QueryPersistence commitPage(const ::geocaching::Destination& source,
                                              const ::geocaching::RequestId& id, ::geocaching::ByteView response,
                                              const ::geocaching::protocol::QueryPageView&) override
    {
        if (pending_ != Pending::None) return Result::Rejected;
        return started(store_.commitQueryResult(local_, source, id, response), Pending::Page, source, id);
    }
    ::geocaching::QueryPersistence pollPersistence() override
    {
        if (pending_ == Pending::None) return Result::Rejected;
        const auto result = store_.stepCommit();
        if (result == JournalWriteResult::InProgress) return Result::Pending;
        if (result == JournalWriteResult::Verified && pending_ == Pending::Page) publishPage();
        pending_ = Pending::None;
        return result == JournalWriteResult::Verified ? Result::Committed : Result::Rejected;
    }
    uint64_t generation() const override { return generation_; }
    bool pageSource(::geocaching::Destination& out) const override
    {
        if (!generation_) return false;
        std::memcpy(out.bytes.data(), page_key_.data() + 16, 16);
        return true;
    }
    ::geocaching::QueryPersistence cancel(const ::geocaching::Destination& source, const ::geocaching::RequestId& id) override
    {
        if (pending_ != Pending::None) return Result::Rejected;
        std::array<uint8_t, 48> key;
        makeKey(source, id, key);
        ::geocaching::ByteView value;
        ::geocaching::storage::OutgoingView outgoing;
        if (!state_.view().find(5, {key.data(), key.size()}, value) ||
            !::geocaching::storage::decodeOutgoing({key.data(), key.size()}, value, outgoing)) return Result::Rejected;
        std::array<uint8_t, 16> task;
        std::memcpy(task.data(), outgoing.task_id.data, task.size());
        return started(store_.stopTask(task), Pending::Cancellation, source, id);
    }
    bool page(::geocaching::protocol::QueryPageView& out) const override
    {
        out = {};
        if (!generation_) return false;
        ::geocaching::ByteView bytes;
        ::geocaching::storage::OutgoingView outgoing;
        ::geocaching::RequestId id;
        std::memcpy(id.bytes.data(), page_key_.data() + 32, 16);
        return state_.view().find(5, {page_key_.data(), page_key_.size()}, bytes) &&
               ::geocaching::storage::decodeOutgoing({page_key_.data(), page_key_.size()}, bytes, outgoing) && outgoing.state == 4 &&
               ::geocaching::protocol::decodeQueryPage(outgoing.terminal_data, id, 2048, 20, out);
    }
    bool summary(size_t index, ::geocaching::protocol::SummaryView& out) const override
    {
        out = {};
        ::geocaching::protocol::QueryPageView current;
        if (!page(current) || index >= current.count) return false;
        ::geocaching::protocol::CmpReader rows(current.encoded_items);
        for (size_t i = 0; i <= index; ++i)
            if (!::geocaching::protocol::decodeSummary(rows, out)) return false;
        return true;
    }
    // The transport may retry while the first callback was still persisting.
    // A duplicate is acknowledged only after exact durable response matching.
    bool accepted(const ::geocaching::Destination& source, const ::geocaching::RequestId& id,
                  ::geocaching::ByteView response) const override
    {
        std::array<uint8_t, 48> key;
        makeKey(source, id, key);
        ::geocaching::ByteView bytes;
        ::geocaching::storage::OutgoingView outgoing;
        if (!response.data || !state_.view().find(5, {key.data(), key.size()}, bytes) ||
            !::geocaching::storage::decodeOutgoing({key.data(), key.size()}, bytes, outgoing)) return false;
        if (outgoing.state == 4)
            return outgoing.terminal_data.size == response.size && !std::memcmp(outgoing.terminal_data.data, response.data, response.size);
        ::geocaching::storage::TaskView task;
        // A durable stop discards late replies without reviving the query.
        return state_.view().find(10, outgoing.task_id, bytes) &&
               ::geocaching::storage::decodeTask(outgoing.task_id, bytes, task) && task.state == 5 && !task.continue_intent;
    }

  private:
    using Result = ::geocaching::QueryPersistence;
    enum class Pending : uint8_t
    {
        None,
        Request,
        Capabilities,
        Page,
        Cancellation
    };
    void makeKey(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id,
                 std::array<uint8_t, 48>& key) const
    {
        std::memcpy(key.data(), local_.bytes.data(), 16);
        std::memcpy(key.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key.data() + 32, id.bytes.data(), 16);
    }
    Result started(JournalWriteResult result, Pending operation, const ::geocaching::Destination& remote,
                   const ::geocaching::RequestId& id)
    {
        if (result != JournalWriteResult::Verified && result != JournalWriteResult::InProgress) return Result::Rejected;
        makeKey(remote, id, pending_key_);
        if (result == JournalWriteResult::Verified)
        {
            if (operation == Pending::Page) publishPage();
            return Result::Committed;
        }
        pending_ = operation;
        return Result::Pending;
    }
    void publishPage()
    {
        if (!generation_ || page_key_ != pending_key_)
        {
            page_key_ = pending_key_;
            ++generation_;
        }
    }
    SdRequestStore& store_;
    ::geocaching::storage::LogicalState& state_;
    ::geocaching::Destination local_;
    RandomId random_;
    Now now_;
    void* context_;
    std::array<uint8_t, 48> pending_key_{}, page_key_{};
    uint64_t generation_ = 0;
    Pending pending_ = Pending::None;
};
static_assert(sizeof(QueryStorePort) <= 192, "Query adapter must not keep a duplicate page payload");
} // namespace platform::esp::arduino_common::geocaching
