#pragma once
#include "platform/linux/hostlink_track_publication_admission.h"
#include "platform/linux/hostlink_track_signer.h"
#include "platform/linux/track_publication_scan.h"
#include "track_sharing/cancel_publication.h"
#include "track_sharing/publication_candidate_reader.h"
#include <memory>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
class TrackPublicationHistory
{
  public:
    TrackPublicationHistory(std::filesystem::path root, const LinuxReticulumIdentity& identity, const uint8_t operation[16], uint32_t generation, const uint8_t actor[32], bool cancel = false, bool keep_lock = false)
        : storage_(std::make_unique<UploadIo>(std::move(root))), io_(*storage_), scan_(io_), identity_(identity), candidate_(io_), generation_(generation), cancel_(cancel), keep_lock_(keep_lock)
    {
        std::copy_n(operation, 16, operation_.begin());
        std::copy_n(actor, 32, actor_.begin());
        if (!io_.current())
        {
            status_ = Status::StorageUnavailable;
            terminal_ = true;
        }
        else if (!io_.lock())
        {
            status_ = Status::Busy;
            terminal_ = true;
        }
    }
    void step()
    {
        if (terminal_) return;
        if (!io_.current())
        {
            finish(Status::StorageUnavailable);
            return;
        }
        if (!identity_.ready() || identity_.generation() != generation_)
        {
            finish(Status::StaleIdentity);
            return;
        }
        if (authentication_)
        {
            HostlinkTrackSigner signer(identity_);
            const auto checked = authentication_->step(signer, scratch_.data(), scratch_.size());
            if (checked == Status::Busy) return;
            if (checked != Status::Ok)
            {
                finish(checked);
                return;
            }
            const auto& job = *authentication_->result();
            if (found_ && !track_sharing::publicationBindingsEqual(result_, job))
            {
                finish(Status::Conflict);
                return;
            }
            result_ = job;
            found_ = true;
            authentication_.reset();
            return;
        }
        track_sharing::PublicationSlotKey key;
        const auto scanned = scan_.step(operation_.data(), key);
        if (scanned == TrackPublicationScan::Result::Pending) return;
        if (scanned == TrackPublicationScan::Result::Unavailable)
        {
            finish(Status::StorageUnavailable);
            return;
        }
        if (scanned == TrackPublicationScan::Result::End)
        {
            if (!found_)
            {
                finish(stale_ ? Status::StaleIdentity : Status::Expired);
                return;
            }
            if (cancel_)
            {
                std::array<char, 160> a{}, b{};
                if (!track_sharing::resolveObjectPath({result_.object, 32}, track_sharing::ObjectFile::PublicationA, {result_.operation, 16}, a.data(), a.size()) ||
                    !track_sharing::resolveObjectPath({result_.object, 32}, track_sharing::ObjectFile::PublicationB, {result_.operation, 16}, b.data(), b.size()))
                {
                    finish(Status::InvalidField);
                    return;
                }
                track_sharing::PublicationJournalStore<UploadIo> jobs(io_, a.data(), b.data());
                track_sharing::PublicationJournal next, verify;
                const auto cancelled = track_sharing::cancelPublicationBeforePublish(jobs, result_, next, verify, generation_, {actor_.data(), 32}, scratch_.data(), scratch_.size());
                if (cancelled != track_sharing::PublicationStoreResult::Ready)
                {
                    finish(cancelled == track_sharing::PublicationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
                    return;
                }
            }
            finish(Status::Ok);
            return;
        }
        const auto loaded = candidate_.load(key, scratch_.data(), scratch_.size());
        if (loaded != track_sharing::PublicationStoreResult::Ready)
        {
            finish(loaded == track_sharing::PublicationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
            return;
        }
        const auto& job = *candidate_.unverifiedJournal();
        if (std::memcmp(job.author_hash, actor_.data(), 32)) return;
        if (job.generation != generation_)
        {
            stale_ = true;
            return;
        }
        const track_sharing::CommitRequest request{{job.operation, 16}, {job.object, 32}, {job.revision_hash, 32}, {job.directory, 16}, {job.directory_epoch, 16}, job.generation, job.revision};
        authentication_ = std::make_unique<HostlinkTrackPublicationAdmission>(request, *candidate_.unverifiedDirectory(), io_, identity_.publicKey().data(), actor_.data(), true);
    }
    bool terminal() const { return terminal_; }
    Status status() const { return status_; }
    const track_sharing::PublicationJournal* result() const { return terminal_ && status_ == Status::Ok ? &result_ : nullptr; }

    std::unique_ptr<UploadIo> takeStorage()
    {
        if (!keep_lock_ || !terminal_ || (status_ != Status::Ok && status_ != Status::Expired)) return {};
        return std::move(storage_);
    }

  private:
    void finish(Status status)
    {
        status_ = status;
        terminal_ = true;
        authentication_.reset();
        io_.close(nullptr);
        if (!keep_lock_ || (status_ != Status::Ok && status_ != Status::Expired)) io_.unlock();
    }
    std::unique_ptr<UploadIo> storage_;
    UploadIo& io_;
    TrackPublicationScan scan_;
    const LinuxReticulumIdentity& identity_;
    track_sharing::PublicationCandidateReader<UploadIo> candidate_;
    uint32_t generation_;
    std::array<uint8_t, 16> operation_{};
    std::array<uint8_t, 32> actor_{};
    std::array<uint8_t, 448> scratch_{};
    track_sharing::PublicationJournal result_;
    std::unique_ptr<HostlinkTrackPublicationAdmission> authentication_;
    Status status_ = Status::Busy;
    bool terminal_ = false, found_ = false, stale_ = false, cancel_ = false, keep_lock_ = false;
};
#endif
} // namespace platform::linux_runtime
