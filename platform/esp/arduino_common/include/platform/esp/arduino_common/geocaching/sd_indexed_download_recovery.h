#pragma once
#include "geocaching/storage/download_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
// One boot-selection job, allocated by the storage owner. The root and frame
// remain pinned until terminal status. The selected metadata owns its bytes,
// so this job and its lease can be released before starting GPX recovery.
// A stable request key orders candidates independently of mutable shard offsets.
class SdIndexedDownloadRecovery
{
  public:
    explicit SdIndexedDownloadRecovery(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, ::geocaching::ByteView after,
               uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexScanStep::Idle || (after.size && (!after.data || after.size != after_.size())) ||
            !scan_.emplace(volume_).begin(root, 5, frame, capacity)) return false;
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        has_after_ = after.size != 0;
        if (has_after_) std::memcpy(after_.data(), after.data, after_.size());
        result_ = IndexScanStep::Working;
        return true;
    }
    bool beginWaiting(const ::geocaching::storage::IndexRootView& root, const ::geocaching::Destination& local,
                      uint8_t* frame, size_t capacity)
    {
        if (!begin(root, {}, frame, capacity)) return false;
        waiting_ = true;
        local_ = local;
        return true;
    }
    bool preview(::geocaching::protocol::SummaryView& out) const
    {
        out = {};
        if (!waiting_ || result_ != IndexScanStep::Item) return false;
        out = preview_;
        return true;
    }
    bool selected(::geocaching::storage::DownloadRecoveryRequest& out) const
    {
        out = {};
        if (result_ != IndexScanStep::Item) return false;
        out = best_;
        return true;
    }
    IndexScanStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexScanStep::Working) return result_;
        if (phase_ == Phase::Scan || phase_ == Phase::Preview)
        {
            const auto status = scan_->step();
            if (status == IndexScanStep::Working) return result_;
            if (status == IndexScanStep::End)
            {
                if (phase_ == Phase::Preview) return finish(IndexScanStep::Invalid);
                if (!waiting_ || !found_) return result_ = found_ ? IndexScanStep::Item : IndexScanStep::End;
                if (!scan_.emplace(volume_).begin(root_, 5, frame_, capacity_)) return finish(IndexScanStep::Invalid);
                phase_ = Phase::Preview;
                return result_;
            }
            if (status != IndexScanStep::Item) return finish(status);
            MutationView row;
            OutgoingView outgoing;
            if (!scan_->item(row) || !decodeOutgoing(row.key, row.value, outgoing)) return finish(IndexScanStep::Invalid);
            if (phase_ == Phase::Preview)
            {
                if (findWaitingDownloadPreview(row.key, outgoing, best_, preview_)) return result_ = IndexScanStep::Item;
                if (!scan_->advance()) return finish(IndexScanStep::Invalid);
                return result_;
            }
            const bool eligible_state = waiting_ ? outgoing.state < 4 && outgoing.continue_intent && !std::memcmp(row.key.data, local_.bytes.data(), 16)
                                                 : outgoing.state == 4;
            if (eligible_state && outgoing.install_generation &&
                (!has_after_ || std::memcmp(row.key.data, after_.data(), after_.size()) > 0) &&
                (!found_ || std::memcmp(row.key.data, best_.key.data(), best_.key.size()) < 0))
            {
                std::memcpy(current_.key.data(), row.key.data, current_.key.size());
                std::memcpy(current_.task.data(), outgoing.task_id.data, current_.task.size());
                current_.created = outgoing.created;
                current_.identity.generation = outgoing.install_generation;
                intent_ = outgoing.continue_intent;
                if (!get_.emplace(volume_).begin(root_, 10, {current_.task.data(), current_.task.size()}, frame_, capacity_)) return finish(IndexScanStep::Invalid);
                phase_ = Phase::Task;
            }
            if (!scan_->advance()) return finish(IndexScanStep::Invalid);
            return result_;
        }
        const auto status = get_->step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready && !(phase_ == Phase::Install && status == IndexGetStep::NotFound))
            return finish(status == IndexGetStep::VolumeChanged ? IndexScanStep::VolumeChanged : status == IndexGetStep::IoError         ? IndexScanStep::IoError
                                                                                             : status == IndexGetStep::WorkspaceTooSmall ? IndexScanStep::WorkspaceTooSmall
                                                                                                                                         : IndexScanStep::Invalid);
        TaskView task;
        if (phase_ == Phase::Task)
        {
            if (!decodeTask({current_.task.data(), current_.task.size()}, get_->value(), task)) return finish(IndexScanStep::Invalid);
            if (task.kind != 2 || task.state == 5 || (waiting_ && (task.state >= 3 || !task.continue_intent))) return continueScan(false);
            if (task.cache_id.size != 32 || task.revision_hash.size != 32 || get_->value().size > task_.size()) return finish(IndexScanStep::Invalid);
            task_size_ = get_->value().size;
            std::memcpy(task_.data(), get_->value().data, task_size_);
            std::memcpy(current_.identity.id.bytes.data(), task.cache_id.data, 32);
            std::memcpy(current_.identity.hash.bytes.data(), task.revision_hash.data, 32);
            current_.installed = task.state == 3;
            if (!get_.emplace(volume_).begin(root_, 2, {current_.identity.id.bytes.data(), 32}, frame_, capacity_)) return finish(IndexScanStep::Invalid);
            phase_ = Phase::Head;
            return result_;
        }
        CacheHeadView head;
        if (phase_ == Phase::Head)
        {
            if (!decodeCacheHead({current_.identity.id.bytes.data(), 32}, get_->value(), head) || get_->value().size > head_.size()) return finish(IndexScanStep::Invalid);
            if (head.install_generation != current_.identity.generation) return continueScan(false);
            if (waiting_)
            {
                if (!decodeTask({current_.task.data(), current_.task.size()}, {task_.data(), task_size_}, task)) return finish(IndexScanStep::Invalid);
                OutgoingView outgoing;
                outgoing.continue_intent = intent_;
                outgoing.task_id = {current_.task.data(), current_.task.size()};
                outgoing.install_generation = current_.identity.generation;
                return continueScan(waitingDownloadEligible({current_.key.data(), current_.key.size()}, outgoing, task, head));
            }
            head_size_ = get_->value().size;
            std::memcpy(head_.data(), get_->value().data, head_size_);
            if (!get_.emplace(volume_).begin(root_, 12, {current_.task.data(), current_.task.size()}, frame_, capacity_)) return finish(IndexScanStep::Invalid);
            phase_ = Phase::Install;
            return result_;
        }
        InstallRecordView install;
        const bool has_install = status == IndexGetStep::Ready;
        if (!decodeTask({current_.task.data(), current_.task.size()}, {task_.data(), task_size_}, task) ||
            !decodeCacheHead({current_.identity.id.bytes.data(), 32}, {head_.data(), head_size_}, head) ||
            (has_install && !decodeInstallRecord({current_.task.data(), current_.task.size()}, get_->value(), install))) return finish(IndexScanStep::Invalid);
        OutgoingView outgoing;
        outgoing.state = 4;
        outgoing.continue_intent = intent_;
        outgoing.task_id = {current_.task.data(), current_.task.size()};
        outgoing.install_generation = current_.identity.generation;
        return continueScan(downloadRecoveryEligible({current_.key.data(), current_.key.size()}, outgoing, task, head, has_install ? &install : nullptr));
    }

  private:
    enum class Phase : uint8_t
    {
        Scan,
        Task,
        Head,
        Install,
        Preview
    };
    IndexScanStep continueScan(bool eligible)
    {
        if (eligible)
        {
            best_ = current_;
            found_ = true;
        }
        get_.reset();
        phase_ = Phase::Scan;
        return result_;
    }
    IndexScanStep finish(IndexScanStep result)
    {
        get_.reset();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::DownloadRecoveryRequest current_, best_;
    std::optional<SdIndexScan> scan_;
    std::optional<SdIndexGet> get_;
    std::array<uint8_t, 256> task_{};
    std::array<uint8_t, 64> head_{};
    std::array<uint8_t, 48> after_{};
    ::geocaching::Destination local_;
    ::geocaching::protocol::SummaryView preview_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, task_size_ = 0, head_size_ = 0;
    bool has_after_ = false, found_ = false, intent_ = false, waiting_ = false;
    Phase phase_ = Phase::Scan;
    IndexScanStep result_ = IndexScanStep::Idle;
};
static_assert(sizeof(SdIndexedDownloadRecovery) <= 2304, "Recovery keeps two keys and current metadata, not a task list");
} // namespace platform::esp::arduino_common::geocaching
