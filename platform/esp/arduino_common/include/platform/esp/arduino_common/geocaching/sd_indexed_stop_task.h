#pragma once
#include "geocaching/storage/task_record.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
// A task stop governs all child requests. Only small task encoding is retained;
// request payloads remain in the caller's reusable read frame.
class SdIndexedStopTask
{
  public:
    explicit SdIndexedStopTask(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy,
               ::geocaching::ByteView key, bool by_request, uint8_t* frame, size_t capacity,
               ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !key.data || key.size != (by_request ? 48 : 16)) return false;
        const auto a = reinterpret_cast<uintptr_t>(frame), b = reinterpret_cast<uintptr_t>(candidate.data());
        if (a <= b ? b - a < capacity : a - b < candidate.size()) return false;
        root_ = root;
        copy_ = copy;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        request_ = by_request;
        std::memcpy(key_.data(), key.data, key.size);
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, by_request ? 5 : 10, {key_.data(), key.size}, frame_, capacity_)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool committed(::geocaching::storage::IndexRootView& root) const
    {
        root = {};
        if (result_ != IndexedCommitStep::Verified) return false;
        if (duplicate_)
        {
            root = root_;
            return true;
        }
        return std::get<SdIndexedCommit>(io_).committed(root);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (committing_) return result_ = std::get<SdIndexedCommit>(io_).step();
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready)
            return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                   : IndexedCommitStep::Invalid);
        if (request_)
        {
            OutgoingView outgoing;
            if (!decodeOutgoing({key_.data(), 48}, read.value(), outgoing)) return fail(IndexedCommitStep::Invalid);
            std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 10, {task_id_.data(), task_id_.size()}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            request_ = false;
            from_request_ = true;
            return result_;
        }
        if (!from_request_) std::memcpy(task_id_.data(), key_.data(), task_id_.size());
        const ::geocaching::ByteView task_key{task_id_.data(), task_id_.size()};
        TaskView task;
        if (!decodeTask(task_key, read.value(), task)) return fail(IndexedCommitStep::Invalid);
        if (from_request_)
        {
            bool linked = false;
            for (size_t i = 0; i < task.request_count; ++i) linked |= !std::memcmp(task.requests[i].data, key_.data(), 48);
            if (!linked) return fail(IndexedCommitStep::Invalid);
        }
        if (task.state == 5 && !task.continue_intent)
        {
            duplicate_ = true;
            return fail(IndexedCommitStep::Verified);
        }
        if (task.state == 3) return fail(IndexedCommitStep::Invalid);
        task.state = 5;
        task.continue_intent = false;
        size_t size = 0;
        if (!encodeTask(task_key, task, encoded_.data(), encoded_.size(), size)) return fail(IndexedCommitStep::Invalid);
        mutation_ = {10, task_key, {encoded_.data(), size}, false};
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, &mutation_, 1, frame_, capacity_, *candidate_)) return fail(IndexedCommitStep::Invalid);
        committing_ = true;
        return result_;
    }

  private:
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        io_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    ::geocaching::storage::MutationView mutation_;
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 256> encoded_{};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    unsigned copy_ = 0;
    bool request_ = false, from_request_ = false, committing_ = false, duplicate_ = false;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> io_;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedStopTask) <= 1536, "Stopping a task must not own request payloads");
} // namespace platform::esp::arduino_common::geocaching
