#pragma once
#include "geocaching/storage/cache_head.h"
#include "geocaching/storage/task_record.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
// One pinned root/read-frame lease. Task/head encodings are small metadata;
// the request/response payload remains in the caller frame and is never copied.
class SdIndexedDownloadContext
{
  public:
    explicit SdIndexedDownloadContext(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, ::geocaching::ByteView key, uint64_t generation,
               uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexGetStep::Idle || !key.data || key.size != key_.size() || !generation) return false;
        std::memcpy(key_.data(), key.data, key_.size());
        root_ = root;
        generation_ = generation;
        frame_ = frame;
        capacity_ = capacity;
        if (!get_.emplace(volume_).begin(root_, 5, {key_.data(), key_.size()}, frame_, capacity_)) return false;
        result_ = IndexGetStep::Working;
        return true;
    }
    bool view(::geocaching::storage::OutgoingView& outgoing, ::geocaching::storage::TaskView& task,
              ::geocaching::storage::CacheHeadView& head) const
    {
        using namespace ::geocaching::storage;
        outgoing = {};
        task = {};
        head = {};
        return result_ == IndexGetStep::Ready &&
               decodeOutgoing({key_.data(), key_.size()}, get_->value(), outgoing) &&
               decodeTask({task_id_.data(), task_id_.size()}, {task_.data(), task_size_}, task) &&
               decodeCacheHead(task.cache_id, {head_.data(), head_size_}, head);
    }
    bool intentActive() const
    {
        ::geocaching::storage::OutgoingView outgoing;
        ::geocaching::storage::TaskView task;
        ::geocaching::storage::CacheHeadView head;
        return view(outgoing, task, head) && outgoing.continue_intent && task.continue_intent && task.state < 3;
    }
    IndexGetStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexGetStep::Working) return result_;
        const auto status = get_->step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready) return fail(status == IndexGetStep::NotFound ? IndexGetStep::Invalid : status);
        if (phase_ == Phase::Outgoing || phase_ == Phase::Reload)
        {
            OutgoingView outgoing;
            if (!decodeOutgoing({key_.data(), key_.size()}, get_->value(), outgoing) || outgoing.install_generation != generation_)
                return fail(IndexGetStep::Invalid);
            if (phase_ == Phase::Reload)
            {
                if (std::memcmp(outgoing.task_id.data, task_id_.data(), task_id_.size())) return fail(IndexGetStep::Invalid);
                return result_ = IndexGetStep::Ready;
            }
            std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
            if (!get_.emplace(volume_).begin(root_, 10, {task_id_.data(), task_id_.size()}, frame_, capacity_)) return fail(IndexGetStep::Invalid);
            phase_ = Phase::Task;
            return result_;
        }
        TaskView task;
        if (phase_ == Phase::Task)
        {
            if (!decodeTask({task_id_.data(), task_id_.size()}, get_->value(), task) || task.kind != 2 ||
                task.cache_id.size != 32 || task.revision_hash.size != 32 || get_->value().size > task_.size()) return fail(IndexGetStep::Invalid);
            bool linked = false;
            for (size_t i = 0; i < task.request_count; ++i) linked |= !std::memcmp(task.requests[i].data, key_.data(), key_.size());
            if (!linked) return fail(IndexGetStep::Invalid);
            task_size_ = get_->value().size;
            std::memcpy(task_.data(), get_->value().data, task_size_);
            if (!decodeTask({task_id_.data(), task_id_.size()}, {task_.data(), task_size_}, task) ||
                !get_.emplace(volume_).begin(root_, 2, task.cache_id, frame_, capacity_)) return fail(IndexGetStep::Invalid);
            phase_ = Phase::Head;
            return result_;
        }
        CacheHeadView head;
        if (!decodeTask({task_id_.data(), task_id_.size()}, {task_.data(), task_size_}, task) ||
            !decodeCacheHead(task.cache_id, get_->value(), head) || head.install_generation != generation_ || get_->value().size > head_.size())
            return fail(IndexGetStep::Invalid);
        head_size_ = get_->value().size;
        std::memcpy(head_.data(), get_->value().data, head_size_);
        if (!get_.emplace(volume_).begin(root_, 5, {key_.data(), key_.size()}, frame_, capacity_)) return fail(IndexGetStep::Invalid);
        phase_ = Phase::Reload;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Outgoing,
        Task,
        Head,
        Reload
    };
    IndexGetStep fail(IndexGetStep status)
    {
        get_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 256> task_{};
    std::array<uint8_t, 64> head_{};
    std::optional<SdIndexGet> get_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, task_size_ = 0, head_size_ = 0;
    uint64_t generation_ = 0;
    Phase phase_ = Phase::Outgoing;
    IndexGetStep result_ = IndexGetStep::Idle;
};
static_assert(sizeof(SdIndexedDownloadContext) <= 1152, "Download context retains metadata, not SignedCache bytes");
} // namespace platform::esp::arduino_common::geocaching
