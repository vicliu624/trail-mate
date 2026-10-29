#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_package_input.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "signed_route_package_fixture.h"
#include "track_sharing/upload_protocol.h"
#include <unistd.h>

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
    assert(argc == 2);
    const auto root = fs::temp_directory_path() / ("trailmate-package-input-" + std::to_string(getpid()));
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
    const auto request = [&](const SignedPackage& package, const Id& signed_upload, const Id* mapped)
    {
        auto body = encode([&](Writer& w)
                           { w.array(8); w.binary({operation.data(), 16}); w.binary({kml.data(), 16}); w.binary({signed_upload.data(), 16});
            w.unsignedInteger(actor.generation()); w.nil(); if (mapped) w.binary({mapped->data(), 16}); else w.nil(); w.binary({package.object.data(), 32}); w.binary({package.revision.data(), 32}); });
        // Decode against persistent buffers so all borrowed fields outlive begin.
        return body;
    };
    const auto verify = [&](const Bytes& body, ts::ManifestFileResult expected, bool attachments = true)
    {
        ts::PrepareRequest request;
        assert(ts::decodePrepareRequest({body.data(), body.size()}, request));
        native::UploadIo io(sd);
        assert(io.lock());
        native::HostlinkPackageInput input(io, request, actor.generation(), actor_hash.data());
        assert(!input.ready() && !input.verified() && input.resourceCount() == 0);
        auto result = input.begin();
        for (unsigned steps = 0; result == ts::ManifestFileResult::Pending; ++steps)
        {
            assert(steps < 10000);
            result = input.step(actor.generation(), actor_hash.data());
        }
        assert(result == expected);
        if (expected == ts::ManifestFileResult::Ready)
        {
            assert(input.ready() && input.verified() && input.signedBytes().size && input.kmlPath() && input.signedPath());
            assert(std::memcmp(input.verified()->manifest.author.data, actor.publicKey().data(), 64)); // Original remote author remains original.
            assert(input.resourceCount() == unsigned(attachments) && input.availability() == unsigned(attachments));
            native::PackageInputResource retained;
            assert(input.resource(0, retained) == attachments);
            if (attachments) assert(retained.hash == fixture.image_hash && retained.bytes == fixture.image.size() && input.indexPath());
        }
        else
        {
            assert(!input.ready() && !input.verified() && input.signedBytes().size == 0 && input.resourceCount() == 0);
        }
    };
    verify(request(fixture, signed_id, &mapping), ts::ManifestFileResult::Ready);
    auto wrong_revision = fixture;
    wrong_revision.revision[0] ^= 1;
    verify(request(wrong_revision, signed_id, &mapping), ts::ManifestFileResult::Invalid);
    const auto bad_signed_id = id(6);
    auto bad_signed = fixture.signed_manifest;
    bad_signed.back() ^= 1;
    upload(bad_signed_id, 11, bad_signed);
    verify(request(fixture, bad_signed_id, &mapping), ts::ManifestFileResult::Invalid);
    const auto bad_mapping_id = id(7);
    const Id missing{};
    upload(bad_mapping_id, 10, encode([&](Writer& w)
                                      { w.array(3); w.unsignedInteger(1); w.binary({index.data(), 16}); w.binary({missing.data(), 16}); }));
    verify(request(fixture, signed_id, &bad_mapping_id), ts::ManifestFileResult::Invalid);
    const auto staged = [&](const Id& upload_id, ts::UploadKind kind)
    { char path[64]{}; assert(ts::resolveUploadPath(upload_id.data(), 16, ts::UploadFile::Candidate, path, sizeof(path), kind)); return sd / (path + 1); };
    auto damaged = fixture.image;
    damaged.back() ^= 1;
    write(staged(resource, ts::UploadKind::AttachmentResource), damaged);
    verify(request(fixture, signed_id, &mapping), ts::ManifestFileResult::HashMismatch);
    write(staged(resource, ts::UploadKind::AttachmentResource), fixture.image);
    auto wrong_geometry = make(argv[1], true, false);
    const auto geometry_id = id(8);
    // Remove the second signed upload only through its exact maintenance tuple.
    {
        const auto digest = hash(bad_signed);
        const auto body = encode([&](Writer& w)
                                 { w.array(4); w.binary({bad_signed_id.data(), 16}); w.unsignedInteger(11); w.unsignedInteger(bad_signed.size()); w.binary({digest.data(), 32}); });
        mgmt::EnvelopeView cleanup{session, nullptr, uint16_t(mgmt::Operation::TransferCleanup), body.data(), body.size()};
        uint8_t output[448];
        size_t size = 0;
        assert(uploads.process(cleanup, now++, output, sizeof(output), size) == mgmt::Status::Ok);
    }
    upload(geometry_id, 11, wrong_geometry.signed_manifest);
    verify(request(wrong_geometry, geometry_id, &mapping), ts::ManifestFileResult::GeometryMismatch);
    const auto legacy = make(argv[1], false);
    // Reuse the geometry upload's ID after exact cleanup to respect shared quota.
    {
        const auto digest = hash(wrong_geometry.signed_manifest);
        const auto body = encode([&](Writer& w)
                                 { w.array(4); w.binary({geometry_id.data(), 16}); w.unsignedInteger(11); w.unsignedInteger(wrong_geometry.signed_manifest.size()); w.binary({digest.data(), 32}); });
        mgmt::EnvelopeView cleanup{session, nullptr, uint16_t(mgmt::Operation::TransferCleanup), body.data(), body.size()};
        uint8_t output[448];
        size_t size = 0;
        assert(uploads.process(cleanup, now++, output, sizeof(output), size) == mgmt::Status::Ok);
    }
    upload(geometry_id, 11, legacy.signed_manifest);
    verify(request(legacy, geometry_id, nullptr), ts::ManifestFileResult::Ready, false);
    {
        const auto digest = hash(legacy.signed_manifest);
        const auto body = encode([&](Writer& w)
                                 { w.array(4); w.binary({geometry_id.data(), 16}); w.unsignedInteger(11); w.unsignedInteger(legacy.signed_manifest.size()); w.binary({digest.data(), 32}); });
        mgmt::EnvelopeView cleanup{session, nullptr, uint16_t(mgmt::Operation::TransferCleanup), body.data(), body.size()};
        uint8_t output[448];
        size_t size = 0;
        assert(uploads.process(cleanup, now++, output, sizeof(output), size) == mgmt::Status::Ok);
    }
    upload(geometry_id, 11, legacy.signed_manifest, false);
    verify(request(legacy, geometry_id, nullptr), ts::ManifestFileResult::Invalid);
    {
        const auto body = request(fixture, signed_id, &mapping);
        ts::PrepareRequest decoded;
        assert(ts::decodePrepareRequest({body.data(), body.size()}, decoded));
        native::UploadIo io(sd);
        assert(io.lock());
        native::HostlinkPackageInput input(io, decoded, actor.generation(), actor_hash.data());
        assert(input.begin() == ts::ManifestFileResult::Pending);
        assert(input.step(actor.generation() + 1, actor_hash.data()) == ts::ManifestFileResult::Invalid);
    }
    fs::remove_all(root);
}
