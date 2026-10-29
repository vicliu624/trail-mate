#include "platform/linux/hostlink_track_snapshots.h"
#include "hostlink/snapshot_container.h"
#include "hostlink/snapshot_space_budget.h"
#include "hostlink/track_snapshot_selection.h"
#include "platform/linux/hostlink_upload_io.h"
#include <algorithm>
#include <array>
#include <cstring>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace management = ::hostlink::management;
namespace container = management::snapshot_storage;
using Status = management::Status;
using Operation = management::Operation;
std::string snapshot_path(const uint8_t id[16], const char* suffix)
{
    std::string result = "/trailmate/web-tracks/";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned index = 0; index < 16; ++index)
    {
        result += hex[id[index] >> 4];
        result += hex[id[index] & 15];
    }
    return result + suffix;
}
bool read_id(const management::EnvelopeView& request, std::array<uint8_t, 16>& id)
{
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    geocaching::ByteView raw;
    if (!reader.array(fields, 1) || fields != 1 || !reader.binary(raw, 16) || raw.size != 16 || !reader.finished()) return false;
    uint8_t bits = 0;
    for (unsigned index = 0; index < 16; ++index) bits |= (id[index] = raw.data[index]);
    return bits != 0;
}
uint32_t catalog_media(UploadIo& io, const uint8_t* session)
{
    const std::array<uint8_t, 32> unused{};
    return io.media_token(session, 0, unused.data(), "/tracks");
}
#if defined(TRAIL_MATE_HAS_OPENSSL)
struct Snapshot final : management::TrackSnapshotStorage
{
    Snapshot(const std::filesystem::path& root, const management::TrackSnapshotSelection& selection)
        : source(root), destination(root), filename(selection.filename), logical("/tracks/" + filename),
          temporary(snapshot_path(selection.id, ".new")), published(snapshot_path(selection.id, ".tsn")),
          media(selection.media_session), builder(*this), task(builder) {}
    ~Snapshot() override { abandonForStop(); }
    bool same(const management::TrackSnapshotSelection& selection) const
    {
        return filename == selection.filename && media == selection.media_session &&
               task.metadata().bytes == selection.bytes && task.metadata().format == selection.format;
    }
    bool source_valid()
    {
        UploadIo::FileStamp current;
        return source.still_named(logical.c_str()) && source.stamp(current) && current == stamp;
    }
    management::SnapshotStorageResult begin(const uint8_t id[16], uint32_t bytes, uint8_t format) override
    {
        using Result = management::SnapshotStorageResult;
        if (!destination.current() || !destination.lock()) return Result::Busy;
        const auto input = source.open(logical.c_str(), UploadIo::Mode::Read);
        if (!input.handle || !source.stamp(stamp) || uint64_t(stamp.bytes) != bytes) return Result::Failed;
        if (!source.lock_open(input.handle))
        {
            release();
            return Result::Busy;
        }
        if (!source_valid() || catalog_media(source, request_session.data()) != media || !destination.mkdirs("/trailmate/web-tracks") ||
            !destination.admit_copy(temporary.c_str(), published.c_str(), bytes + container::kSnapshotContainerHeaderBytes,
                                    management::SnapshotSpaceBudget::kBytes, management::SnapshotSpaceBudget::kFiles)) return Result::Failed;
        const auto existing = destination.open(published.c_str(), UploadIo::Mode::Read);
        if (existing.status != UploadIo::OpenStatus::Missing)
        {
            destination.close(existing.handle);
            return Result::Failed;
        }
        const auto output = destination.open(temporary.c_str(), UploadIo::Mode::Exclusive);
        if (!output.handle) return Result::Failed;
        candidate = true;
        metadata = {};
        std::copy_n(id, 16, metadata.id);
        metadata.bytes = bytes;
        metadata.format = format;
        hash = EVP_MD_CTX_new();
        if (!hash || EVP_DigestInit_ex(hash, EVP_sha256(), nullptr) != 1 ||
            destination.write(output.handle, container::kEmptySnapshotHeader, container::kSnapshotContainerHeaderBytes) != container::kSnapshotContainerHeaderBytes) return Result::Failed;
        return Result::Ok;
    }
    management::SnapshotStorageResult copy(uint32_t offset, uint8_t* scratch, size_t length) override
    {
        using Result = management::SnapshotStorageResult;
        if (!candidate || !hash || !source_valid() || !destination.still_named(temporary.c_str()) ||
            !source.seek(&source, offset) || source.read(&source, scratch, length) != length || !source_valid() ||
            !destination.seek(&destination, container::kSnapshotContainerHeaderBytes + offset) ||
            destination.write(&destination, scratch, length) != length || EVP_DigestUpdate(hash, scratch, length) != 1) return Result::Failed;
        return Result::Ok;
    }
    management::SnapshotStorageResult finish(management::TrackSnapshotMetadata& output) override
    {
        using Result = management::SnapshotStorageResult;
        unsigned size = 0;
        uint64_t bytes = 0;
        if (!candidate || !hash || !source_valid() || !destination.still_named(temporary.c_str()) ||
            !destination.size(&destination, bytes) || bytes != container::kSnapshotContainerHeaderBytes + metadata.bytes) return Result::Failed;
        if (!copied_hash)
        {
            if (EVP_DigestFinal_ex(hash, metadata.hash, &size) != 1 || size != 32 || EVP_DigestInit_ex(hash, EVP_sha256(), nullptr) != 1) return Result::Failed;
            copied_hash = true;
        }
        // Filesystem timestamps can be identical for successive same-size
        // writes. Verify the complete source again in bounded slices before
        // sealing, even when inode/size/mtime/ctime still match.
        if (verified < metadata.bytes)
        {
            std::array<uint8_t, 4096> scratch;
            const auto length = std::min<size_t>(scratch.size(), metadata.bytes - verified);
            if (!source.seek(&source, verified) || source.read(&source, scratch.data(), length) != length ||
                !source_valid() || EVP_DigestUpdate(hash, scratch.data(), length) != 1) return Result::Failed;
            verified += uint32_t(length);
            return Result::Busy;
        }
        std::array<uint8_t, 32> verified_hash{};
        if (EVP_DigestFinal_ex(hash, verified_hash.data(), &size) != 1 || size != 32 ||
            std::memcmp(verified_hash.data(), metadata.hash, 32) ||
            !container::writeSnapshotContainerHeader(destination, &destination, metadata) || !destination.sync(&destination) ||
            !destination.close(&destination) || !source_valid() || !destination.publish(temporary.c_str(), published.c_str())) return Result::Failed;
        candidate = false;
        output = metadata;
        output.sealed = true;
        release();
        return Result::Ok;
    }
    management::SnapshotStorageResult abort() override
    {
        // After a media change, leave only an unsealed candidate on the old
        // medium. Never resolve its pathname against a newly mounted root.
        if (candidate && destination.current())
        {
            const auto opened = destination.open(temporary.c_str(), UploadIo::Mode::Read);
            if (opened.status == UploadIo::OpenStatus::Missing) candidate = false;
            else
            {
                // The original destination may still be open. In that case
                // open() refuses it; still_named verifies that owned handle.
                if (!destination.still_named(temporary.c_str()) || !destination.remove(temporary.c_str())) return management::SnapshotStorageResult::Failed;
                candidate = false;
            }
        }
        release();
        return management::SnapshotStorageResult::Ok;
    }
    bool abandonForStop() override
    {
        release();
        return true;
    }
    void release()
    {
        if (hash) EVP_MD_CTX_free(hash);
        hash = nullptr;
        source.close(nullptr);
        destination.close(nullptr);
        destination.unlock();
    }
    UploadIo source, destination;
    std::string filename, logical, temporary, published;
    UploadIo::FileStamp stamp;
    std::array<uint8_t, 16> request_session{};
    management::TrackSnapshotMetadata metadata;
    uint32_t media;
    uint64_t touched = 0;
    bool candidate = false;
    bool copied_hash = false;
    uint32_t verified = 0;
    EVP_MD_CTX* hash = nullptr;
    management::TrackSnapshotBuilder builder;
    management::SnapshotBuildSession task;
};
#endif
} // namespace

