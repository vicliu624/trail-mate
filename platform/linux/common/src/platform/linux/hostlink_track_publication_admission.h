#pragma once
#include "chat/infra/reticulum/reticulum_wire.h"
#include "hostlink/management_envelope.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/route_image_source_io.h"
#include "platform/linux/track_manifest_crypto.h"
#include "platform/linux/track_receipt_crypto.h"
#include "track_sharing/admit_publication_intent.h"
#include "track_sharing/commit_request.h"
#include "track_sharing/object_paths.h"
#include "track_sharing/publication_intent.h"
#include "track_sharing/commit_publication_head.h"
#include "track_sharing/load_publication_history.h"
#include "track_sharing/object_head_store.h"
#include "track_sharing/persist_publication_intent.h"
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/publication_receipt_loader.h"
#include "track_sharing/reservation_store.h"
#include "track_sharing/signed_manifest_reader.h"
namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
using Status = ::hostlink::management::Status;
// Native platform binding for shared durable admission/recovery predicates.
// The caller owns storage exclusion, current identity and this heap arena.
class HostlinkTrackPublicationAdmission
{
    friend class HostlinkTrackPublicationExecution;

  public:
    HostlinkTrackPublicationAdmission(const track_sharing::CommitRequest& request, const track_sharing::PublicationDirectory& directory,
                                      UploadIo& io, const uint8_t* author, const uint8_t* author_hash, bool read_only = false)
        : directory_(directory), io_(io),
          reader_(io_), file_check_(io_, hash_), heads_(io_, head_a_, head_b_), reservations_(io_, reservation_a_, reservation_b_),
          directories_(io_, directory_path_), jobs_(io_, job_a_, job_b_), receipt_reader_(io_, receipt_crypto_, job_, directory_, receipt_, 0), read_only_(read_only)
    {
        std::memcpy(bindings_, request.operation.data, 16);
        std::memcpy(bindings_ + 16, request.object.data, 32);
        std::memcpy(bindings_ + 48, request.revision_hash.data, 32);
        std::memcpy(bindings_ + 80, request.directory.data, 16);
        std::memcpy(bindings_ + 96, request.directory_epoch.data, 16);
        request_ = {{bindings_, 16}, {bindings_ + 16, 32}, {bindings_ + 48, 32}, {bindings_ + 80, 16}, {bindings_ + 96, 16}, request.generation, request.revision};
        std::memcpy(author_, author, 64);
        std::memcpy(author_hash_, author_hash, 32);
    }
    HostlinkTrackPublicationAdmission(const HostlinkTrackPublicationAdmission&) = delete;
    HostlinkTrackPublicationAdmission& operator=(const HostlinkTrackPublicationAdmission&) = delete;
    bool hasBackingStorage() const { return io_.current(); }
    bool mediaReady() const { return io_.current(); }
    bool ready() const { return stage_ == Stage::Ready && identity_verified_ && mediaReady(); }
    bool readOnly() const { return read_only_; }
    const track_sharing::PublicationJournal* result() const { return ready() ? &job_ : nullptr; }
    const track_sharing::VerifiedManifestView* manifest() const { return ready() ? &manifest_ : nullptr; }
    template <class Signer>
    Status step(Signer& adapter, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        identity_verified_ = false;
        if (!scratch || capacity < 448 || signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this))) return fail(Status::InvalidField);
        if (!mediaReady()) return fail(Status::StorageUnavailable);
        if (!directoryBindingValid()) return fail(Status::Conflict);
        uint8_t current_hash[32];
        uint32_t generation = 0;
        auto* key = scratch + capacity - 64;
        const auto identity = adapter.readAuthor(key, current_hash, generation);
        if (identity == Status::Busy) return identity;
        if (identity != Status::Ok) return fail(identity);
        if (generation != request_.generation || std::memcmp(key, author_, 64) || std::memcmp(current_hash, author_hash_, 32)) return fail(Status::StaleIdentity);
        identity_verified_ = true;
        if (stage_ == Stage::Failed) return status_;
        if (stage_ == Stage::Ready) return Status::Ok;
        if (stage_ == Stage::Idle) return begin(scratch, capacity);
        if (stage_ == Stage::Reading)
        {
            const auto read = reader_.step();
            if (read == FileIntegrityResult::Pending) return Status::Busy;
            if (read != FileIntegrityResult::Ready) return fail(Status::StorageUnavailable);
            if (verifyManifest(reader_.bytes(), crypto_, hash_, workspace_, sizeof(workspace_), manifest_, {author_, 64}) != ManifestVerification::Valid) return fail(Status::Conflict);
            if (read_only_) historicalHead();
            if (!requestMatchesHead() || !signedHeadMatches(head_, manifest_, reservation_, request_.generation, {author_, 64}, {author_hash_, 32})) return fail(Status::Conflict);
            if (manifest_.manifest.file_bytes > directory_.file_limit) return fail(Status::LimitExceeded);
            if (!resolveObjectPath(request_.object, ObjectFile::KmlFile, manifest_.manifest.file_hash, kml_path_, sizeof(kml_path_))) return fail(Status::InvalidField);
            if (file_check_.begin(kml_path_, manifest_.manifest.file_bytes, manifest_.manifest.file_hash.data) != FileIntegrityResult::Pending) return fail(Status::StorageUnavailable);
            stage_ = Stage::Checking;
            return Status::Busy;
        }
        if (stage_ == Stage::Checking)
        {
            const auto checked = file_check_.step(scratch, capacity);
            if (checked == FileIntegrityResult::Pending) return Status::Busy;
            if (checked != FileIntegrityResult::Ready) return fail(checked == FileIntegrityResult::HashMismatch ? Status::HashMismatch : Status::StorageUnavailable);
            stage_ = Stage::Admitting;
            return Status::Busy;
        }
        if (stage_ == Stage::ReceiptReading)
        {
            const auto recovered = receipt_reader_.step(workspace_ + 1100, sizeof(workspace_) - 1100);
            if (recovered == ReceiptLoadResult::Pending) return Status::Busy;
            if (recovered != ReceiptLoadResult::Ready || !receipt_reader_.ready()) return fail(recovered == ReceiptLoadResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
            stage_ = Stage::Ready;
            return status_ = Status::Ok;
        }
        // Re-read both durable evidence sets after all sliced reads. Runtime
        // holds exclusive object paths; replacement media is rejected above.
        if (!read_only_)
        {
            const auto loaded = heads_.load(head_, head_verify_, scratch, capacity);
            if (loaded != HeadStoreResult::Ready) return fail(loaded == HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        }
        const auto reserved = reservations_.load(reservation_, reservation_verify_, scratch, capacity);
        if (reserved != ReservationStoreResult::Ready) return fail(reserved == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        if (read_only_) historicalHead();
        if (!requestMatchesHead() || !signedHeadMatches(head_, manifest_, reservation_, request_.generation, {author_, 64}, {author_hash_, 32})) return fail(Status::Conflict);
        // A Committed head is legal only with an already durable matching job.
        // Construct identical desired evidence using a shadow Signed head; no
        // persisted head is rolled back or rewritten by this shadow.
        head_next_ = head_;
        head_next_.phase = HeadPhase::Signed;
        if (!createPublicationIntent(request_, head_next_, manifest_, reservation_, {author_, 64}, {author_hash_, 32}, file_check_, desired_)) return fail(Status::Conflict);
        if (read_only_ || head_.phase == HeadPhase::Committed)
        {
            const auto existing = loadPublicationHistory(directories_, jobs_, directory_, desired_, directory_verify_,
                                                         job_, job_verify_, scratch, capacity);
            if (existing != PublicationStoreResult::Ready) return fail(existing == PublicationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        }
        else
        {
            const auto admitted = admitPublicationIntent(directories_, jobs_, directory_, directory_verify_, desired_, job_, job_verify_, scratch, capacity);
            if (admitted != PublicationStoreResult::Ready) return fail(admitted == PublicationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        }
        if (!read_only_)
        {
            const auto committed = commitPublicationHead(heads_, jobs_, job_, job_verify_, manifest_, reservation_, request_.generation,
                                                         {author_, 64}, {author_hash_, 32}, head_, head_next_, head_verify_, scratch, capacity);
            if (committed != HeadStoreResult::Ready) return fail(committed == HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        }
        if (job_.phase == PublicationPhase::Confirmed)
        {
            if (!resolveObjectPath(request_.object, ObjectFile::Receipt, request_.operation, receipt_path_, sizeof(receipt_path_)) ||
                !receipt_reader_.setExpectedState(head_.archived ? 1 : 0)) return fail(Status::InvalidField);
            // Reuse the former manifest-domain workspace: first 1100 bytes
            // hold the receipt, remaining bytes hold its verification domain.
            const auto started = receipt_reader_.begin(receipt_path_, workspace_, 1100);
            if (started != ReceiptLoadResult::Pending) return fail(started == ReceiptLoadResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
            stage_ = Stage::ReceiptReading;
            return Status::Busy;
        }
        stage_ = Stage::Ready;
        status_ = Status::Ok;
        return status_;
    }

  private:
    bool directoryBindingValid() const
    {
        if (!track_sharing::publicationDirectoryValid(directory_)) return false;
        uint8_t hash[16], name[10], delivery[16];
        ::chat::reticulum::computeIdentityHash(directory_.public_key, hash);
        ::chat::reticulum::computeNameHash("lxmf", "delivery", name);
        ::chat::reticulum::computeDestinationHash(name, hash, delivery);
        return !std::memcmp(delivery, directory_.delivery, 16);
    }
    // Never persisted. The immutable signed manifest and this operation's
    // reservation authenticate historical bindings independently of latest head.
    void historicalHead()
    {
        head_ = {};
        head_.sequence = 1;
        head_.generation = reservation_.generation;
        head_.revision = reservation_.revision;
        head_.phase = track_sharing::HeadPhase::Signed;
        head_.archived = manifest_.manifest.state == 1;
        std::memcpy(head_.object, reservation_.object, 32);
        std::memcpy(head_.hash, reservation_.candidate, 32);
        std::memcpy(head_.parent, reservation_.parent, 32);
        std::memcpy(head_.operation, reservation_.operation, 16);
    }
    enum class Stage : uint8_t
    {
        Idle,
        Reading,
        Checking,
        Admitting,
        ReceiptReading,
        Ready,
        Failed
    };
    bool requestMatchesHead() const
    {
        return head_.generation == request_.generation && head_.revision == request_.revision &&
               !std::memcmp(head_.operation, request_.operation.data, 16) && !std::memcmp(head_.object, request_.object.data, 32) &&
               !std::memcmp(head_.hash, request_.revision_hash.data, 32);
    }
    Status begin(uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (!resolveObjectPath(request_.object, ObjectFile::HeadA, {}, head_a_, sizeof(head_a_)) ||
            !resolveObjectPath(request_.object, ObjectFile::HeadB, {}, head_b_, sizeof(head_b_)) ||
            !resolveObjectPath(request_.object, ObjectFile::ReservationA, request_.operation, reservation_a_, sizeof(reservation_a_)) ||
            !resolveObjectPath(request_.object, ObjectFile::ReservationB, request_.operation, reservation_b_, sizeof(reservation_b_)) ||
            !resolveObjectPath(request_.object, ObjectFile::SignedManifest, request_.revision_hash, manifest_path_, sizeof(manifest_path_)) ||
            !resolveObjectPath(request_.object, ObjectFile::PublicationDirectory, request_.operation, directory_path_, sizeof(directory_path_)) ||
            !resolveObjectPath(request_.object, ObjectFile::PublicationA, request_.operation, job_a_, sizeof(job_a_)) ||
            !resolveObjectPath(request_.object, ObjectFile::PublicationB, request_.operation, job_b_, sizeof(job_b_))) return fail(Status::InvalidField);
        if (!read_only_)
        {
            const auto loaded = heads_.load(head_, head_verify_, scratch, capacity);
            if (loaded != HeadStoreResult::Ready) return fail(loaded == HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
            if (!requestMatchesHead()) return fail(Status::Conflict);
        }
        const auto reserved = reservations_.load(reservation_, reservation_verify_, scratch, capacity);
        if (reserved != ReservationStoreResult::Ready) return fail(reserved == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        if (reader_.begin(manifest_path_, signed_bytes_, sizeof(signed_bytes_)) != FileIntegrityResult::Pending) return fail(Status::StorageUnavailable);
        stage_ = Stage::Reading;
        return Status::Busy;
    }
    Status fail(Status status)
    {
        reader_.cancel();
        file_check_.cancel();
        receipt_reader_.cancel();
        manifest_.verified = false;
        stage_ = Stage::Failed;
        status_ = status;
        return status;
    }
    track_sharing::PublicationDirectory directory_, directory_verify_;
    uint8_t bindings_[112]{}, author_[64]{}, author_hash_[32]{};
    track_sharing::CommitRequest request_;

    char head_a_[160]{}, head_b_[160]{}, reservation_a_[160]{}, reservation_b_[160]{}, manifest_path_[160]{},
        directory_path_[160]{}, job_a_[160]{}, job_b_[160]{}, kml_path_[160]{}, receipt_path_[160]{};
    uint8_t signed_bytes_[4166]{}, workspace_[4200]{};
    track_sharing::ObjectHead head_, head_next_, head_verify_;
    track_sharing::RevisionReservation reservation_, reservation_verify_;
    track_sharing::PublicationJournal desired_, job_, job_verify_;
    track_sharing::VerifiedManifestView manifest_;
    track_sharing::VerifiedReceiptView receipt_;
    UploadIo& io_;
    RouteSourceHash hash_;
    LinuxTrackManifestCrypto crypto_;
    track_sharing::SignedManifestReader<UploadIo> reader_;
    track_sharing::FileIntegrityCheck<UploadIo, RouteSourceHash> file_check_;
    track_sharing::ObjectHeadStore<UploadIo> heads_;
    track_sharing::ReservationStore<UploadIo> reservations_;
    track_sharing::PublicationDirectoryStore<UploadIo> directories_;
    track_sharing::PublicationJournalStore<UploadIo> jobs_;
    LinuxTrackReceiptCrypto receipt_crypto_;
    track_sharing::PublicationReceiptLoader<UploadIo, LinuxTrackReceiptCrypto> receipt_reader_;
    Stage stage_ = Stage::Idle;
    Status status_ = Status::Busy;
    bool identity_verified_ = false;
    bool read_only_ = false;
};

#endif
} // namespace platform::linux_runtime
