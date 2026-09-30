#pragma once
#include "platform/esp/arduino_common/geocaching/download_store.h"
#include "platform/esp/arduino_common/geocaching/sd_request_store.h"

namespace platform::esp::arduino_common::geocaching
{
// Compatibility adapter for the pre-migration store and its regression tests.
// No duplicate GPX implementation or additional logical state is created.
class LogicalDownloadStore final : public DownloadStore
{
  public:
    LogicalDownloadStore() = default;
    LogicalDownloadStore(SdRequestStore& store, ::geocaching::storage::LogicalState& state) : store_(&store), state_(&state) {}
    bool needsRecovery() const override { return !store_ || store_->needsRecovery(); }
    DownloadRecoveryRead readSavedCache(::geocaching::ByteView key, bool exact, ::geocaching::protocol::RecordCrypto& crypto,
                                        ::geocaching::storage::SavedCacheRecord& out) override
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        out = {};
        if ((key.size && (!key.data || key.size != 32)) || (exact && key.size != 32)) return DownloadRecoveryRead::Invalid;
        if (needsRecovery() || !state_) return DownloadRecoveryRead::Unavailable;
        if (store_->commitPending()) return DownloadRecoveryRead::Busy;
        const auto view = state_->view();
        CacheHeadView selected;
        size_t cursor = 0;
        MutationView row;
        while (view.next(cursor, row))
        {
            if (row.table != 2) continue;
            CacheHeadView head;
            if (!decodeCacheHead(row.key, row.value, head)) return DownloadRecoveryRead::Invalid;
            if (!head.current_hash.size || (exact ? std::memcmp(row.key.data, key.data, 32) != 0 : key.size && std::memcmp(row.key.data, key.data, 32) <= 0) ||
                (selected.install_generation && std::memcmp(row.key.data, out.id.data(), 32) >= 0)) continue;
            selected = head;
            std::memcpy(out.id.data(), row.key.data, 32);
            std::memcpy(out.hash.data(), head.current_hash.data, 32);
        }
        if (!selected.install_generation) return DownloadRecoveryRead::End;
        uint64_t installed = 0;
        std::array<uint8_t, 16> task_id{};
        cursor = 0;
        while (view.next(cursor, row))
        {
            if (row.table != 12) continue;
            InstallRecordView install;
            if (!decodeInstallRecord(row.key, row.value, install)) return DownloadRecoveryRead::Invalid;
            if (install.phase != InstallPhase::Installed || install.generation > selected.install_generation || install.generation <= installed ||
                std::memcmp(install.cache_id.data, out.id.data(), 32) || std::memcmp(install.revision_hash.data, out.hash.data(), 32)) continue;
            installed = install.generation;
            std::memcpy(task_id.data(), row.key.data, 16);
            std::memcpy(out.file_hash.data(), install.new_file_hash.data, 32);
        }
        ByteView value;
        TaskView task;
        const ByteView task_key{task_id.data(), task_id.size()};
        if (!installed || !view.find(10, task_key, value) || !decodeTask(task_key, value, task) || task.kind != 2 || task.state != 3 ||
            task.cache_id.size != 32 || task.revision_hash.size != 32 || std::memcmp(task.cache_id.data, out.id.data(), 32) ||
            std::memcmp(task.revision_hash.data, out.hash.data(), 32)) return DownloadRecoveryRead::Invalid;
        for (size_t i = 0; i < task.request_count; ++i)
        {
            OutgoingView outgoing;
            if (!view.find(5, task.requests[i], value) || !decodeOutgoing(task.requests[i], value, outgoing) ||
                !requestBelongsToTask(task_key, task, task.requests[i], outgoing)) return DownloadRecoveryRead::Invalid;
            if (outgoing.state != 4 || outgoing.install_generation != installed) continue;
            RequestId id;
            std::memcpy(id.bytes.data(), task.requests[i].data + 32, 16);
            protocol::GetResponseView response;
            if (!protocol::decodeGetResponse(outgoing.terminal_data, id, 8192, response) || response.has_conflict) return DownloadRecoveryRead::Invalid;
            auto verified = protocol::VerificationResult::WorkspaceTooSmall;
            if (!state_->withScratch([&](uint8_t* bytes, size_t capacity)
                                     { verified = verifySavedCache(response.signed_cache, crypto, bytes, capacity, out); })) return DownloadRecoveryRead::Busy;
            return verified == protocol::VerificationResult::Valid               ? DownloadRecoveryRead::Ready
                   : verified == protocol::VerificationResult::WorkspaceTooSmall ? DownloadRecoveryRead::WorkspaceTooSmall
                   : verified == protocol::VerificationResult::CryptoUnavailable ? DownloadRecoveryRead::Unavailable
                                                                                 : DownloadRecoveryRead::Invalid;
        }
        return DownloadRecoveryRead::Invalid;
    }
    DownloadRecoveryRead readNextGeneration(const ::geocaching::GeocacheId& id, uint64_t& generation) override
    {
        generation = 0;
        if (needsRecovery() || !state_) return DownloadRecoveryRead::Unavailable;
        if (store_->commitPending()) return DownloadRecoveryRead::Busy;
        ::geocaching::ByteView bytes;
        ::geocaching::storage::CacheHeadView head;
        const ::geocaching::ByteView key{id.bytes.data(), id.bytes.size()};
        if (state_->view().find(2, key, bytes) && !::geocaching::storage::decodeCacheHead(key, bytes, head)) return DownloadRecoveryRead::Invalid;
        generation = head.install_generation == UINT64_MAX ? 0 : head.install_generation + 1;
        return DownloadRecoveryRead::Ready;
    }
    DownloadRecoveryRead readRecovery(::geocaching::ByteView after, ::geocaching::storage::DownloadRecoveryRequest& out) override
    {
        out = {};
        if (needsRecovery() || !state_) return DownloadRecoveryRead::Unavailable;
        if (store_->commitPending()) return DownloadRecoveryRead::Busy;
        const auto result = ::geocaching::storage::nextDownloadRecovery(state_->view(), after, out);
        return result == ::geocaching::storage::DownloadRecoverySelection::Found ? DownloadRecoveryRead::Ready
               : result == ::geocaching::storage::DownloadRecoverySelection::End ? DownloadRecoveryRead::End
                                                                                 : DownloadRecoveryRead::Invalid;
    }
    JournalWriteResult readDownload(::geocaching::ByteView key, uint64_t generation) override
    {
        return downloadIntentActive(key, generation) || downloadCompleted(key, generation) ? JournalWriteResult::Verified : JournalWriteResult::StateRejected;
    }
    DownloadRecoveryRead readWaitingDownload(const ::geocaching::Destination& local, ::geocaching::storage::DownloadRecoveryRequest& out,
                                             ::geocaching::protocol::SummaryView& preview) override
    {
        out = {};
        preview = {};
        if (needsRecovery() || !state_) return DownloadRecoveryRead::Unavailable;
        if (store_->commitPending()) return DownloadRecoveryRead::Busy;
        const auto result = ::geocaching::storage::selectWaitingDownload(state_->view(), local, out, preview);
        return result == ::geocaching::storage::DownloadRecoverySelection::Found ? DownloadRecoveryRead::Ready
               : result == ::geocaching::storage::DownloadRecoverySelection::End ? DownloadRecoveryRead::End
                                                                                 : DownloadRecoveryRead::Invalid;
    }
    void releaseRead() override {}
    bool readOutgoing(::geocaching::ByteView key, ::geocaching::storage::OutgoingView& out) const override
    {
        ::geocaching::ByteView value;
        return state_ && state_->view().find(5, key, value) && ::geocaching::storage::decodeOutgoing(key, value, out);
    }
    bool readInstall(const std::array<uint8_t, 16>& task, ::geocaching::ByteView& out) const override
    {
        return state_ && state_->view().find(12, {task.data(), task.size()}, out);
    }
    bool verifyRecord(::geocaching::ByteView signed_cache, ::geocaching::protocol::RecordCrypto& crypto,
                      const ::geocaching::GeocacheId& id, const ::geocaching::RevisionHash& hash,
                      ::geocaching::protocol::VerifiedRecordView& out) override
    {
        auto result = ::geocaching::protocol::VerificationResult::WorkspaceTooSmall;
        return state_ && state_->withScratch([&](uint8_t* bytes, size_t capacity)
                                             { result = ::geocaching::protocol::verifyGeocache(signed_cache, crypto, bytes, capacity, out, &id, &hash); }) &&
               result == ::geocaching::protocol::VerificationResult::Valid;
    }
    bool downloadIntentActive(::geocaching::ByteView key, uint64_t generation) const override
    {
        return store_ && store_->downloadIntentActive(key, generation);
    }
    bool downloadCompleted(::geocaching::ByteView key, uint64_t generation) const override
    {
        using namespace ::geocaching::storage;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        InstallRecordView install;
        ::geocaching::ByteView value;
        if (!generation || !readOutgoing(key, outgoing) || outgoing.state != 4 || outgoing.install_generation != generation ||
            !state_->view().find(10, outgoing.task_id, value) || !decodeTask(outgoing.task_id, value, task) || task.kind != 2 || task.state != 3 ||
            !requestBelongsToTask(outgoing.task_id, task, key, outgoing) ||
            !state_->view().find(2, task.cache_id, value) || !decodeCacheHead(task.cache_id, value, head) || head.install_generation != generation ||
            head.current_hash.size != 32 || task.revision_hash.size != 32 || std::memcmp(head.current_hash.data, task.revision_hash.data, 32) ||
            !state_->view().find(12, outgoing.task_id, value) || !decodeInstallRecord(outgoing.task_id, value, install)) return false;
        return install.phase == InstallPhase::Installed && install.generation == generation &&
               !std::memcmp(install.cache_id.data, task.cache_id.data, 32) && !std::memcmp(install.revision_hash.data, task.revision_hash.data, 32);
    }
    bool installedFileProof(::geocaching::ByteView key, uint64_t generation, std::array<uint8_t, 32>& hash,
                            ::geocaching::RevisionHash& revision) const override
    {
        if (downloadCompleted(key, generation))
        {
            using namespace ::geocaching::storage;
            OutgoingView outgoing;
            ::geocaching::ByteView value;
            InstallRecordView current;
            if (!readOutgoing(key, outgoing) || !state_->view().find(12, outgoing.task_id, value) ||
                !decodeInstallRecord(outgoing.task_id, value, current) || current.old_file_hash.size != 32) return false;
            size_t cursor = 0;
            uint64_t latest = 0;
            MutationView row;
            while (state_->view().next(cursor, row))
            {
                if (row.table != 12) continue;
                InstallRecordView previous;
                if (!decodeInstallRecord(row.key, row.value, previous)) return false;
                if (previous.phase == InstallPhase::Installed && previous.generation < generation && previous.generation > latest &&
                    !std::memcmp(previous.cache_id.data, current.cache_id.data, 32) && !std::memcmp(previous.new_file_hash.data, current.old_file_hash.data, 32))
                {
                    latest = previous.generation;
                    std::memcpy(hash.data(), previous.new_file_hash.data, 32);
                    std::memcpy(revision.bytes.data(), previous.revision_hash.data, 32);
                }
            }
            return latest != 0;
        }
        return store_->installedFileProof(key, generation, hash, revision);
    }
    bool commitPending() const override { return store_->commitPending(); }
    JournalWriteResult stepCommit() override { return store_->stepCommit(); }
    JournalWriteResult persistNewTask(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                      const ::geocaching::RequestId& request, const std::array<uint8_t, 16>& task, uint8_t kind,
                                      ::geocaching::ByteView bytes, const ::geocaching::storage::StoredTime& time,
                                      const ::geocaching::storage::RequestTaskTarget& target) override
    {
        return store_->persistNewTask(local, remote, request, task, kind, bytes, time, target);
    }
    JournalWriteResult recordDownloadResponse(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                              const ::geocaching::RequestId& request, uint64_t generation, ::geocaching::ByteView response,
                                              ::geocaching::protocol::RecordCrypto& crypto) override
    {
        return store_->recordDownloadResponse(local, remote, request, generation, response, crypto);
    }
    JournalWriteResult stopTask(const std::array<uint8_t, 16>& task) override { return store_->stopTask(task); }
    JournalWriteResult prepareDownloadInstall(::geocaching::ByteView request, const std::array<uint8_t, 16>& task,
                                              uint64_t generation, const std::array<uint8_t, 32>& hash, ::geocaching::ByteView old_hash,
                                              ::geocaching::protocol::RecordCrypto& crypto) override
    {
        return store_->prepareDownloadInstall(request, task, generation, hash, old_hash, crypto);
    }
    JournalWriteResult finishDownloadInstall(::geocaching::ByteView request, const std::array<uint8_t, 16>& task,
                                             uint64_t generation, const std::array<uint8_t, 32>& hash) override
    {
        return store_->finishDownloadInstall(request, task, generation, hash);
    }

  private:
    SdRequestStore* store_ = nullptr;
    ::geocaching::storage::LogicalState* state_ = nullptr;
};
} // namespace platform::esp::arduino_common::geocaching
