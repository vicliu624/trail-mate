#include "geocaching/protocol/cmp_reader.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_track_prepare_input.h"
#include "platform/linux/hostlink_track_prepare_persistence.h"
#include "platform/linux/hostlink_track_signer.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "signed_route_package_fixture.h"
#include "track_sharing/upload_protocol.h"
#include <cerrno>
#include <cstdio>
#include <unistd.h>

bool fail_reservation_write = false, reservation_write_failed = false;
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int file, const void* bytes, size_t count)
{
    if (fail_reservation_write)
    {
        char path[64]{}, target[512]{};
        std::snprintf(path, sizeof(path), "/proc/self/fd/%d", file);
        const auto length = readlink(path, target, sizeof(target) - 1);
        const std::string name = length > 0 ? std::string(target, size_t(length)) : std::string{};
        if (name.size() >= 4 && (name.substr(name.size() - 4) == ".rva" || name.substr(name.size() - 4) == ".rvb"))
        {
            fail_reservation_write = false;
            reservation_write_failed = true;
            errno = EIO;
            return -1;
        }
    }
    return __real_write(file, bytes, count);
}

namespace fs = std::filesystem;
namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using namespace package_fixture;
using Id = std::array<uint8_t, 16>;
static Id id(uint8_t number)
{
    Id value{};
    value[0] = number;
    return value;
}
static void write(const fs::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    const bool inject_failure = argc == 3;
    const auto root = fs::temp_directory_path() / ("trailmate-prepare-input-" + std::to_string(getpid()));
    const auto sd = root / "sd", database = root / "identity.sqlite3";
    fs::create_directories(sd);
    native::HostlinkUploads uploads(sd, database);
    auto fixture = make(argv[1]);
    uint8_t session[16]{1};
    uint64_t now = 1;
    const auto upload = [&](const Id& transfer, unsigned kind, const Bytes& bytes, bool seal = true)
    {
        const auto digest = hash(bytes);
        const auto call = [&](mgmt::Operation operation, const Bytes& body)
        {
            mgmt::EnvelopeView request{session, nullptr, uint16_t(operation), body.data(), body.size()};
            uint8_t output[448]{};
            size_t size = 0;
            assert(uploads.process(request, now++, output, sizeof(output), size) == mgmt::Status::Ok);
        };
        call(mgmt::Operation::TransferBegin, encode([&](Writer& w)
                                                    { w.array(4); w.binary({transfer.data(), 16}); w.unsignedInteger(kind); w.unsignedInteger(bytes.size()); w.binary({digest.data(), 32}); }));
        for (size_t offset = 0; offset < bytes.size(); offset += 320)
            call(mgmt::Operation::TransferChunk, encode([&](Writer& w)
                                                        { w.array(3); w.binary({transfer.data(), 16}); w.unsignedInteger(offset); w.binary({bytes.data() + offset, std::min<size_t>(320, bytes.size() - offset)}); }));
        if (seal)
        {
            call(mgmt::Operation::TransferFinish, encode([&](Writer& w)
                                                         { w.array(1); w.binary({transfer.data(), 16}); }));
            uploads.advance(now++, true);
        }
        uploads.close(session);
    };
    const auto kml = id(1), signed_id = id(2), index = id(3), resource = id(4), mapping = id(5), operation = id(9);
    upload(kml, 3, fixture.kml);
    upload(signed_id, 11, fixture.signed_manifest);
    upload(index, 8, fixture.index);
    upload(resource, 9, fixture.image);
    const auto mapping_bytes = encode([&](Writer& w)
                                      { w.array(3); w.unsignedInteger(1); w.binary({index.data(), 16}); w.binary({resource.data(), 16}); });
    upload(mapping, 10, mapping_bytes);
    native::LinuxReticulumIdentity actor(database);
    assert(actor.begin());
    std::array<uint8_t, 32> actor_hash{};
    native::LinuxGeocachingCrypto crypto;
    assert(crypto.sha256({actor.publicKey().data(), 64}, actor_hash.data()));
    const auto raw_manifest = [&](const SignedPackage& package, bool own_author = true)
    {
        geocaching::protocol::CmpReader reader({package.signed_manifest.data(), package.signed_manifest.size()});
        size_t count = 0;
        geocaching::ByteView raw, signature;
        assert(reader.array(count, 2) && count == 2 && reader.binary(raw, 4096) && reader.binary(signature, 64) && reader.finished());
        Bytes result(raw.data, raw.data + raw.size);
        ts::ManifestView view;
        assert(ts::decodeManifest({result.data(), result.size()}, view));
        if (own_author) std::copy_n(actor.publicKey().data(), 64, result.begin() + (view.author.data - result.data()));
        if (view.version == 1) result[size_t(view.nonce.data - result.data())] = 22;
        return result;
    };
    const auto manifest_id = id(10);
    const auto raw = raw_manifest(fixture);
    upload(manifest_id, 4, raw);
    const auto request = [&](const Id& manifest, const Id* mapped, bool package = true)
    {
        return encode([&](Writer& w)
                      {
            w.array(package ? 6 : 5); w.binary({operation.data(), 16}); w.binary({kml.data(), 16}); w.binary({manifest.data(), 16});
            w.unsignedInteger(actor.generation()); w.nil(); if (package) { if (mapped) w.binary({mapped->data(), 16}); else w.nil(); } });
    };
    const auto verify = [&](const Bytes& body, ts::ManifestFileResult expected, bool has_attachments = true, bool stale = false)
    {
        ts::PrepareRequest decoded;
        assert(ts::decodePrepareRequest({body.data(), body.size()}, decoded));
        native::UploadIo io(sd);
        assert(io.lock());
        native::HostlinkTrackPrepareInput input(io, decoded, actor.publicKey().data(), actor_hash.data());
        assert(!input.ready() && input.manifest().size == 0);
        auto result = input.begin();
        for (unsigned step = 0; result == ts::ManifestFileResult::Pending; ++step)
        {
            assert(step < 10000);
            result = input.step(actor.generation() + unsigned(stale), actor_hash.data());
        }
        assert(result == expected);
        if (expected == ts::ManifestFileResult::Ready)
        {
            assert(input.ready() && input.manifest().size && input.matchesCandidate(input.view));
            assert(!std::memcmp(input.view.author.data, actor.publicKey().data(), 64));
            assert(input.attachment_input.resourceCount() == unsigned(has_attachments));
            assert(input.attachment_input.availability() == unsigned(has_attachments));
            auto persistence = std::make_unique<native::HostlinkTrackPreparePersistence>(input);
            assert(persistence->initialize());
            native::HostlinkTrackSigner signer(actor);
            std::array<uint8_t, 448> scratch{};
            mgmt::Status persisted;
            unsigned steps = 0;
            fail_reservation_write = inject_failure;
            do
            {
                persisted = persistence->finish(signer, nullptr, scratch.data(), scratch.size());
                assert(++steps < 1000);
            } while (persisted == mgmt::Status::Busy);
            if (inject_failure)
            {
                assert(reservation_write_failed && persisted == mgmt::Status::StorageUnavailable);
                assert(!persistence->ready() && persistence->signature().size == 0);
                assert(actor.ready() && actor.generation() == decoded.generation);
            }
            else
            {
                assert(persisted == mgmt::Status::Ok && persistence->ready() && persistence->signature().size == 64);
                assert(persistence->head().phase == ts::HeadPhase::Signed);
            }
            input.cancel();
            assert(!input.ready() && input.manifest().size == 0);
        }
        else assert(!input.ready() && input.manifest().size == 0);
    };
    verify(request(manifest_id, &mapping), ts::ManifestFileResult::Ready);
    if (inject_failure)
    {
        fs::remove_all(root);
        return 0;
    }
    verify(request(manifest_id, &mapping), ts::ManifestFileResult::Ready); // Durable same-operation recovery.
    verify(request(manifest_id, &mapping), ts::ManifestFileResult::Invalid, true, true);
    verify(request(manifest_id, nullptr), ts::ManifestFileResult::Invalid);
    verify(request(manifest_id, nullptr, false), ts::ManifestFileResult::Invalid);
    const auto cleanup = [&](const Id& transfer, const Bytes& bytes)
    {
        const auto digest = hash(bytes);
        const auto body = encode([&](Writer& w)
                                 { w.array(4); w.binary({transfer.data(), 16}); w.unsignedInteger(4); w.unsignedInteger(bytes.size()); w.binary({digest.data(), 32}); });
        mgmt::EnvelopeView request{session, nullptr, uint16_t(mgmt::Operation::TransferCleanup), body.data(), body.size()};
        uint8_t output[448]{};
        size_t size = 0;
        assert(uploads.process(request, now++, output, sizeof(output), size) == mgmt::Status::Ok);
    };
    const auto foreign = id(11);
    upload(foreign, 4, raw_manifest(fixture, false));
    verify(request(foreign, &mapping), ts::ManifestFileResult::Invalid);
    cleanup(foreign, raw_manifest(fixture, false));
    const auto bad_geometry = id(12);
    upload(bad_geometry, 4, raw_manifest(make(argv[1], true, false)));
    verify(request(bad_geometry, &mapping), ts::ManifestFileResult::GeometryMismatch);
    cleanup(bad_geometry, raw_manifest(make(argv[1], true, false)));
    const auto staged = [&](const Id& transfer, ts::UploadKind kind)
    {
        char path[64]{};
        assert(ts::resolveUploadPath(transfer.data(), 16, ts::UploadFile::Candidate, path, sizeof(path), kind));
        return sd / (path + 1);
    };
    auto damaged = fixture.image;
    damaged.back() ^= 1;
    write(staged(resource, ts::UploadKind::AttachmentResource), damaged);
    verify(request(manifest_id, &mapping), ts::ManifestFileResult::HashMismatch);
    write(staged(resource, ts::UploadKind::AttachmentResource), fixture.image);
    const auto legacy_id = id(13);
    upload(legacy_id, 4, raw_manifest(make(argv[1], false)));
    verify(request(legacy_id, nullptr, false), ts::ManifestFileResult::Ready, false);
    verify(request(legacy_id, nullptr, true), ts::ManifestFileResult::Invalid, false);
    fs::remove_all(root);
}
