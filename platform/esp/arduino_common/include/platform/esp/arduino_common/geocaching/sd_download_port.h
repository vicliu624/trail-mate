#pragma once
#include "geocaching/usecase/download_client.h"
#include "geocaching/usecase/gpx_install.h"
#include "platform/esp/arduino_common/geocaching/logical_download_store.h"
#include "platform/esp/arduino_common/geocaching/sd_gpx_hash.h"
#include "platform/esp/arduino_common/geocaching/sd_gpx_stage.h"
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
// One storage-owner job. While staging, the owner must not admit another state
// mutation: the GPX serializer borrows the committed Get response's read lease.
template <class Digest>
class SdDownloadPort final : public ::geocaching::DownloadPort, private ::geocaching::GpxInstallPort
{
  public:
    using Result = ::geocaching::DownloadOperationResult;
    SdDownloadPort(SdRequestStore& store, ::geocaching::storage::LogicalState& state,
                   ::geocaching::protocol::RecordCrypto& crypto, const ::geocaching::Destination& local,
                   const ::geocaching::InstallIdentity& identity, const std::array<uint8_t, 16>& task,
                   const ::geocaching::storage::StoredTime& created)
        : legacy_(store, state), store_(legacy_), crypto_(crypto), local_(local), identity_(identity), task_(task), created_(created) {}
    SdDownloadPort(DownloadStore& store, ::geocaching::protocol::RecordCrypto& crypto, const ::geocaching::Destination& local,
                   const ::geocaching::InstallIdentity& identity, const std::array<uint8_t, 16>& task,
                   const ::geocaching::storage::StoredTime& created)
        : store_(store), crypto_(crypto), local_(local), identity_(identity), task_(task), created_(created) {}
    ~SdDownloadPort() override
    {
        stage_.reset();
        store_.releaseRead();
    }
    Result submit(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id, ::geocaching::ByteView request) override
    {
        if (phase_ != Phase::Idle) return Result::Rejected;
        remote_ = remote;
        request_ = id;
        std::memcpy(key_.data(), local_.bytes.data(), 16);
        std::memcpy(key_.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key_.data() + 32, id.bytes.data(), 16);
        const ::geocaching::storage::RequestTaskTarget target{{identity_.id.bytes.data(), 32}, {identity_.hash.bytes.data(), 32}, identity_.generation};
        const auto result = store_.persistNewTask(local_, remote, id, task_, 2, request, created_, target);
        if (result != JournalWriteResult::InProgress) return Result::Rejected;
        phase_ = Phase::Submitting;
        return Result::Pending;
    }
    Result commit(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id, uint64_t generation,
                  ::geocaching::ByteView response, const ::geocaching::protocol::VerifiedRecordView&) override
    {
        if (phase_ != Phase::Waiting || remote.bytes != remote_.bytes || id.bytes != request_.bytes || generation != identity_.generation) return Result::Rejected;
        const auto result = store_.recordDownloadResponse(local_, remote, id, generation, response, crypto_);
        if (result != JournalWriteResult::InProgress && result != JournalWriteResult::Verified) return Result::Rejected;
        phase_ = result == JournalWriteResult::Verified ? Phase::StartStage : Phase::Receiving;
        return Result::Pending;
    }
    Result resumeWaiting(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id)
    {
        if (phase_ != Phase::Idle) return Result::Rejected;
        remote_ = remote;
        request_ = id;
        std::memcpy(key_.data(), local_.bytes.data(), 16);
        std::memcpy(key_.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key_.data() + 32, id.bytes.data(), 16);
        phase_ = Phase::ReadWaiting;
        return readWaiting();
    }
    Result readWaiting()
    {
        const auto loaded = store_.readDownload(key(), identity_.generation);
        if (loaded == JournalWriteResult::InProgress || loaded == JournalWriteResult::Busy) return Result::Pending;
        if (loaded != JournalWriteResult::Verified) return fail();
        ::geocaching::storage::OutgoingView outgoing;
        ::geocaching::protocol::GetRequestView request;
        if (!store_.downloadIntentActive(key(), identity_.generation) || !store_.readOutgoing(key(), outgoing) || outgoing.state >= 4 ||
            !::geocaching::protocol::decodeGetRequest(outgoing.request, request_, request) || request.wanted_hash.size != 32 ||
            std::memcmp(request.cache_id.data, identity_.id.bytes.data(), 32) ||
            std::memcmp(request.wanted_hash.data, identity_.hash.bytes.data(), 32) ||
            std::memcmp(outgoing.task_id.data, task_.data(), 16)) return fail();
        phase_ = Phase::Waiting;
        store_.releaseRead();
        return Result::Complete;
    }
    // Reconstruct an installation whose exact Get response is already durable.
    // This never resubmits the network request or trusts an unverified file.
    Result resume(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id)
    {
        if (phase_ != Phase::Idle) return Result::Rejected;
        remote_ = remote;
        request_ = id;
        std::memcpy(key_.data(), local_.bytes.data(), 16);
        std::memcpy(key_.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key_.data() + 32, id.bytes.data(), 16);
        phase_ = Phase::ReadRecovery;
        return Result::Pending;
    }
    Result readRecovery()
    {
        const auto loaded = store_.readDownload(key(), identity_.generation);
        if (loaded == JournalWriteResult::InProgress || loaded == JournalWriteResult::Busy) return Result::Pending;
        if (loaded != JournalWriteResult::Verified) return fail();
        ::geocaching::protocol::VerifiedRecordView record;
        if (!readRecord(record)) return fail();
        path(target_, "/trailmate/geocaching/caches/", identity_.id.bytes.data(), 32, ".gpx");
        path(staged_, "/trailmate/geocaching/.state/staging/", task_.data(), 16, ".gpx");
        path(backup_, "/trailmate/geocaching/.state/staging/", task_.data(), 16, ".old.gpx");
        ::geocaching::ByteView value;
        if (!store_.readInstall(task_, value))
        {
            phase_ = Phase::RecoverUnprepared;
            return Result::Pending;
        }
        ::geocaching::storage::InstallRecordView install;
        if (!::geocaching::storage::decodeInstallRecord({task_.data(), task_.size()}, value, install) ||
            (install.phase != ::geocaching::storage::InstallPhase::Prepared && install.phase != ::geocaching::storage::InstallPhase::Installed) ||
            install.generation != identity_.generation ||
            std::memcmp(install.cache_id.data, identity_.id.bytes.data(), 32) ||
            std::memcmp(install.revision_hash.data, identity_.hash.bytes.data(), 32)) return fail();
        std::memcpy(new_hash_.data(), install.new_file_hash.data, 32);
        old_present_ = install.old_file_hash.size != 0;
        if (old_present_)
        {
            std::memcpy(old_hash_.data(), install.old_file_hash.data, 32);
            std::array<uint8_t, 32> recorded;
            if (!store_.installedFileProof(key(), identity_.generation, recorded, old_revision_) || recorded != old_hash_) return fail();
        }
        if (install.phase == ::geocaching::storage::InstallPhase::Installed)
        {
            if (!store_.downloadCompleted(key(), identity_.generation)) return fail();
            phase_ = Phase::RecoverInstalledTarget;
        }
        else phase_ = Phase::RecoverTarget;
        return Result::Pending;
    }

