#pragma once
#include "geocaching/protocol/get_response.h"
#include "geocaching/storage/install_record.h"
#include "geocaching/storage/saved_cache.h"
#include "geocaching/storage/task_record.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"

namespace platform::esp::arduino_common::geocaching
{
// A single candidate under the caller's root/frame lease. Reuses one index
// operation at a time and keeps only IDs, file proof and up to three task keys.
class SdIndexedSavedCache
{
  public:
    explicit SdIndexedSavedCache(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, ::geocaching::ByteView key, bool exact,
               uint8_t* frame, size_t capacity)
    {
        if (status_ != IndexScanStep::Idle || (key.size && (!key.data || key.size != 32)) || (exact && key.size != 32)) return false;
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        has_after_ = key.size != 0;
        if (has_after_) std::memcpy(after_.data(), key.data, 32);
        phase_ = exact ? Phase::Head : Phase::Heads;
        const bool begun = exact ? operation_.emplace<SdIndexGet>(volume_).begin(root_, 2, key, frame_, capacity_)
                                 : operation_.emplace<SdIndexScan>(volume_).begin(root_, 2, frame_, capacity_);
        if (!begun) return false;
        status_ = IndexScanStep::Working;
        return true;
    }
    bool result(::geocaching::storage::SavedCacheRecord& out, ::geocaching::ByteView& signed_cache) const
    {
        out = {};
        signed_cache = {};
        if (status_ != IndexScanStep::Item) return false;
        out = record_;
        signed_cache = signed_;
        return true;
    }
    IndexScanStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (status_ != IndexScanStep::Working) return status_;
        if (phase_ == Phase::Heads || phase_ == Phase::Installs)
        {
            auto& scan = std::get<SdIndexScan>(operation_);
            const auto status = scan.step();
            if (status == IndexScanStep::Working) return status_;
            if (status == IndexScanStep::End)
            {
                if (phase_ == Phase::Heads) return head_generation_ ? startInstalls() : finish(IndexScanStep::End);
                if (!installed_generation_ || !operation_.emplace<SdIndexGet>(volume_).begin(root_, 10, {task_.data(), task_.size()}, frame_, capacity_))
                    return finish(IndexScanStep::Invalid);
                phase_ = Phase::Task;
                return status_;
            }
            if (status != IndexScanStep::Item) return finish(status);
            MutationView row;
            if (!scan.item(row)) return finish(IndexScanStep::Invalid);
            if (phase_ == Phase::Heads)
            {
                CacheHeadView head;
                if (!decodeCacheHead(row.key, row.value, head)) return finish(IndexScanStep::Invalid);
                if (head.current_hash.size && (!has_after_ || std::memcmp(row.key.data, after_.data(), 32) > 0) &&
                    (!head_generation_ || std::memcmp(row.key.data, record_.id.data(), 32) < 0)) saveHead(row.key, head);
            }
            else
            {
                InstallRecordView install;
                if (!decodeInstallRecord(row.key, row.value, install)) return finish(IndexScanStep::Invalid);
                if (install.phase == InstallPhase::Installed && install.generation <= head_generation_ && install.generation > installed_generation_ &&
                    !std::memcmp(install.cache_id.data, record_.id.data(), 32) && !std::memcmp(install.revision_hash.data, record_.hash.data(), 32))
                {
                    installed_generation_ = install.generation;
                    std::memcpy(task_.data(), row.key.data, 16);
                    std::memcpy(record_.file_hash.data(), install.new_file_hash.data, 32);
                }
            }
            if (!scan.advance()) return finish(IndexScanStep::Invalid);
            return status_;
        }
        auto& get = std::get<SdIndexGet>(operation_);
        const auto status = get.step();
        if (status == IndexGetStep::Working) return status_;
        if (phase_ == Phase::Head && status == IndexGetStep::NotFound) return finish(IndexScanStep::End);
        if (status != IndexGetStep::Ready)
            return finish(status == IndexGetStep::IoError ? IndexScanStep::IoError : status == IndexGetStep::VolumeChanged   ? IndexScanStep::VolumeChanged
                                                                                 : status == IndexGetStep::WorkspaceTooSmall ? IndexScanStep::WorkspaceTooSmall
                                                                                                                             : IndexScanStep::Invalid);
        if (phase_ == Phase::Head)
        {
            CacheHeadView head;
            if (!decodeCacheHead({after_.data(), after_.size()}, get.value(), head)) return finish(IndexScanStep::Invalid);
            if (!head.current_hash.size) return finish(IndexScanStep::End);
            saveHead({after_.data(), after_.size()}, head);
            return startInstalls();
        }
        if (phase_ == Phase::Task)
        {
            TaskView task;
            if (!decodeTask({task_.data(), task_.size()}, get.value(), task) || task.kind != 2 || task.state != 3 ||
                task.cache_id.size != 32 || task.revision_hash.size != 32 || !task.request_count ||
                std::memcmp(task.cache_id.data, record_.id.data(), 32) || std::memcmp(task.revision_hash.data, record_.hash.data(), 32)) return finish(IndexScanStep::Invalid);
            request_count_ = task.request_count;
            for (size_t i = 0; i < request_count_; ++i) std::memcpy(requests_[i].data(), task.requests[i].data, 48);
            phase_ = Phase::Outgoing;
            return startRequest();
        }
        OutgoingView outgoing;
        const ByteView key{requests_[request_].data(), 48};
        if (!decodeOutgoing(key, get.value(), outgoing) || std::memcmp(outgoing.task_id.data, task_.data(), 16)) return finish(IndexScanStep::Invalid);
        if (outgoing.state != 4 || outgoing.install_generation != installed_generation_)
        {
            if (++request_ == request_count_) return finish(IndexScanStep::Invalid);
            return startRequest();
        }
        RequestId id;
        std::memcpy(id.bytes.data(), key.data + 32, 16);
        protocol::GetResponseView response;
        if (!protocol::decodeGetResponse(outgoing.terminal_data, id, 8192, response) || response.has_conflict) return finish(IndexScanStep::Invalid);
        signed_ = response.signed_cache;
        return status_ = IndexScanStep::Item;
    }

