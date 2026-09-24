#pragma once
#include "geocaching/storage/pending_request.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexedPendingStep : uint8_t
{
    Idle,
    Working,
    Ready,
    None,
    Invalid,
    IoError,
    VolumeChanged,
    WorkspaceTooSmall
};

// Snapshot root remains pinned. A selected request borrows the caller frame
// until this operation is released; the dispatcher reloads it after committing
// a send attempt. One key is retained, not an array of pending requests.
class SdIndexedPendingRequest
{
  public:
    explicit SdIndexedPendingRequest(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume), scan_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, const ::geocaching::Destination& local,
               ::geocaching::ByteView after, uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexedPendingStep::Idle || (after.size && (!after.data || after.size != 48))) return false;
        if (!scan_.begin(root, 5, frame, capacity)) return false;
        root_ = root;
        local_ = local;
        if (after.size) std::memcpy(after_.data(), after.data, after_.size());
        has_after_ = after.size != 0;
        frame_ = frame;
        capacity_ = capacity;
        result_ = IndexedPendingStep::Working;
        return true;
    }
    bool selected(::geocaching::storage::PendingRequestView& out) const
    {
        out = {};
        if (result_ != IndexedPendingStep::Ready) return false;
        ::geocaching::storage::OutgoingView row;
        if (!::geocaching::storage::decodeOutgoing({best_.data(), best_.size()}, get_->value(), row)) return false;
        out.key = best_;
        std::memcpy(out.destination.bytes.data(), best_.data() + 16, 16);
        std::memcpy(out.request_id.bytes.data(), best_.data() + 32, 16);
        out.request = row.request;
        out.task_id = row.task_id;
        return true;
    }
    IndexedPendingStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedPendingStep::Working) return result_;
        if (phase_ == Phase::Scan)
        {
            const auto status = scan_.step();
            if (status == IndexScanStep::Working) return result_;
            if (status == IndexScanStep::End)
            {
                if (!found_) return finish(IndexedPendingStep::None);
                if (!get_.emplace(volume_).begin(root_, 5, {best_.data(), best_.size()}, frame_, capacity_)) return finish(IndexedPendingStep::Invalid);
                phase_ = Phase::Selected;
                return result_;
            }
            if (status != IndexScanStep::Item) return error(status);
            MutationView row;
            OutgoingView outgoing;
            if (!scan_.item(row) || !decodeOutgoing(row.key, row.value, outgoing)) return finish(IndexedPendingStep::Invalid);
            const bool candidate = outgoing.continue_intent && (outgoing.state == 0 || outgoing.state == 3) &&
                                   !std::memcmp(row.key.data, local_.bytes.data(), 16) &&
                                   (!has_after_ || std::memcmp(row.key.data, after_.data(), 48) > 0) &&
                                   (!found_ || std::memcmp(row.key.data, best_.data(), 48) < 0);
            if (candidate)
            {
                std::memcpy(key_.data(), row.key.data, key_.size());
                std::memcpy(task_.data(), outgoing.task_id.data, task_.size());
                generation_ = outgoing.install_generation;
                if (!get_.emplace(volume_).begin(root_, 10, {task_.data(), task_.size()}, frame_, capacity_)) return finish(IndexedPendingStep::Invalid);
                phase_ = Phase::Task;
            }
            if (!scan_.advance()) return finish(IndexedPendingStep::Invalid);
            return result_;
        }
        const auto status = get_->step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready) return error(status);
        if (phase_ == Phase::Selected) return result_ = IndexedPendingStep::Ready;
        if (phase_ == Phase::Task)
        {
            TaskView task;
            if (!decodeTask({task_.data(), task_.size()}, get_->value(), task)) return finish(IndexedPendingStep::Invalid);
            bool linked = false;
            for (size_t i = 0; i < task.request_count; ++i) linked |= !std::memcmp(task.requests[i].data, key_.data(), key_.size());
            if (!linked) return finish(IndexedPendingStep::Invalid);
            if (!task.continue_intent || task.state > 2) return continueScan(false);
            if ((task.kind == 2 || task.kind == 4) && generation_)
            {
                if (task.cache_id.size != cache_.size()) return finish(IndexedPendingStep::Invalid);
                std::memcpy(cache_.data(), task.cache_id.data, cache_.size());
                if (!get_.emplace(volume_).begin(root_, 2, {cache_.data(), cache_.size()}, frame_, capacity_)) return finish(IndexedPendingStep::Invalid);
                phase_ = Phase::Head;
                return result_;
            }
            return continueScan(true);
        }
        CacheHeadView head;
        if (!decodeCacheHead({cache_.data(), cache_.size()}, get_->value(), head)) return finish(IndexedPendingStep::Invalid);
        return continueScan(head.install_generation == generation_);
    }

  private:
    enum class Phase : uint8_t
    {
        Scan,
        Task,
        Head,
        Selected
    };
    IndexedPendingStep continueScan(bool eligible)
    {
        if (eligible)
        {
            best_ = key_;
            found_ = true;
        }
        get_.reset();
        phase_ = Phase::Scan;
        return result_;
    }
    template <class Status>
    IndexedPendingStep error(Status status)
    {
        return finish(status == Status::VolumeChanged ? IndexedPendingStep::VolumeChanged : status == Status::IoError         ? IndexedPendingStep::IoError
                                                                                        : status == Status::WorkspaceTooSmall ? IndexedPendingStep::WorkspaceTooSmall
                                                                                                                              : IndexedPendingStep::Invalid);
    }
    IndexedPendingStep finish(IndexedPendingStep status)
    {
        get_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::Destination local_;
    SdIndexScan scan_;
    std::optional<SdIndexGet> get_;
    std::array<uint8_t, 48> after_{}, key_{}, best_{};
    std::array<uint8_t, 16> task_{};
    std::array<uint8_t, 32> cache_{};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    uint64_t generation_ = 0;
    bool has_after_ = false, found_ = false;
    Phase phase_ = Phase::Scan;
    IndexedPendingStep result_ = IndexedPendingStep::Idle;
};
static_assert(sizeof(SdIndexedPendingRequest) <= 1792, "Pending selection retains keys, not request payloads");
} // namespace platform::esp::arduino_common::geocaching
