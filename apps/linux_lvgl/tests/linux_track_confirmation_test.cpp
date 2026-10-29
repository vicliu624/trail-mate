#include "native_track_directory_fixture.h"
#include "platform/linux/hostlink_track_confirmation.h"
#include "platform/linux/hostlink_upload_io.h"
#include "track_sharing/object_head_store.h"
#include "track_sharing/object_paths.h"
#include <fstream>

namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string scenario(argv[2]);
    const bool imported = scenario == "import" || scenario == "unknown" || scenario == "import_retry" || scenario == "v2";
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-ip-confirmation-" + std::to_string(getpid()));
    {
        auto fixture = std::make_unique<native_track_fixture::Fixture>(root, argv[1], scenario == "v2");
        native::LinuxReticulumIdentity server(root / "directory.sqlite");
        assert(server.begin());
        native_track_fixture::Directory directory(*fixture, server);
        std::array<char, 160> path{};
        assert(ts::resolveObjectPath({fixture->object.data(), 32}, ts::ObjectFile::SignedManifest, {fixture->revision.data(), 32}, path.data(), path.size()));
        std::ifstream file(fixture->sd / (path.data() + 1), std::ios::binary);
        const Bytes signed_manifest((std::istreambuf_iterator<char>(file)), {});
        geocaching::protocol::CmpReader signed_reader({signed_manifest.data(), signed_manifest.size()});
        size_t fields = 0;
        geocaching::ByteView raw_manifest, signature;
        assert(signed_reader.array(fields, 2) && fields == 2 && signed_reader.binary(raw_manifest, 4096) && signed_reader.binary(signature, 64) && signed_reader.finished());
        native_track_fixture::Id nonce{};
        nonce[0] = 42;
        auto raw_receipt = encode([&](Writer& w)
                                  {
            w.array(13); w.unsignedInteger(1); w.binary({nonce.data(), 16}); w.binary({fixture->object.data(), 32}); w.binary({fixture->revision.data(), 32});
            w.unsignedInteger(1); w.unsignedInteger(0); w.binary({fixture->package.kml_hash.data(), 32}); w.unsignedInteger(fixture->package.kml.size());
            w.binary({directory.binding.public_key, 64}); w.binary({directory.binding.delivery, 16}); w.binary({directory.binding.epoch, 16}); w.unsignedInteger(1); w.unsignedInteger(100); });
        constexpr char domain[] = "TrailMate.Track.Receipt.v1";
        Bytes message(reinterpret_cast<const uint8_t*>(domain), reinterpret_cast<const uint8_t*>(domain) + sizeof(domain));
        message.insert(message.end(), raw_receipt.begin(), raw_receipt.end());
        std::array<uint8_t, 64> receipt_signature{};
        assert(server.sign(message.data(), message.size(), receipt_signature.data()));
        if (scenario == "forged") receipt_signature.back() ^= 1;
        const auto receipt = encode([&](Writer& w)
                                    { w.array(2); w.binary({raw_receipt.data(), raw_receipt.size()}); w.binary({receipt_signature.data(), 64}); });
        const auto proof_hash = package_fixture::hash(receipt);
        const auto proof = fixture->upload(7, receipt);
        native_track_fixture::Id manifest_upload{};
        std::array<uint8_t, 32> manifest_hash{};
        if (imported)
        {
            const Bytes raw(raw_manifest.data, raw_manifest.data + raw_manifest.size);
            manifest_hash = package_fixture::hash(raw);
            manifest_upload = fixture->upload(4, raw);
            assert(ts::resolveObjectPath({fixture->object.data(), 32}, ts::ObjectFile::Directory, {}, path.data(), path.size()));
            const auto object_root = fixture->sd / (path.data() + 1);
            assert(object_root.parent_path() == fixture->sd / "trailmate/tracks");
            // Fixture-owned temporary object only: simulate a replacement device.
            std::filesystem::remove_all(object_root);
            if (scenario == "unknown")
            {
                std::filesystem::create_directories(object_root);
                std::ofstream(object_root / "unknown.bin") << "preserve";
            }
        }
        uint32_t generation = fixture->author.generation() + (scenario == "stale" ? 1 : 0);
        auto body = encode([&](Writer& w)
                           {
            w.array(imported ? 14 : 11); w.binary({fixture->operation.data(), 16}); w.binary({proof.data(), 16}); w.binary({proof_hash.data(), 32}); w.unsignedInteger(generation);
            w.binary({directory.binding.public_key, 64}); w.binary({directory.binding.delivery, 16}); w.binary({directory.binding.epoch, 16}); w.binary({nonce.data(), 16});
            w.binary({fixture->object.data(), 32}); w.binary({fixture->revision.data(), 32}); w.unsignedInteger(1);
            if (imported) { w.binary({manifest_upload.data(), 16}); w.binary({manifest_hash.data(), 32}); w.binary(signature); } });
        auto coordinator = std::make_unique<native::HostlinkTrackConfirmation>(fixture->sd, fixture->database, fixture->uploads);
        const auto call = [&]()
        {
            std::array<uint8_t, 448> output{};
            size_t written = 0;
            const mgmt::EnvelopeView request{fixture->session.data(), nullptr, uint16_t(imported ? mgmt::Operation::TrackImportHistory : mgmt::Operation::TrackConfirmPublication), body.data(), body.size()};
            const auto status = coordinator->process(request, ++fixture->now, output.data(), output.size(), written);
            if (status != mgmt::Status::Ok) return std::pair(status, false);
            geocaching::protocol::CmpReader reader({output.data(), written});
            uint64_t epoch = 0, done = 0;
            geocaching::ByteView op, transfer;
            assert(reader.array(fields, 8) && fields == 8 && reader.binary(op, 16) && reader.binary(transfer, 16) && reader.unsignedInteger(epoch) && reader.unsignedInteger(done));
            assert(epoch == generation && !std::memcmp(op.data, fixture->operation.data(), 16) && !std::memcmp(transfer.data, proof.data(), 16));
            return std::pair(status, done == 1);
        };
        const auto expected = scenario == "stale" ? mgmt::Status::StaleIdentity : scenario == "unknown" || scenario == "forged" ? mgmt::Status::Conflict
                                                                                                                                : mgmt::Status::Ok;
        bool complete = false;
        bool interrupted = false;
        for (unsigned step = 0; step < 10000; ++step)
        {
            const auto result = call();
            if (result.first != mgmt::Status::Ok)
            {
                assert(result.first == expected && expected != mgmt::Status::Ok);
                complete = true;
                break;
            }
            if (result.second)
            {
                assert(expected == mgmt::Status::Ok);
                complete = true;
                break;
            }
            if (scenario == "import_retry" && !interrupted)
            {
                coordinator.reset();
                coordinator = std::make_unique<native::HostlinkTrackConfirmation>(fixture->sd, fixture->database, fixture->uploads);
                interrupted = true;
            }
            coordinator->advance(++fixture->now, true);
        }
        assert(complete && !coordinator->identityBusy());
        if (expected == mgmt::Status::Ok)
        {
            assert(call().second);
            if (scenario == "busy_retry")
            {
                native::UploadIo competing(fixture->sd);
                assert(competing.lock());
                assert(call().first == mgmt::Status::Busy);
                competing.unlock();
                assert(call().second);
            }
            if (scenario == "media")
            {
                std::filesystem::rename(fixture->sd, root / "detached");
                std::filesystem::create_directories(fixture->sd);
                assert(call().first == mgmt::Status::StorageUnavailable);
                return 0;
            }
            if (scenario == "head_changed")
            {
                for (const auto slot : {ts::ObjectFile::HeadA, ts::ObjectFile::HeadB})
                {
                    assert(ts::resolveObjectPath({fixture->object.data(), 32}, slot, {}, path.data(), path.size()));
                    std::ofstream(fixture->sd / (path.data() + 1), std::ios::binary | std::ios::trunc) << "damaged";
                }
                assert(call().first == mgmt::Status::Conflict);
                return 0;
            }
            coordinator->close(fixture->session.data());
            coordinator = std::make_unique<native::HostlinkTrackConfirmation>(fixture->sd, fixture->database, fixture->uploads);
            for (unsigned step = 0;; ++step)
            {
                assert(step < 10000);
                const auto result = call();
                assert(result.first == mgmt::Status::Ok);
                if (result.second) break;
                coordinator->advance(++fixture->now, true);
            }
            coordinator->close(fixture->session.data());
            native::UploadIo io(fixture->sd);
            assert(io.lock());
            std::array<char, 160> a{}, b{};
            assert(ts::resolveObjectPath({fixture->object.data(), 32}, ts::ObjectFile::HeadA, {}, a.data(), a.size()) && ts::resolveObjectPath({fixture->object.data(), 32}, ts::ObjectFile::HeadB, {}, b.data(), b.size()));
            ts::ObjectHeadStore<native::UploadIo> heads(io, a.data(), b.data());
            ts::ObjectHead head, verify;
            std::array<uint8_t, 448> scratch{};
            assert(heads.load(head, verify, scratch.data(), scratch.size()) == ts::HeadStoreResult::Ready && head.phase == ts::HeadPhase::Committed);
        }
    }
    std::filesystem::remove_all(root);
}