    Result poll() override
    {
        if (phase_ == Phase::Complete) return Result::Complete;
        if (phase_ == Phase::Failed) return Result::Rejected;
        if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return Result::Pending;
        if (phase_ == Phase::Submitting || phase_ == Phase::Receiving || phase_ == Phase::Stopping)
        {
            const auto result = store_.stepCommit();
            if (result == JournalWriteResult::InProgress) return Result::Pending;
            if (result != JournalWriteResult::Verified) return fail();
            if (phase_ == Phase::Stopping)
            {
                phase_ = Phase::Complete;
                return Result::Complete;
            }
            const bool submitted = phase_ == Phase::Submitting;
            phase_ = submitted ? Phase::Waiting : Phase::StartStage;
            if (cancel_requested_) return stop();
            return submitted ? Result::Complete : Result::Pending;
        }
        if (cancel_requested_) return stop();
        if (phase_ == Phase::ReadWaiting) return readWaiting();
        if (phase_ == Phase::ReadRecovery) return readRecovery();
        if (phase_ >= Phase::RecoverInstalledTarget) return recoverInstalled();
        if (phase_ >= Phase::RecoverUnprepared) return recover();
        if (phase_ == Phase::StartStage)
        {
            const auto loaded = store_.readDownload(key(), identity_.generation);
            if (loaded == JournalWriteResult::InProgress || loaded == JournalWriteResult::Busy) return Result::Pending;
            if (loaded != JournalWriteResult::Verified) return fail();
            ::geocaching::protocol::VerifiedRecordView record;
            if (!readRecord(record)) return fail();
            stage_.reset(new (std::nothrow) SdGpxStage);
            if (!stage_ || stage_->begin(
                               task_, record, crypto_, [](void* context, const uint8_t* data, size_t size)
                               { static_cast<Digest*>(context)->update(data, size); },
                               &stage_digest_) != StageResult::InProgress) return fail();
            std::snprintf(staged_.data(), staged_.size(), "%s", stage_->path());
            path(target_, "/trailmate/geocaching/caches/", identity_.id.bytes.data(), 32, ".gpx");
            path(backup_, "/trailmate/geocaching/.state/staging/", task_.data(), 16, ".old.gpx");
            phase_ = Phase::Staging;
            return Result::Pending;
        }
        if (phase_ == Phase::Staging)
        {
            if (!store_.downloadIntentActive(key(), identity_.generation)) return fail();
            const auto result = stage_->step();
            if (result == StageResult::InProgress) return Result::Pending;
            if (result != StageResult::Written || !stage_digest_.finalize(new_hash_.data(), new_hash_.size())) return fail();
            stage_.reset();
            install_.reset(new (std::nothrow)::geocaching::GpxInstall(identity_));
            if (!install_) return fail();
            phase_ = Phase::Installing;
            return Result::Pending;
        }
        if (phase_ == Phase::Installing)
        {
            install_->advance(*this);
            if (install_->step() == ::geocaching::InstallStep::Failed || install_->step() == ::geocaching::InstallStep::Cancelled) return fail();
            if (install_->installed())
            {
                phase_ = backup_moved_ ? Phase::RetainHistory : Phase::Complete;
                return phase_ == Phase::Complete ? Result::Complete : Result::Pending;
            }
            return Result::Pending;
        }
        if (phase_ == Phase::RetainHistory)
        {
            std::array<char, 128> history;
            path(history, "/trailmate/geocaching/.state/history/", old_revision_.bytes.data(), 32, ".gpx");
            if (!history_checked_)
            {
                if (storage::sd_exists(history.data()))
                {
                    store_.releaseRead();
                    phase_ = Phase::Complete;
                    return Result::Complete;
                }
                history_checked_ = true;
                return Result::Pending;
            }
            // The new target is already committed. On retention failure keep
            // the backup and its Installed marker as recovery evidence.
            if (storage::sd_rename(backup_.data(), history.data())) backup_moved_ = false;
            store_.releaseRead();
            phase_ = Phase::Complete;
            return Result::Complete;
        }
        return Result::Rejected;
    }
    Result cancel(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id, uint64_t generation) override
    {
        if (remote.bytes != remote_.bytes || id.bytes != request_.bytes || generation != identity_.generation ||
            phase_ == Phase::Complete || target_moved_ || backup_moved_) return Result::Rejected;
        cancel_requested_ = true;
        return Result::Pending;
    }
    const char* targetPath() const { return target_.data(); }
    bool historyPending() const { return phase_ == Phase::Complete && backup_moved_; }