  private:
    enum class Phase : uint8_t
    {
        Heads,
        Head,
        Installs,
        Task,
        Outgoing
    };
    void saveHead(::geocaching::ByteView key, const ::geocaching::storage::CacheHeadView& head)
    {
        head_generation_ = head.install_generation;
        std::memcpy(record_.id.data(), key.data, 32);
        std::memcpy(record_.hash.data(), head.current_hash.data, 32);
    }
    IndexScanStep startInstalls()
    {
        if (!operation_.emplace<SdIndexScan>(volume_).begin(root_, 12, frame_, capacity_)) return finish(IndexScanStep::Invalid);
        phase_ = Phase::Installs;
        return status_;
    }
    IndexScanStep startRequest()
    {
        if (!operation_.emplace<SdIndexGet>(volume_).begin(root_, 5, {requests_[request_].data(), 48}, frame_, capacity_)) return finish(IndexScanStep::Invalid);
        return status_;
    }
    IndexScanStep finish(IndexScanStep result)
    {
        operation_.emplace<std::monostate>();
        return status_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::variant<std::monostate, SdIndexScan, SdIndexGet> operation_;
    ::geocaching::storage::SavedCacheRecord record_;
    ::geocaching::ByteView signed_;
    std::array<uint8_t, 32> after_{};
    std::array<uint8_t, 16> task_{};
    std::array<std::array<uint8_t, 48>, 3> requests_{};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, request_count_ = 0, request_ = 0;
    uint64_t head_generation_ = 0, installed_generation_ = 0;
    Phase phase_ = Phase::Heads;
    IndexScanStep status_ = IndexScanStep::Idle;
    bool has_after_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
