#pragma once
#include "hostlink/management_envelope.h"
#include "platform/linux/hostlink_track_prepare_input.h"
#include "platform/linux/track_manifest_crypto.h"
#include "track_sharing/checked_reservation.h"
#include "track_sharing/current_parent_loader.h"
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/prepared_signature.h"
#include "track_sharing/restore_prepared_signature.h"
#include "track_sharing/signed_manifest_reader.h"
namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
using Status = ::hostlink::management::Status;
// Native orchestration of the shared checked reservation, durable signature,
// recovery and head rules. All large evidence arenas belong to this heap owner.
class HostlinkTrackPreparePersistence
{
  public:
    explicit HostlinkTrackPreparePersistence(HostlinkTrackPrepareInput& input)
        : input_(input), heads_(input.io, head_a_, head_b_), reservations_(input.io, reservation_a_, reservation_b_),
          blobs_(input.io, signed_temporary_, signed_path_),
          parent_loader_(input.io, input.hash, crypto_, heads_, head_, head_verify_, verified_, parent_path_, signed_bytes_, sizeof(signed_bytes_)) {}
    bool initialize()
    {
        if (!input_.ready() || !track_sharing::deriveManifestHashes(input_.manifest(), input_.view, input_.hash, object_, candidate_)) return false;
        using track_sharing::ObjectFile;
        const geocaching::ByteView object{object_, 32}, operation = input_.request.operation, revision{candidate_, 32};
        initialized_ = track_sharing::resolveObjectPath(object, ObjectFile::HeadA, {}, head_a_, sizeof(head_a_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::HeadB, {}, head_b_, sizeof(head_b_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::ReservationA, operation, reservation_a_, sizeof(reservation_a_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::ReservationB, operation, reservation_b_, sizeof(reservation_b_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::SignedTemporary, revision, signed_temporary_, sizeof(signed_temporary_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::SignedManifest, revision, signed_path_, sizeof(signed_path_));
        return initialized_;
    }
    template <class Signer>
    Status finish(Signer& adapter, const track_sharing::VerifiedManifestView* parent, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        complete_ = false;
        if (!initialized_ || !input_.ready() || !scratch || capacity < 448) return Status::InvalidField;
        uint8_t current_hash[32];
        uint32_t current_generation = 0;
        auto* key = scratch + capacity - 64;
        const auto identity = adapter.readAuthor(key, current_hash, current_generation);
        if (identity != Status::Ok) return identity;
        if (current_generation != input_.generation || std::memcmp(key, input_.public_key.data(), 64) || std::memcmp(current_hash, input_.actor.data(), 32)) return Status::StaleIdentity;
        if (parent_loading_)
        {
            const auto loaded_parent = parent_loader_.step(workspace_, sizeof(workspace_));
            if (loaded_parent == ParentLoadResult::Pending) return Status::Busy;
            parent_loading_ = false;
            if (loaded_parent != ParentLoadResult::Ready) return loaded_parent == ParentLoadResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            parent_ready_ = true;
        }
        const auto loaded_head = heads_.load(head_, head_verify_, scratch, capacity);
        if (loaded_head != HeadStoreResult::Ready && loaded_head != HeadStoreResult::Missing) return loaded_head == HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        const auto loaded_reservation = reservations_.load(reservation_, reservation_verify_, scratch, capacity);
        if (loaded_reservation != ReservationStoreResult::Ready && loaded_reservation != ReservationStoreResult::Missing) return loaded_reservation == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        const geocaching::ByteView author{input_.public_key.data(), 64}, author_hash{input_.actor.data(), 32};
        if (loaded_reservation == ReservationStoreResult::Ready)
        {
            if (!reservationMatchesInput()) return Status::Conflict;
            if (reservation_.phase == ReservationPhase::Checked || reservation_.phase == ReservationPhase::Signed)
            {
                if (loaded_head == HeadStoreResult::Missing)
                {
                    const auto admitted = requireFirstDirectory(&reservation_, scratch, capacity);
                    if (admitted != Status::Ok) return admitted;
                }
                const auto existing = readVerifiedSignature();
                if (existing == Status::Ok)
                {
                    const auto restored = restorePreparedSignature(input_, verified_, author, input_.generation, author_hash, reservation_, reservation_verify_, reservations_, workspace_, sizeof(workspace_), signature_, sizeof(signature_));
                    if (restored != PreparedSignatureResult::Ready) return restored == PreparedSignatureResult::StorageFailure ? Status::StorageUnavailable : restored == PreparedSignatureResult::StaleIdentity ? Status::StaleIdentity
                                                                                                                                                                                                                 : Status::Conflict;
                    return persistHead(scratch, capacity);
                }
                if (existing != Status::Expired) return existing;
                if (reservation_.phase == ReservationPhase::Signed) return Status::StorageUnavailable;
            }
        }
        if (loaded_head == HeadStoreResult::Missing)
        {
            if (parent || input_.request.expected_parent.size) return Status::Conflict;
            Status admitted;
            if (loaded_reservation == ReservationStoreResult::Missing)
                admitted = requireFirstDirectory(nullptr, scratch, capacity);
            else if (loaded_reservation == ReservationStoreResult::Ready && !std::memcmp(reservation_.object, object_, 32) && !std::memcmp(reservation_.candidate, candidate_, 32))
                admitted = requireFirstDirectory(&reservation_, scratch, capacity);
            else return loaded_reservation == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            if (admitted != Status::Ok) return admitted;
        }
        else if (!parent)
        {
            if (parent_ready_) parent = &verified_;
            else
            {
                const auto started = parent_loader_.begin({object_, 32}, input_.request.expected_parent, {input_.public_key.data(), 64}, input_.generation, scratch, capacity);
                if (started != ParentLoadResult::Pending) return started == ParentLoadResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
                parent_loading_ = true;
                return Status::Busy;
            }
        }
        const auto directory = ensureDirectory();
        if (directory != Status::Ok) return directory;
        const auto checked = persistCheckedReservation(input_, input_.request, input_.manifest(), author, author_hash, input_.generation,
                                                       loaded_head == HeadStoreResult::Missing ? nullptr : &head_, parent, input_.hash, reservations_, input_.view,
                                                       desired_, reservation_, reservation_verify_, scratch, capacity);
        if (checked != ReservationStoreResult::Ready) return checked == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        const auto signed_result = persistPreparedSignature(input_, input_.manifest(), author, input_.generation, author_hash, reservation_, reservation_verify_,
                                                            input_.hash, adapter, blobs_, reservations_, input_.view, workspace_, sizeof(workspace_), signature_, sizeof(signature_));
        if (signed_result != PreparedSignatureResult::Ready) return signed_result == PreparedSignatureResult::StaleIdentity ? Status::StaleIdentity : signed_result == PreparedSignatureResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                                  : signed_result == PreparedSignatureResult::Unavailable      ? Status::Busy
                                                                                                                                                                                                               : Status::Conflict;
        const auto verified = readVerifiedSignature();
        if (verified != Status::Ok) return verified == Status::Expired ? Status::StorageUnavailable : verified;
        return persistHead(scratch, capacity);
    }
    bool ready() const { return complete_ && input_.ready(); }
    const track_sharing::ObjectHead& head() const { return head_; }
    geocaching::ByteView signature() const { return complete_ ? geocaching::ByteView{signature_, sizeof(signature_)} : geocaching::ByteView{}; }

  private:
    Status ensureDirectory()
    {
        std::array<char, track_sharing::kObjectPathCapacity> path{};
        if (!track_sharing::resolveObjectPath({object_, 32}, track_sharing::ObjectFile::Directory, {}, path.data(), path.size())) return Status::InvalidField;
        return input_.io.mkdirs(path.data()) ? Status::Ok : Status::StorageUnavailable;
    }
    Status requireFirstDirectory(const track_sharing::RevisionReservation* reservation, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (!input_.io.current()) return Status::StorageUnavailable;
        if (reservation)
        {
            if (!reservationValid(*reservation) || reservation->revision != 1 || input_.request.expected_parent.size ||
                reservation->generation != input_.generation || std::memcmp(reservation->author, input_.actor.data(), 32) ||
                std::memcmp(reservation->operation, input_.request.operation.data, 16)) return Status::Conflict;
            const auto checked = reservations_.validateCurrent(*reservation, reservation_verify_, scratch, capacity);
            if (checked != ReservationStoreResult::Ready) return checked == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        }
        std::array<char, kObjectPathCapacity> path{};
        if (!resolveObjectPath({object_, 32}, ObjectFile::Directory, {}, path.data(), path.size())) return Status::InvalidField;
        unsigned seen = 0;
        for (unsigned position = 0; position <= 4; position += 4)
        {
            std::array<UploadIo::ScanEntry, 4> entries;
            size_t count = 0;
            bool ended = false;
            const auto status = input_.io.scan(path.data(), position, entries, count, ended);
            if (status == UploadIo::ScanStatus::Missing) return reservation ? Status::StorageUnavailable : Status::Ok;
            if (status != UploadIo::ScanStatus::Ready) return Status::StorageUnavailable;
            for (size_t i = 0; i < count; ++i)
            {
                if (!reservation || !entries[i].regular || ++seen > 4) return Status::Conflict;
                const auto& name = entries[i].name;
                const size_t bytes = name.size() == 36 ? 16 : name.size() == 68 ? 32
                                                                                : 0;
                if (!bytes) return Status::Conflict;
                const auto* key = bytes == 16 ? reservation->operation : reservation->candidate;
                constexpr char hex[] = "0123456789abcdef";
                for (size_t j = 0; j < bytes; ++j)
                    if (name[2 * j] != hex[key[j] >> 4] || name[2 * j + 1] != hex[key[j] & 15]) return Status::Conflict;
                const auto suffix = name.substr(bytes * 2);
                if (bytes == 16 ? suffix != ".rva" && suffix != ".rvb" : suffix != ".tmp" && suffix != ".cmp") return Status::Conflict;
            }
            if (ended) return input_.io.current() ? Status::Ok : Status::StorageUnavailable;
        }
        return Status::Conflict;
    }
    bool reservationMatchesInput() const
    {
        if (reservation_.generation != input_.generation || reservation_.revision != input_.view.revision ||
            std::memcmp(reservation_.operation, input_.request.operation.data, 16) || std::memcmp(reservation_.object, object_, 32) ||
            std::memcmp(reservation_.candidate, candidate_, 32) || std::memcmp(reservation_.author, input_.actor.data(), 32)) return false;
        return reservation_.revision == 1 ? input_.view.previous.size == 0 : input_.view.previous.size == 32 && !std::memcmp(reservation_.parent, input_.view.previous.data, 32);
    }
    Status readVerifiedSignature()
    {
        using namespace track_sharing;
        const auto probe = input_.io.open(signed_path_, UploadIo::Mode::Read);
        if (!probe.handle) return probe.status == UploadIo::OpenStatus::Missing ? Status::Expired : Status::StorageUnavailable;
        if (!input_.io.close(probe.handle)) return Status::StorageUnavailable;
        SignedManifestReader<UploadIo> reader(input_.io);
        if (reader.begin(signed_path_, signed_bytes_, sizeof(signed_bytes_)) != FileIntegrityResult::Pending) return Status::StorageUnavailable;
        FileIntegrityResult read;
        do
        {
            read = reader.step();
        } while (read == FileIntegrityResult::Pending);
        if (read != FileIntegrityResult::Ready) return Status::StorageUnavailable;
        return verifyManifest(reader.bytes(), crypto_, input_.hash, workspace_, sizeof(workspace_), verified_, {input_.public_key.data(), 64}) == ManifestVerification::Valid ? Status::Ok : Status::Conflict;
    }
    Status persistHead(uint8_t* scratch, size_t capacity)
    {
        const auto persisted = track_sharing::persistSignedObjectHead(heads_, verified_, reservation_, input_.generation, {input_.public_key.data(), 64}, {input_.actor.data(), 32},
                                                                      head_, head_next_, head_verify_, scratch, capacity);
        if (persisted != track_sharing::HeadStoreResult::Ready) return persisted == track_sharing::HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        complete_ = true;
        return Status::Ok;
    }
    HostlinkTrackPrepareInput& input_;
    uint8_t object_[32]{}, candidate_[32]{}, signature_[64]{}, workspace_[4200]{}, signed_bytes_[4166]{};
    char head_a_[160]{}, head_b_[160]{}, reservation_a_[160]{}, reservation_b_[160]{}, signed_temporary_[160]{}, signed_path_[160]{}, parent_path_[160]{};
    track_sharing::ObjectHead head_, head_next_, head_verify_;
    track_sharing::RevisionReservation desired_, reservation_, reservation_verify_;
    track_sharing::VerifiedManifestView verified_;
    LinuxTrackManifestCrypto crypto_;
    track_sharing::ObjectHeadStore<UploadIo> heads_;
    track_sharing::ReservationStore<UploadIo> reservations_;
    track_sharing::SignedManifestStore<UploadIo> blobs_;
    track_sharing::CurrentParentLoader<UploadIo, RouteSourceHash,
                                       LinuxTrackManifestCrypto, track_sharing::ObjectHeadStore<UploadIo>>
        parent_loader_;
    bool initialized_ = false, complete_ = false, parent_loading_ = false, parent_ready_ = false;
};

#endif
} // namespace platform::linux_runtime