  private:
    using Effect = ::geocaching::InstallEffectResult;
    using Step = ::geocaching::InstallStep;
    enum class Phase : uint8_t
    {
        Idle,
        Submitting,
        Waiting,
        Receiving,
        StartStage,
        Staging,
        Installing,
        RetainHistory,
        Stopping,
        Complete,
        Failed,
        ReadWaiting,
        ReadRecovery,
        RecoverUnprepared,
        RecoverOrphanName,
        RecoverOrphanMove,
        RecoverTarget,
        RecoverTargetOpen,
        RecoverTargetHash,
        RecoverBackup,
        RecoverBackupOpen,
        RecoverBackupHash,
        RecoverStage,
        RecoverStageOpen,
        RecoverStageHash,
        RecoverInstalledTarget,
        RecoverInstalledTargetHash,
        RecoverInstalledBackup,
        RecoverInstalledBackupOpen,
        RecoverInstalledBackupHash,
        RecoverInstalledHistoryOpen,
        RecoverInstalledHistoryHash
    };
    ::geocaching::ByteView key() const { return {key_.data(), key_.size()}; }
    Result fail()
    {
        stage_.reset();
        store_.releaseRead();
        phase_ = Phase::Failed;
        return Result::Rejected;
    }
    Result stop()
    {
        if (store_.commitPending())
        {
            const auto result = store_.stepCommit();
            if (result == JournalWriteResult::InProgress) return Result::Pending;
            if (result != JournalWriteResult::Verified) return fail();
        }
        if (stage_)
        {
            stage_->cancel();
            stage_.reset();
            return Result::Pending;
        }
        const auto stopped = store_.stopTask(task_);
        if (stopped == JournalWriteResult::Busy) return Result::Pending;
        if (stopped == JournalWriteResult::InProgress)
        {
            phase_ = Phase::Stopping;
            return Result::Pending;
        }
        if (stopped != JournalWriteResult::Verified) return fail();
        phase_ = Phase::Complete;
        return Result::Complete;
    }
    template <size_t N>
    static void path(std::array<char, N>& output, const char* prefix, const uint8_t* id, size_t size, const char* suffix)
    {
        constexpr char hex[] = "0123456789abcdef";
        size_t offset = std::strlen(prefix);
        std::memcpy(output.data(), prefix, offset);
        for (size_t i = 0; i < size; ++i)
        {
            output[offset++] = hex[id[i] >> 4];
            output[offset++] = hex[id[i] & 15];
        }
        std::snprintf(output.data() + offset, output.size() - offset, "%s", suffix);
    }
    bool readRecord(::geocaching::protocol::VerifiedRecordView& record)
    {
        ::geocaching::storage::OutgoingView outgoing;
        ::geocaching::protocol::GetResponseView response;
        if ((!store_.downloadIntentActive(key(), identity_.generation) && !store_.downloadCompleted(key(), identity_.generation)) || !store_.readOutgoing(key(), outgoing) ||
            outgoing.state != 4 || outgoing.task_id.size != task_.size() || std::memcmp(outgoing.task_id.data, task_.data(), task_.size()) ||
            !::geocaching::protocol::decodeGetResponse(outgoing.terminal_data, request_, 8192, response)) return false;
        return store_.verifyRecord(response.signed_cache, crypto_, identity_.id, identity_.hash, record);
    }
    Result recoverInstalled()
    {
        if (!store_.downloadCompleted(key(), identity_.generation)) return fail();
        if (phase_ == Phase::RecoverInstalledHistoryOpen)
        {
            std::array<char, 128> history;
            path(history, "/trailmate/geocaching/.state/history/", old_revision_.bytes.data(), 32, ".gpx");
            if (!startHash(history.data())) return fail();
            phase_ = Phase::RecoverInstalledHistoryHash;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverInstalledTarget || phase_ == Phase::RecoverInstalledBackupOpen)
        {
            if (!startHash(phase_ == Phase::RecoverInstalledTarget ? target_.data() : backup_.data())) return fail();
            phase_ = phase_ == Phase::RecoverInstalledTarget ? Phase::RecoverInstalledTargetHash : Phase::RecoverInstalledBackupHash;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverInstalledBackup)
        {
            if (storage::sd_exists(backup_.data()))
            {
                phase_ = Phase::RecoverInstalledBackupOpen;
                return Result::Pending;
            }
            // Retention may already have finished before the restart. Verify
            // its destination rather than treating a missing backup as proof.
            phase_ = Phase::RecoverInstalledHistoryOpen;
            return Result::Pending;
        }
        if (hasher_->step() == GpxHashStep::Reading) return Result::Pending;
        std::array<uint8_t, 32> actual;
        if (!hasher_->result(actual)) return fail();
        if (phase_ == Phase::RecoverInstalledTargetHash)
        {
            if (actual != new_hash_) return fail();
            if (old_present_)
            {
                phase_ = Phase::RecoverInstalledBackup;
                return Result::Pending;
            }
            store_.releaseRead();
            phase_ = Phase::Complete;
            return Result::Complete;
        }
        if (actual != old_hash_) return fail();
        if (phase_ == Phase::RecoverInstalledHistoryHash)
        {
            store_.releaseRead();
            phase_ = Phase::Complete;
            return Result::Complete;
        }
        backup_moved_ = true;
        phase_ = Phase::RetainHistory;
        return Result::Pending;
    }
    Result continueInstall()
    {
        recovered_files_ = true;
        hasher_.reset();
        hash_digest_.reset();
        install_.reset(new (std::nothrow)::geocaching::GpxInstall(identity_));
        if (!install_) return fail();
        phase_ = Phase::Installing;
        return Result::Pending;
    }
    Result recover()
    {
        if (!store_.downloadIntentActive(key(), identity_.generation)) return fail();
        std::array<char, 96> orphan{};
        if (phase_ == Phase::RecoverUnprepared)
        {
            phase_ = storage::sd_exists(staged_.data()) ? Phase::RecoverOrphanName : Phase::StartStage;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverOrphanName || phase_ == Phase::RecoverOrphanMove)
        {
            const int length = std::snprintf(orphan.data(), orphan.size(), "%s.partial.%lu", staged_.data(), static_cast<unsigned long>(orphan_number_));
            if (length < 0 || static_cast<size_t>(length) >= orphan.size()) return fail();
            if (phase_ == Phase::RecoverOrphanName)
            {
                if (storage::sd_exists(orphan.data()))
                {
                    if (orphan_number_ == UINT32_MAX) return fail();
                    ++orphan_number_;
                }
                else phase_ = Phase::RecoverOrphanMove;
                return Result::Pending;
            }
            if (!storage::sd_rename(staged_.data(), orphan.data())) return fail();
            phase_ = Phase::StartStage;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverTarget)
        {
            phase_ = storage::sd_exists(target_.data()) ? Phase::RecoverTargetOpen : old_present_ ? Phase::RecoverBackup
                                                                                                  : Phase::RecoverStage;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverBackup)
        {
            if (!storage::sd_exists(backup_.data()))
            {
                // Identical-version downloads have identical old/new hashes.
                // Without a backup, the verified target is still the old file;
                // validate staging and perform the normal replacement sequence.
                if (!target_moved_ || new_hash_ != old_hash_) return fail();
                target_moved_ = false;
                phase_ = Phase::RecoverStage;
                return Result::Pending;
            }
            phase_ = Phase::RecoverBackupOpen;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverStage)
        {
            if (!storage::sd_exists(staged_.data())) return fail();
            phase_ = Phase::RecoverStageOpen;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverTargetOpen || phase_ == Phase::RecoverBackupOpen || phase_ == Phase::RecoverStageOpen)
        {
            const bool opened = startHash(phase_ == Phase::RecoverTargetOpen ? target_.data() : phase_ == Phase::RecoverBackupOpen ? backup_.data()
                                                                                                                                   : staged_.data());
            if (!opened) return fail();
            phase_ = phase_ == Phase::RecoverTargetOpen ? Phase::RecoverTargetHash : phase_ == Phase::RecoverBackupOpen ? Phase::RecoverBackupHash
                                                                                                                        : Phase::RecoverStageHash;
            return Result::Pending;
        }
        if (hasher_->step() == GpxHashStep::Reading) return Result::Pending;
        std::array<uint8_t, 32> actual;
        if (!hasher_->result(actual)) return fail();
        if (phase_ == Phase::RecoverTargetHash)
        {
            if (actual == new_hash_)
            {
                target_moved_ = true;
                if (old_present_)
                {
                    phase_ = Phase::RecoverBackup;
                    return Result::Pending;
                }
                return continueInstall();
            }
            if (!old_present_ || actual != old_hash_) return fail();
            phase_ = Phase::RecoverStage;
            return Result::Pending;
        }
        if (phase_ == Phase::RecoverBackupHash)
        {
            if (actual != old_hash_) return fail();
            backup_moved_ = true;
            if (target_moved_) return continueInstall();
            phase_ = Phase::RecoverStage;
            return Result::Pending;
        }
        if (actual != new_hash_) return fail();
        return continueInstall();
    }
    bool startHash(const char* filename)
    {
        hasher_.reset();
        hash_digest_.reset(new (std::nothrow) Digest);
        if (!hash_digest_) return false;
        hasher_.reset(new (std::nothrow) SdGpxHash<Digest>(*hash_digest_));
        return hasher_ && hasher_->open(filename);
    }
    Effect execute(Step step, const ::geocaching::InstallIdentity&) override
    {
        if (!store_.downloadIntentActive(key(), identity_.generation)) return Effect::StaleIntent;
        if (step != last_step_)
        {
            last_step_ = step;
            substep_ = 0;
        }
        switch (step)
        {
        case Step::CheckIntent:
            return Effect::Complete;
        case Step::ValidateStagedFile:
            if (recovered_files_) return Effect::Complete;
            if (substep_ == 0)
            {
                old_present_ = storage::sd_exists(target_.data());
                if (!old_present_) return Effect::Complete;
                if (!store_.installedFileProof(key(), identity_.generation, old_hash_, old_revision_)) return Effect::Failed;
                ++substep_;
                return Effect::Pending;
            }
            if (substep_ == 1)
            {
                if (!startHash(target_.data())) return Effect::Failed;
                ++substep_;
                return Effect::Pending;
            }
            if (hasher_->step() == GpxHashStep::Reading) return Effect::Pending;
            {
                std::array<uint8_t, 32> hash;
                return hasher_->result(hash) && hash == old_hash_ ? Effect::Complete : Effect::Failed;
            }
        case Step::PrepareJournal:
        {
            const auto result = substep_++ == 0 ? store_.prepareDownloadInstall(key(), task_, identity_.generation, new_hash_,
                                                                                old_present_ ? ::geocaching::ByteView{old_hash_.data(), old_hash_.size()} : ::geocaching::ByteView{}, crypto_)
                                                : store_.stepCommit();
            return result == JournalWriteResult::InProgress ? Effect::Pending : result == JournalWriteResult::Verified ? Effect::Complete
                                                                                                                       : Effect::Failed;
        }
        case Step::ReplaceFile:
            if (target_moved_) return Effect::Complete;
            if (substep_ == 0)
            {
                if (old_present_ && !backup_moved_ && storage::sd_exists(backup_.data())) return Effect::Failed;
                if ((!old_present_ || backup_moved_) && storage::sd_exists(target_.data())) return Effect::Failed;
                ++substep_;
                return Effect::Pending;
            }
            if (substep_ == 1 && old_present_ && !backup_moved_)
            {
                if (!storage::sd_rename(target_.data(), backup_.data())) return Effect::Failed;
                backup_moved_ = true;
                ++substep_;
                return Effect::Pending;
            }
            if (!storage::sd_rename(staged_.data(), target_.data())) return Effect::Failed;
            target_moved_ = true;
            return Effect::Complete;
        case Step::ValidateInstalledFile:
            if (substep_++ == 0) return startHash(target_.data()) ? Effect::Pending : Effect::Failed;
            if (hasher_->step() == GpxHashStep::Reading) return Effect::Pending;
            {
                std::array<uint8_t, 32> hash;
                return hasher_->result(hash) && hash == new_hash_ ? Effect::Complete : Effect::Failed;
            }
        case Step::CommitJournal:
        {
            const auto result = substep_++ == 0 ? store_.finishDownloadInstall(key(), task_, identity_.generation, new_hash_) : store_.stepCommit();
            return result == JournalWriteResult::InProgress ? Effect::Pending : result == JournalWriteResult::Verified ? Effect::Complete
                                                                                                                       : Effect::Failed;
        }
        default:
            return Effect::Failed;
        }
    }
    LogicalDownloadStore legacy_;
    DownloadStore& store_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::Destination local_, remote_;
    ::geocaching::RequestId request_;
    ::geocaching::InstallIdentity identity_;
    ::geocaching::RevisionHash old_revision_;
    std::array<uint8_t, 16> task_;
    ::geocaching::storage::StoredTime created_;
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 32> new_hash_{}, old_hash_{};
    std::array<char, 128> target_{};
    std::array<char, 96> staged_{}, backup_{};
    Digest stage_digest_;
    std::unique_ptr<Digest> hash_digest_;
    std::unique_ptr<SdGpxHash<Digest>> hasher_;
    std::unique_ptr<SdGpxStage> stage_;
    std::unique_ptr<::geocaching::GpxInstall> install_;
    Phase phase_ = Phase::Idle;
    Step last_step_ = Step::Complete;
    unsigned substep_ = 0;
    uint32_t orphan_number_ = 0;
    bool recovered_files_ = false;
    bool old_present_ = false, target_moved_ = false, backup_moved_ = false, cancel_requested_ = false, history_checked_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
