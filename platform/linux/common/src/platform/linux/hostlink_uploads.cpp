#include "platform/linux/hostlink_uploads.h"
#include "hostlink_upload_io.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "route_image_source_io.h"
#include "track_sharing/upload_protocol.h"
#include "track_sharing/upload_resources.h"
#include "track_sharing/upload_session.h"
#include <array>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace
{
namespace ts = track_sharing;
using Status = ::hostlink::management::Status;
using Operation = ::hostlink::management::Operation;
bool nonzero(const uint8_t* bytes, size_t count)
{
    uint8_t bits = 0;
    if (bytes)
        for (size_t i = 0; i < count; ++i) bits |= bytes[i];
    return bits != 0;
}
Status journal_status(ts::JournalStoreResult result)
{
    return result == ts::JournalStoreResult::Ready ? Status::Ok : result == ts::JournalStoreResult::Conflict ? Status::Conflict
                                                              : result == ts::JournalStoreResult::Invalid    ? Status::InvalidField
                                                                                                             : Status::StorageUnavailable;
}
bool journal_id(std::string_view name, uint8_t id[16])
{
    if (name.size() != 36 || (name.substr(32) != ".uja" && name.substr(32) != ".ujb")) return false;
    const auto digit = [](char c)
    { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                   : -1; };
    for (unsigned i = 0; i < 16; ++i)
    {
        const int high = digit(name[i * 2]), low = digit(name[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        id[i] = uint8_t(high * 16 + low);
    }
    return nonzero(id, 16);
}
Status remove_bound_files(UploadIo& io, const ts::UploadJournal& metadata, ts::UploadKind kind)
{
    for (const auto file : {ts::UploadFile::Candidate, ts::UploadFile::JournalA, ts::UploadFile::JournalB})
    {
        char path[64]{};
        if (!ts::resolveUploadPath(metadata.id, 16, file, path, sizeof(path), kind)) return Status::Internal;
        const auto existing = io.open(path, UploadIo::Mode::Read);
        if (!existing.handle)
        {
            if (existing.status == UploadIo::OpenStatus::Missing) continue;
            return Status::StorageUnavailable;
        }
        if (!io.close(existing.handle) || !io.remove(path)) return Status::StorageUnavailable;
    }
    return io.current() ? Status::Ok : Status::StorageUnavailable;
}
struct Owner
{
    using Resources = ts::UploadResources<UploadIo, RouteSourceHash>;
    Owner(const std::filesystem::path& root, const ts::UploadRequest& request, const uint8_t* session_id,
          uint32_t epoch, const uint8_t* author_hash, uint64_t now)
        : generation(epoch), kind(request.kind), io(root), resources(io, hash, request.id.data, generation, author.data(), kind),
          session(resources, session_id, 1, now), recheck(io, hash)
    {
        std::copy_n(author_hash, 32, author.begin());
        std::copy_n(session_id, 16, session_id_.begin());
    }
    ~Owner()
    {
        resources.cancel();
        recheck.cancel();
    }
    uint32_t generation;
    ts::UploadKind kind;
    std::array<uint8_t, 32> author{};
    std::array<uint8_t, 16> session_id_{};
    UploadIo io;
    RouteSourceHash hash;
    Resources resources;
    ts::UploadSession<Resources> session;
    ts::FileIntegrityCheck<UploadIo, RouteSourceHash> recheck;
    Status failure = Status::Ok;
    bool revalidating = false, discarding = false;
};
} // namespace
struct HostlinkUploads::Impl
{
    std::filesystem::path root, database;
    std::unique_ptr<Owner> owner;
    std::array<uint8_t, 4096> scratch{};
    bool identity(uint32_t& generation, std::array<uint8_t, 32>& author)
    {
        LinuxReticulumIdentity current(database);
        LinuxGeocachingCrypto crypto;
        if (!current.begin() || !crypto.sha256({current.publicKey().data(), 64}, author.data())) return false;
        generation = current.generation();
        return generation && nonzero(author.data(), author.size());
    }
    bool refresh()
    {
        if (!owner) return false;
        return identity(owner->generation, owner->author);
    }
    Status create(const ts::UploadRequest& request, const uint8_t* session, uint64_t now, uint32_t generation, const uint8_t* author)
    {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error) return Status::StorageUnavailable;
        auto candidate = std::make_unique<Owner>(root, request, session, generation, author, now);
        if (!candidate->io.current()) return Status::StorageUnavailable;
        if (!candidate->io.lock()) return Status::Busy;
        if (!candidate->io.mkdirs(ts::uploadDirectory(request.kind))) return Status::StorageUnavailable;
        auto& resources = candidate->resources;
        const auto recovered = resources.recover(scratch.data(), scratch.size());
        if (recovered == ts::JournalStoreResult::Missing)
        {
            // Quota includes all unknown/orphan entries before reserving the
            // candidate and two journal slots. No implicit orphan truncation.
            if (!candidate->io.admit(request.kind, request.total)) return Status::LimitExceeded;
            const auto file = candidate->io.open(resources.candidatePath(), UploadIo::Mode::Exclusive);
            if (!file.handle) return Status::Conflict;
            const bool synced = candidate->io.sync(file.handle), closed = candidate->io.close(file.handle);
            if (!synced || !closed) return Status::StorageUnavailable;
            const auto initialized = resources.initialize(request.total, request.hash.data, scratch.data(), scratch.size());
            if (initialized != ts::JournalStoreResult::Ready) return journal_status(initialized);
        }
        else
        {
            if (recovered != ts::JournalStoreResult::Ready) return journal_status(recovered);
            const auto& metadata = resources.metadata();
            if (metadata.total != request.total || std::memcmp(metadata.file_hash, request.hash.data, 32)) return Status::Conflict;
            const auto file = candidate->io.open(resources.candidatePath(), UploadIo::Mode::Read);
            uint64_t bytes = 0;
            const bool sized = file.handle && candidate->io.size(file.handle, bytes);
            const bool closed = file.handle && candidate->io.close(file.handle);
            if (!sized || !closed || bytes < metadata.durable_offset || bytes > metadata.total) candidate->failure = Status::StorageUnavailable;
            else if (metadata.phase == ts::UploadPhase::Failed) candidate->failure = Status::HashMismatch;
            else if (metadata.phase == ts::UploadPhase::Sealed)
            {
                // A CRC-valid Sealed journal is never fresh proof of bytes.
                candidate->revalidating = candidate->recheck.begin(resources.candidatePath(), metadata.total, metadata.file_hash) == ts::FileIntegrityResult::Pending;
                if (!candidate->revalidating) candidate->failure = Status::StorageUnavailable;
            }
        }
        owner = std::move(candidate);
        return owner->failure;
    }
    Status progress(uint8_t* output, size_t capacity, size_t& written)
    {
        auto metadata = owner->resources.metadata();
        if (owner->failure != Status::Ok) return owner->failure;
        if (owner->revalidating) metadata.phase = ts::UploadPhase::Checking;
        return ts::encodeUploadProgress(metadata, output, capacity, written) ? Status::Ok : Status::Internal;
    }
    Status discard(uint8_t* output, size_t& written)
    {
        owner->discarding = true;
        owner->resources.cancel();
        owner->recheck.cancel();
        const auto result = remove_bound_files(owner->io, owner->resources.metadata(), owner->kind);
        if (result != Status::Ok) return result;
        owner.reset();
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    Status cleanup(const ::hostlink::management::EnvelopeView& request, uint64_t now,
                   uint8_t* output, size_t& written)
    {
        ts::UploadRequest decoded;
        if (request.body_size > 128 || !nonzero(request.session, 16) ||
            !ts::decodeUploadRequest(ts::UploadCommand::Begin, {request.body, request.body_size}, decoded)) return Status::InvalidField;
        uint32_t generation = 0;
        std::array<uint8_t, 32> author{};
        if (!identity(generation, author)) return Status::StorageUnavailable;
        if (owner)
        {
            const auto& metadata = owner->resources.metadata();
            if (std::memcmp(decoded.id.data, metadata.id, 16)) return Status::Busy;
            if (decoded.kind != owner->kind || decoded.total != metadata.total || std::memcmp(decoded.hash.data, metadata.file_hash, 32)) return Status::Conflict;
            if (generation != metadata.generation || std::memcmp(author.data(), metadata.author_hash, 32)) return Status::StaleIdentity;
            if (!owner->session.authorize(request.session, 1, now)) return Status::Unauthorized;
            return discard(output, written);
        }
        UploadIo io(root);
        if (!io.current()) return Status::StorageUnavailable;
        if (!io.lock()) return Status::Busy;
        RouteSourceHash hash;
        Owner::Resources resources(io, hash, decoded.id.data, generation, author.data(), decoded.kind);
        const auto recovered = resources.recover(scratch.data(), scratch.size());
        if (recovered == ts::JournalStoreResult::Missing)
        {
            const auto candidate = io.open(resources.candidatePath(), UploadIo::Mode::Read);
            if (candidate.handle)
            {
                const bool closed = io.close(candidate.handle);
                return closed ? Status::Conflict : Status::StorageUnavailable;
            }
            if (candidate.status != UploadIo::OpenStatus::Missing) return Status::StorageUnavailable;
        }
        else
        {
            if (recovered != ts::JournalStoreResult::Ready)
                return recovered == ts::JournalStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            const auto& metadata = resources.metadata();
            if (metadata.total != decoded.total || std::memcmp(metadata.file_hash, decoded.hash.data, 32)) return Status::Conflict;
            const auto removed = remove_bound_files(io, metadata, decoded.kind);
            if (removed != Status::Ok) return removed;
        }
        if (!io.current()) return Status::StorageUnavailable;
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    Status list(const ::hostlink::management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
    {
        if (owner) return Status::Busy;
        if (!nonzero(request.session, 16) || request.body_size > 128) return Status::InvalidField;
        geocaching::protocol::CmpReader reader({request.body, request.body_size});
        size_t fields = 0;
        uint64_t raw_kind = 0, wanted = 0, position = 0;
        if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(raw_kind) || raw_kind > UINT8_MAX ||
            !ts::uploadByteLimit(static_cast<ts::UploadKind>(raw_kind))) return Status::InvalidField;
        const bool first = reader.position() < request.body_size && request.body[reader.position()] == 0xc0;
        if (first)
        {
            if (!reader.nil()) return Status::InvalidField;
        }
        else if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(wanted) || wanted > UINT32_MAX ||
                 !reader.unsignedInteger(position) || position > 128U * 1024U * 1024U || position % 32) return Status::InvalidField;
        if (!reader.finished()) return Status::InvalidField;
        uint32_t generation = 0;
        std::array<uint8_t, 32> author{};
        if (!identity(generation, author)) return Status::StorageUnavailable;
        UploadIo io(root);
        if (!io.current()) return Status::StorageUnavailable;
        const auto media = io.media_token(request.session, generation, author.data(), ts::uploadDirectory(static_cast<ts::UploadKind>(raw_kind)));
        if (!media) return Status::StorageUnavailable;
        if (!first && wanted != media) return Status::Conflict;
        if (!io.lock()) return Status::Busy;
        const auto kind = static_cast<ts::UploadKind>(raw_kind);
        std::array<UploadIo::ScanEntry, 4> entries;
        size_t count = 0;
        bool ended = false;
        const auto scanned = io.scan(ts::uploadDirectory(kind), uint32_t(position / 32), entries, count, ended);
        if (scanned == UploadIo::ScanStatus::Unavailable) return Status::StorageUnavailable;
        if (scanned == UploadIo::ScanStatus::Invalid) return Status::Conflict;
        if (scanned == UploadIo::ScanStatus::Missing)
        {
            if (position) return Status::Conflict;
            ended = true;
        }
        uint8_t id[16]{};
        bool found = false;
        RouteSourceHash hash;
        std::unique_ptr<Owner::Resources> resources;
        for (size_t index = 0; index < count; ++index)
        {
            position += 32;
            if (!entries[index].regular || !journal_id(entries[index].name, id)) continue;
            ended = false; // A matching name consumed one live cursor row.
            resources = std::make_unique<Owner::Resources>(io, hash, id, generation, author.data(), kind);
            const auto loaded = resources->recover(scratch.data(), scratch.size());
            if (loaded == ts::JournalStoreResult::Conflict) break; // Other identity stays untouched.
            if (loaded != ts::JournalStoreResult::Ready)
                return loaded == ts::JournalStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            found = true;
            break;
        }
        if (!io.current()) return Status::StorageUnavailable;
        if (io.media_token(request.session, generation, author.data(), ts::uploadDirectory(kind)) != media) return Status::Conflict;
        geocaching::protocol::CmpWriter writer(output, capacity);
        if (!writer.array(3) || !writer.unsignedInteger(media) || !(ended ? writer.nil() : writer.unsignedInteger(position))) return Status::Internal;
        if (found)
        {
            const auto& metadata = resources->metadata();
            if (!writer.array(7) || !writer.binary({metadata.id, 16}) || !writer.unsignedInteger(raw_kind) || !writer.unsignedInteger(metadata.total) ||
                !writer.binary({metadata.file_hash, 32}) || !writer.unsignedInteger(metadata.generation) ||
                !writer.unsignedInteger(uint8_t(metadata.phase)) || !writer.unsignedInteger(metadata.durable_offset)) return Status::Internal;
        }
        else if (!writer.nil()) return Status::Internal;
        written = writer.size();
        return Status::Ok;
    }
};
HostlinkUploads::HostlinkUploads(std::filesystem::path root, std::filesystem::path database) : impl_(std::make_unique<Impl>())
{
    impl_->root = std::move(root);
    impl_->database = std::move(database);
}
HostlinkUploads::~HostlinkUploads() = default;
bool HostlinkUploads::supported() { return true; }
bool HostlinkUploads::identityBusy() const { return bool(impl_->owner); }

void HostlinkUploads::advance(uint64_t now, bool connected)
{
    auto& state = *impl_;
    if (!state.owner) return;
    auto& owner = *state.owner;
    if (!owner.session.matches(owner.session_id_.data(), 1, now))
    {
        state.owner.reset();
        return;
    }
    if (!owner.io.current() || !connected)
    {
        owner.session.advance(1, now, false, state.scratch.data(), state.scratch.size());
        state.owner.reset();
        return;
    }
    const bool hashing = owner.revalidating || owner.resources.sealResult() == ts::UploadSealResult::Pending;
    if (hashing && !state.refresh()) return; // No progress under unavailable identity.
    const auto& metadata = owner.resources.metadata();
    if (owner.generation != metadata.generation || std::memcmp(owner.author.data(), metadata.author_hash, 32))
    {
        owner.resources.cancel();
        owner.recheck.cancel();
        owner.failure = Status::StaleIdentity;
        return;
    }
    for (unsigned step = 0; step < 256; ++step)
    {
        const auto result = owner.session.advance(1, now, true, state.scratch.data(), state.scratch.size());
        if (owner.session.releasable())
        {
            state.owner.reset();
            return;
        }
        if (owner.revalidating)
        {
            const auto checked = owner.recheck.step(state.scratch.data(), state.scratch.size());
            if (checked == ts::FileIntegrityResult::Pending) continue;
            owner.revalidating = false;
            if (checked != ts::FileIntegrityResult::Ready)
                owner.failure = checked == ts::FileIntegrityResult::HashMismatch ? Status::HashMismatch : Status::StorageUnavailable;
            break;
        }
        if (result == ts::UploadSealResult::Pending) continue;
        if (result == ts::UploadSealResult::HashMismatch) owner.failure = Status::HashMismatch;
        else if (result == ts::UploadSealResult::Conflict) owner.failure = Status::StaleIdentity;
        else if (result == ts::UploadSealResult::StorageFailure) owner.failure = Status::StorageUnavailable;
        break;
    }
}
void HostlinkUploads::close(const uint8_t* session)
{
    if (impl_->owner && impl_->owner->session.close(session)) impl_->owner.reset();
}
::hostlink::management::Status HostlinkUploads::prepare_install(const uint8_t* session, const ts::PrepareRequest& request, uint64_t now, const uint8_t actor[32])
{
    advance(now, true);
    if (!impl_->owner) return Status::Ok;
    if (request.query_request) return Status::Busy;
    const auto& owner = *impl_->owner;
    const auto& metadata = owner.resources.metadata();
    if (owner.discarding || owner.revalidating || owner.failure != Status::Ok || metadata.phase != ts::UploadPhase::Sealed ||
        !actor || request.generation != metadata.generation || std::memcmp(actor, metadata.author_hash, 32)) return Status::Busy;
    if (!impl_->owner->session.matches(session, 1, now)) return Status::Busy;
    bool bound = false;
    for (const auto upload : {request.kml_upload, request.manifest_upload, request.package_upload})
        if (upload.data && upload.size == 16 && !std::memcmp(upload.data, metadata.id, 16)) bound = true;
    if (!bound) return Status::Busy;
    impl_->owner.reset();
    return Status::Ok;
}
::hostlink::management::Status HostlinkUploads::prepare_confirmation(const uint8_t* session, const ts::IpConfirmationRequest& request, uint64_t now, const uint8_t actor[32])
{
    advance(now, true);
    if (!impl_->owner) return Status::Ok;
    const auto& owner = *impl_->owner;
    const auto& metadata = owner.resources.metadata();
    if (owner.discarding || owner.revalidating || owner.failure != Status::Ok || metadata.phase != ts::UploadPhase::Sealed ||
        !actor || request.generation != metadata.generation || std::memcmp(actor, metadata.author_hash, 32) ||
        !impl_->owner->session.matches(session, 1, now)) return Status::Busy;
    const bool receipt = owner.kind == ts::UploadKind::IpReceipt && !std::memcmp(metadata.id, request.transfer.data, 16) &&
                         !std::memcmp(metadata.file_hash, request.proof_hash.data, 32);
    const bool manifest = request.manifest_transfer.size == 16 && owner.kind == ts::UploadKind::Manifest &&
                          !std::memcmp(metadata.id, request.manifest_transfer.data, 16) && !std::memcmp(metadata.file_hash, request.manifest_hash.data, 32);
    if (!receipt && !manifest) return Status::Busy;
    impl_->owner.reset();
    return Status::Ok;
}
::hostlink::management::Status HostlinkUploads::process(const ::hostlink::management::EnvelopeView& request, uint64_t now,
                                                        uint8_t* output, size_t capacity, size_t& written)
try
{
    written = 0;
    if (!output || capacity < 128) return Status::Internal;
    if (request.code == uint16_t(Operation::TransferCleanup) || request.code == uint16_t(Operation::TransferListOwned))
    {
        advance(now, true);
        return request.code == uint16_t(Operation::TransferCleanup) ? impl_->cleanup(request, now, output, written)
                                                                    : impl_->list(request, output, capacity, written);
    }
    ts::UploadCommand command;
    if (request.code == uint16_t(Operation::TransferBegin)) command = ts::UploadCommand::Begin;
    else if (request.code == uint16_t(Operation::TransferChunk)) command = ts::UploadCommand::Chunk;
    else if (request.code == uint16_t(Operation::TransferStatus)) command = ts::UploadCommand::Status;
    else if (request.code == uint16_t(Operation::TransferFinish) || request.code == uint16_t(Operation::TransferAbort)) command = ts::UploadCommand::Finish;
    else return Status::Unsupported;
    ts::UploadRequest decoded;
    if (!ts::decodeUploadRequest(command, {request.body, request.body_size}, decoded) || !nonzero(request.session, 16)) return Status::InvalidField;
    auto& state = *impl_;
    advance(now, true);
    uint32_t generation = 0;
    std::array<uint8_t, 32> author{};
    if (!state.identity(generation, author)) return Status::StorageUnavailable;
    if (state.owner)
    {
        auto& owner = *state.owner;
        owner.generation = generation;
        owner.author = author;
        const auto& metadata = owner.resources.metadata();
        if (generation != metadata.generation || std::memcmp(author.data(), metadata.author_hash, 32)) return Status::StaleIdentity;
        if (!owner.session.matches(request.session, 1, now)) return Status::Unauthorized;
        if (command == ts::UploadCommand::Begin && std::memcmp(decoded.id.data, metadata.id, 16))
        {
            if (metadata.phase != ts::UploadPhase::Sealed || owner.revalidating || owner.failure != Status::Ok || owner.discarding) return Status::Conflict;
            state.owner.reset();
        }
    }
    if (!state.owner)
    {
        if (command != ts::UploadCommand::Begin) return Status::Expired;
        const auto created = state.create(decoded, request.session, now, generation, author.data());
        if (created != Status::Ok) return created;
    }
    auto& owner = *state.owner;
    const auto& metadata = owner.resources.metadata();
    if (std::memcmp(decoded.id.data, metadata.id, 16)) return Status::Conflict;
    if (!owner.session.authorize(request.session, 1, now)) return Status::Unauthorized;
    if (request.code == uint16_t(Operation::TransferAbort))
    {
        return state.discard(output, written);
    }
    if (owner.discarding) return Status::Conflict;
    if (owner.failure != Status::Ok) return owner.failure;
    if (command == ts::UploadCommand::Begin)
    {
        if (decoded.kind != owner.kind || decoded.total != metadata.total || std::memcmp(decoded.hash.data, metadata.file_hash, 32)) return Status::Conflict;
    }
    else if (command == ts::UploadCommand::Chunk)
    {
        const auto result = owner.session.append(request.session, 1, now, decoded.id.data, decoded.offset, decoded.chunk.data, decoded.chunk.size,
                                                 state.scratch.data(), state.scratch.size());
        if (result != ts::JournalStoreResult::Ready) return journal_status(result);
    }
    else if (command == ts::UploadCommand::Finish && metadata.phase != ts::UploadPhase::Sealed && !owner.resources.sealingStarted())
    {
        const auto result = owner.session.beginSeal(request.session, 1, now, state.scratch.data(), state.scratch.size());
        if (result != ts::UploadSealResult::Pending)
            return result == ts::UploadSealResult::Conflict ? Status::Conflict : Status::StorageUnavailable;
    }
    return state.progress(output, capacity, written);
}
catch (...)
{
    written = 0;
    return Status::StorageUnavailable;
}
#else
struct HostlinkUploads::Impl
{
};
HostlinkUploads::HostlinkUploads(std::filesystem::path, std::filesystem::path) : impl_(std::make_unique<Impl>()) {}
HostlinkUploads::~HostlinkUploads() = default;
bool HostlinkUploads::supported() { return false; }
::hostlink::management::Status HostlinkUploads::prepare_confirmation(const uint8_t*, const track_sharing::IpConfirmationRequest&, uint64_t, const uint8_t*)
{
    return ::hostlink::management::Status::Unsupported;
}
bool HostlinkUploads::identityBusy() const { return false; }
void HostlinkUploads::advance(uint64_t, bool) {}
void HostlinkUploads::close(const uint8_t*) {}
::hostlink::management::Status HostlinkUploads::prepare_install(const uint8_t*, const track_sharing::PrepareRequest&, uint64_t, const uint8_t*)
{
    return ::hostlink::management::Status::Unsupported;
}
::hostlink::management::Status HostlinkUploads::process(const ::hostlink::management::EnvelopeView&, uint64_t,
                                                        uint8_t*, size_t, size_t& written)
{
    written = 0;
    return ::hostlink::management::Status::Unsupported;
}
#endif
} // namespace platform::linux_runtime