struct HostlinkTrackSnapshots::Impl
{
    explicit Impl(std::filesystem::path path) : root(std::move(path)) {}
    std::filesystem::path root;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    std::unique_ptr<Snapshot> snapshot;
#endif
};
HostlinkTrackSnapshots::HostlinkTrackSnapshots(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
HostlinkTrackSnapshots::~HostlinkTrackSnapshots() = default;
bool HostlinkTrackSnapshots::supported()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}
bool HostlinkTrackSnapshots::identityBusy() const
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    return impl_->snapshot && !impl_->snapshot->task.releasable();
#else
    return false;
#endif
}
void HostlinkTrackSnapshots::advance(uint64_t now, bool connected)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    auto& snapshot = impl_->snapshot;
    if (!snapshot) return;
    std::array<uint8_t, management::kTrackSnapshotChunkBytes> scratch;
    // Bounded slices keep an 8 MiB copy off the UI loop and preserve HostLink
    // heartbeat/command service without a second scheduler or payload queue.
    for (unsigned slice = 0; slice < 64 && !snapshot->task.releasable(); ++slice)
        snapshot->task.advance(scratch.data(), scratch.size(), now, connected);
    if (snapshot->task.releasable() && (!connected || now < snapshot->touched || now - snapshot->touched >= management::SnapshotBuildSession::kIdleTimeoutMs)) snapshot.reset();
#else
    (void)now;
    (void)connected;
#endif
}
void HostlinkTrackSnapshots::close(const uint8_t* session)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    auto& snapshot = impl_->snapshot;
    if (!snapshot || !snapshot->task.cancel(session, snapshot->task.metadata().id)) return;
    advance(snapshot->touched, false);
#else
    (void)session;
