#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_track_prepare.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "signed_route_package_fixture.h"
#include <unistd.h>
#include <algorithm>

namespace native_track_fixture
{
namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
using Id = std::array<uint8_t, 16>;
struct Fixture
{
    Fixture(const std::filesystem::path& root, const std::filesystem::path& image, bool assets)
        : root(root), sd(root / "sd"), database(root / "author.sqlite"), author(database), uploads(sd, database), prepare(sd, database, uploads), package(package_fixture::make(image, assets))
    {
        std::filesystem::create_directories(sd);
        assert(author.begin());
        operation[0] = 9;
        session[0] = 1;
        native::LinuxGeocachingCrypto crypto;
        assert(crypto.sha256({author.publicKey().data(), 64}, actor.data()));
        geocaching::protocol::CmpReader reader({package.signed_manifest.data(), package.signed_manifest.size()});
        size_t count = 0;
        geocaching::ByteView original, signature;
        assert(reader.array(count, 2) && count == 2 && reader.binary(original, 4096) && reader.binary(signature, 64) && reader.finished());
        manifest.assign(original.data, original.data + original.size);
        track_sharing::ManifestView view;
        assert(track_sharing::decodeManifest({manifest.data(), manifest.size()}, view));
        std::copy_n(author.publicKey().data(), 64, manifest.begin() + size_t(view.author.data - manifest.data()));
        const auto kml = upload(3, package.kml), raw = upload(4, manifest);
        Id mapping{};
        if (assets)
        {
            const auto index = upload(8, package.index), resource = upload(9, package.image);
            mapping = upload(10, encode([&](Writer& w)
                                        {w.array(3);w.unsignedInteger(1);w.binary({index.data(),16});w.binary({resource.data(),16}); }));
        }
        const auto request = encode([&](Writer& w)
                                    {w.array(assets ? 6 : 5);w.binary({operation.data(),16});w.binary({kml.data(),16});w.binary({raw.data(),16});
            w.unsignedInteger(author.generation());w.nil();if(assets)w.binary({mapping.data(),16}); });
        std::array<uint8_t, 448> output{};
        size_t written = 0;
        mgmt::EnvelopeView envelope{session.data(), nullptr, uint16_t(assets ? mgmt::Operation::TrackPackageSignPrepare : mgmt::Operation::TrackSignPrepare), request.data(), request.size()};
        for (unsigned steps = 0;; ++steps)
        {
            assert(steps < 10000);
            assert(prepare.process(envelope, ++now, output.data(), output.size(), written) == mgmt::Status::Ok);
            geocaching::protocol::CmpReader response({output.data(), written});
            uint64_t state = 0, revision_number = 0;
            geocaching::ByteView id, obj, hash;
            assert(response.array(count, 6) && count == 6 && response.binary(id, 16) && response.unsignedInteger(state));
            if (state == 1)
            {
                assert(response.binary(obj, 32) && response.binary(hash, 32) && response.unsignedInteger(revision_number) && revision_number == 1);
                std::copy_n(obj.data, 32, object.begin());
                std::copy_n(hash.data, 32, revision.begin());
                break;
            }
            assert(state == 0);
            prepare.advance(++now, true);
        }
        prepare.close(session.data());
    }
    Id upload(unsigned kind, const Bytes& bytes)
    {
        Id id{};
        id[0] = ++next_upload;
        const auto hash = package_fixture::hash(bytes);
        const auto call = [&](mgmt::Operation operation, const Bytes& body)
        {
            std::array<uint8_t, 448> output{};
            size_t written = 0;
            mgmt::EnvelopeView request{session.data(), nullptr, uint16_t(operation), body.data(), body.size()};
            assert(uploads.process(request, ++now, output.data(), output.size(), written) == mgmt::Status::Ok);
        };
        call(mgmt::Operation::TransferBegin, encode([&](Writer& w)
                                                    {w.array(4);w.binary({id.data(),16});w.unsignedInteger(kind);w.unsignedInteger(bytes.size());w.binary({hash.data(),32}); }));
        for (size_t offset = 0; offset < bytes.size(); offset += 320)
            call(mgmt::Operation::TransferChunk, encode([&](Writer& w)
                                                        {w.array(3);w.binary({id.data(),16});w.unsignedInteger(offset);w.binary({bytes.data()+offset,std::min<size_t>(320,bytes.size()-offset)}); }));
        call(mgmt::Operation::TransferFinish, encode([&](Writer& w)
                                                     {w.array(1);w.binary({id.data(),16}); }));
        uploads.advance(++now, true);
        uploads.close(session.data());
        return id;
    }
    std::filesystem::path root, sd, database;
    native::LinuxReticulumIdentity author;
    native::HostlinkUploads uploads;
    native::HostlinkTrackPrepare prepare;
    package_fixture::SignedPackage package;
    Bytes manifest;
    std::array<uint8_t, 32> actor{}, object{}, revision{};
    Id operation{}, session{};
    uint64_t now = 0;
    uint8_t next_upload = 0;
};
} // namespace native_track_fixture