#endif
}
Status HostlinkTrackSnapshots::process(const management::EnvelopeView& request, uint64_t now,
                                       uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!request.session) return Status::InvalidField;
    if (!output || capacity < 400) return Status::Internal;
    auto& snapshot = impl_->snapshot;
    const auto operation = static_cast<Operation>(request.code);
    if (operation == Operation::TrackSnapshotBuild)
    {
        management::TrackSnapshotSelection selection;
        if (!management::decodeTrackSnapshotSelection({request.body, request.body_size}, selection) || selection.filename.front() == '.') return Status::InvalidField;
        if (snapshot && snapshot->task.matches(request.session, selection.id))
        {
            if (!snapshot->same(selection)) return Status::Conflict;
            snapshot->touched = now;
            snapshot->task.touch(request.session, selection.id, now);
            return management::encodeTrackSnapshotBuildState(snapshot->task, output, capacity, written) ? Status::Ok : Status::Internal;
        }
        if (snapshot && !snapshot->task.releasable()) return Status::Busy;
        UploadIo io(impl_->root);
        if (!io.current()) return Status::StorageUnavailable;
        if (catalog_media(io, request.session) != selection.media_session) return Status::Conflict;
        auto next = std::make_unique<Snapshot>(impl_->root, selection);
        std::copy_n(request.session, 16, next->request_session.begin());
        const auto started = next->task.begin(request.session, selection.id, selection.bytes, selection.format, now);
        if (started != management::SnapshotBuildResult::Pending) return started == management::SnapshotBuildResult::Busy ? Status::Busy : Status::InvalidField;
        next->touched = now;
        snapshot = std::move(next);
        return management::encodeTrackSnapshotBuildState(snapshot->task, output, capacity, written) ? Status::Ok : Status::Internal;
    }
    if (operation == Operation::TrackSnapshotCancel || operation == Operation::TrackSnapshotRelease)
    {
        std::array<uint8_t, 16> id{};
        if (!read_id(request, id)) return Status::InvalidField;
        if (snapshot && !std::memcmp(snapshot->task.metadata().id, id.data(), 16) && !snapshot->task.matches(request.session, id.data())) return Status::Unauthorized;
        if (operation == Operation::TrackSnapshotCancel)
        {
            if (!snapshot) return Status::Expired;
            if (!snapshot->task.cancel(request.session, id.data())) return Status::Unauthorized;
            return management::encodeTrackSnapshotBuildState(snapshot->task, output, capacity, written) ? Status::Ok : Status::Internal;
        }
        if (snapshot && !snapshot->task.releasable()) return Status::Busy;
        UploadIo io(impl_->root);
        if (!io.current()) return Status::StorageUnavailable;
        if (!io.lock()) return Status::Busy;
        const auto path = snapshot_path(id.data(), ".tsn");
        const auto opened = io.open(path.c_str(), UploadIo::Mode::Read);
        if (opened.status != UploadIo::OpenStatus::Missing)
        {
            management::TrackSnapshotMetadata metadata;
            if (!opened.handle || !io.lock_open(opened.handle) || !container::readSnapshotContainerHeader(io, opened.handle, metadata) ||
                std::memcmp(metadata.id, id.data(), 16) || !io.still_named(path.c_str()) || !io.remove(path.c_str())) return Status::StorageUnavailable;
        }
        if (snapshot && !std::memcmp(snapshot->task.metadata().id, id.data(), 16)) snapshot.reset();
        if (!capacity) return Status::Internal;
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    if (operation != Operation::TrackSnapshot) return Status::Unsupported;
    management::TrackSnapshotRequest read;
    if (!management::decodeTrackSnapshotRequest({request.body, request.body_size}, read)) return Status::InvalidField;
    UploadIo io(impl_->root);
    const auto path = snapshot_path(read.id, ".tsn");
    const auto opened = io.open(path.c_str(), UploadIo::Mode::Read);
    if (!opened.handle) return opened.status == UploadIo::OpenStatus::Missing ? Status::Expired : Status::StorageUnavailable;
    management::TrackSnapshotMetadata metadata;
    if (!io.lock_open(opened.handle) || !container::readSnapshotContainerHeader(io, opened.handle, metadata)) return Status::StorageUnavailable;
    if (std::memcmp(metadata.id, read.id, 16) || (read.has_hash && std::memcmp(metadata.hash, read.expected_hash, 32))) return Status::Conflict;
    if (read.offset >= metadata.bytes) return Status::InvalidField;
    size_t start = 0, length = 0;
    if (!management::prepareTrackSnapshotResponse(read, metadata, output, capacity, start, length, written)) return Status::Internal;
    if (!io.seek(opened.handle, container::kSnapshotContainerHeaderBytes + read.offset) || io.read(opened.handle, output + start, length) != length ||
        !io.still_named(path.c_str()))
    {
        written = 0;
        return Status::StorageUnavailable;
    }
    if (snapshot && snapshot->task.matches(request.session, read.id))
    {
        snapshot->touched = now;
        snapshot->task.touch(request.session, read.id, now);
    }
    return Status::Ok;
#else
    (void)request;
    (void)now;
    (void)output;
    (void)capacity;
    return Status::Unsupported;
#endif
}
} // namespace platform::linux_runtime
