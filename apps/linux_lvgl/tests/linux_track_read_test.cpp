#include "chat/linux_track_read.h"
#include "native_track_directory_fixture.h"
#include "platform/linux/track_read_runtime.h"
#include "track_sharing/network_request.h"
#include "track_sharing/object_paths.h"
#include <fstream>
#include <future>

namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
using Reader = geocaching::protocol::CmpReader;
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string scenario(argv[2]);
    unsigned operation = scenario.size() >= 3 && scenario.substr(0, 2) == "op" ? std::stoul(scenario.substr(2)) : scenario == "forged" ? 2
                                                                                                                                       : 8;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-track-read-" + std::to_string(getpid()));
    {
        auto fixture = std::make_unique<native_track_fixture::Fixture>(root, argv[1], false);
        native::LinuxReticulumIdentity server(root / "directory.sqlite");
        assert(server.begin());
        native_track_fixture::Directory directory(*fixture, server);
        directory.binding.chunk_limit = 1024;
        native::HostlinkTrackRead frontend(fixture->database), wrong(root / "wrong.sqlite");
        assert(!frontend.supported());
        Bytes sent;
        unsigned sends = 0;
        uint32_t message = 0;
        uint64_t now = 1, context = 1;
        bool enabled = true;
        trailmate::linux_app::LinuxTrackRead worker(
            fixture->sd, fixture->database, fixture->author,
            [&](const uint8_t* session, geocaching::ByteView delivery, geocaching::ByteView epoch, ts::PublicationDirectory& result)
            { assert(!std::memcmp(session, fixture->session.data(), 16)); if (std::memcmp(delivery.data, directory.binding.delivery, 16) || std::memcmp(epoch.data, directory.binding.epoch, 16)) return mgmt::Status::Conflict; result = directory.binding; return mgmt::Status::Ok; },
            [&](const uint8_t* target, const uint8_t* key, chat::lxmf::ByteSpan bytes, chat::MessageId id)
            { assert(id && !std::memcmp(target, directory.binding.delivery, 16) && !std::memcmp(key, directory.binding.public_key, 64)); ++sends; sent.assign(bytes.data, bytes.data + bytes.size); return true; },
            [&]
            { return ++message; });
        worker.tick(now, enabled, context);
        assert(frontend.supported() && !wrong.supported());
        native_track_fixture::Id request{};
        request[0] = 40;
        Bytes content(1024, 42);
        const auto file_hash = package_fixture::hash(content);
        const auto body = encode([&](Writer& w)
                                 {
            w.array(6); w.binary({request.data(), 16}); w.binary({directory.binding.delivery, 16}); w.binary({directory.binding.epoch, 16}); w.unsignedInteger(fixture->author.generation()); w.unsignedInteger(operation);
            if (operation == 0 || operation == 9) w.array(0);
            else if (operation == 2) { w.array(2); w.binary({fixture->object.data(), 32}); w.binary({fixture->revision.data(), 32}); }
            else if (operation == 8) { w.array(4); w.binary({file_hash.data(), 32}); w.unsignedInteger(1024); w.unsignedInteger(0); w.unsignedInteger(1024); }
            else if (operation == 10) { w.array(7); w.text("test"); w.binary({file_hash.data(), 32}); w.nil(); w.signedInteger(-1); w.nil(); w.unsignedInteger(3); w.unsignedInteger(16); }
            else if (operation == 11) { w.array(4); w.text("test"); w.text("test:r"); w.binary({file_hash.data(), 32}); w.unsignedInteger(3); }
            else if (operation == 12) { w.array(7); w.text("test"); w.text("test:r"); w.binary({file_hash.data(), 32}); w.binary({file_hash.data(), 32}); w.unsignedInteger(1024); w.unsignedInteger(0); w.unsignedInteger(1024); }
            else
            {
                w.array(operation == 1 ? 7 : 10);
                if (operation == 13) { w.text("test"); w.text("test:r"); w.binary({file_hash.data(), 32}); }
                w.array(4); w.signedInteger(0); w.signedInteger(0); w.signedInteger(100); w.signedInteger(100);
                w.nil(); w.nil(); w.unsignedInteger(0); w.unsignedInteger(16); w.unsignedInteger(100); w.nil();
            } });
        const auto query = encode([&](Writer& w)
                                  { w.array(1); w.binary({request.data(), 16}); });
        const auto call = [&](mgmt::Operation op, const Bytes& input, mgmt::Status expected = mgmt::Status::Ok, const uint8_t* session = nullptr)
        {
            std::array<uint8_t, 448> output{};
            size_t written = 0;
            const mgmt::EnvelopeView envelope{session ? session : fixture->session.data(), nullptr, uint16_t(op), input.data(), input.size()};
            for (unsigned attempt = 0;; ++attempt)
            {
                assert(attempt < 10000);
                auto pending = std::async(std::launch::async, [&]
                                          { return frontend.process(envelope, output.data(), output.size(), written); });
                for (unsigned visit = 0; pending.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready; ++visit)
                {
                    assert(visit < 2000);
                    worker.tick(++now, enabled, context);
                }
                const auto status = pending.get();
                if (status == mgmt::Status::Busy)
                {
                    worker.tick(++now, enabled, context);
                    continue;
                }
                assert(status == expected);
                return Bytes(output.begin(), output.begin() + written);
            }
        };
        const auto admitted = call(mgmt::Operation::TrackReadBegin, body);
        assert(!admitted.empty() && sends == 1 && frontend.identityBusy());
        Reader network({sent.data(), sent.size()});
        size_t fields = 0;
        uint64_t value = 0;
        geocaching::ByteView echoed;
        assert(network.array(fields, 6) && fields == 6 && network.unsignedInteger(value) && value == 1 && network.unsignedInteger(value) && value == 0 && network.unsignedInteger(value) && value == operation && network.binary(echoed, 16) && !std::memcmp(echoed.data, request.data(), 16));
        const auto reply = encode([&](Writer& w)
                                  {
            w.array(6); w.unsignedInteger(1); w.unsignedInteger(1); w.unsignedInteger(operation); w.binary({request.data(), 16}); w.unsignedInteger(200);
            if (operation == 0)
            {
                w.array(8); w.unsignedInteger(1); w.text(ts::kApplicationType); w.binary({directory.binding.public_key, 64}); w.binary({directory.binding.epoch, 16});
                w.unsignedInteger(8388608); w.unsignedInteger(1024); w.unsignedInteger(32); w.array(4); for (unsigned op : {0, 1, 2, 8}) w.unsignedInteger(op);
            }
            else if (operation == 1 || operation == 13) { w.array(3); w.unsignedInteger(100); w.nil(); w.array(0); }
            else if (operation == 2)
            {
                std::array<char, 160> path{};
                assert(ts::resolveObjectPath({fixture->object.data(), 32}, ts::ObjectFile::SignedManifest, {fixture->revision.data(), 32}, path.data(), path.size()));
                std::ifstream file(fixture->sd / (path.data() + 1), std::ios::binary);
                const Bytes signed_bytes((std::istreambuf_iterator<char>(file)), {});
                Reader signed_reader({signed_bytes.data(), signed_bytes.size()});
                geocaching::ByteView raw, signature;
                assert(signed_reader.array(fields, 2) && fields == 2 && signed_reader.binary(raw, 4096) && signed_reader.binary(signature, 64));
                w.array(2); w.array(2); w.binary(raw); w.binary(signature); w.unsignedInteger(1);
            }
            else if (operation == 8 || operation == 12) { w.array(4); w.binary({file_hash.data(), 32}); w.unsignedInteger(1024); w.unsignedInteger(0); w.binary({content.data(), content.size()}); }
            else if (operation == 9)
            {
                w.array(8); w.unsignedInteger(1); w.text("trailmate.track.regions.v1"); w.text("test"); w.binary({file_hash.data(), 32});
                w.unsignedInteger(3); w.unsignedInteger(32); w.unsignedInteger(1024); w.array(5); for (unsigned op = 9; op <= 13; ++op) w.unsignedInteger(op);
            }
            else if (operation == 10) { w.array(3); w.binary({file_hash.data(), 32}); w.nil(); w.array(0); }
            else if (operation == 11)
            {
                w.array(3); w.binary({file_hash.data(), 32}); w.array(7); w.text("test:r"); w.nil(); w.unsignedInteger(0); w.text("Root"); w.array(0);
                w.array(4); w.signedInteger(0); w.signedInteger(0); w.signedInteger(1); w.signedInteger(1); w.unsignedInteger(0);
                w.array(3); w.text("region-e7-json-v1"); w.binary({file_hash.data(), 32}); w.unsignedInteger(1024);
            } });
        const auto receive = [&](const Bytes& bytes, bool wrong_source = false)
        {
            const auto hash = package_fixture::hash(bytes);
            auto source = server.destinationHash();
            if (wrong_source) source[0] ^= 1;
            const chat::lxmf::CustomDeliveryView view{{source.data(), 16}, {fixture->author.destinationHash().data(), 16}, {hash.data(), 32}, {bytes.data(), bytes.size()}};
            constexpr char type[] = "trailmate.track.v1";
            return worker.receive({reinterpret_cast<const uint8_t*>(type), sizeof(type) - 1}, view, now);
        };
        if (scenario == "source") assert(!receive(reply, true));
        if (scenario == "forged")
        {
            auto bad = reply;
            bad[bad.size() - 2] ^= 1;
            assert(!receive(bad));
        }
        if (scenario == "disconnect" || scenario == "context" || scenario == "timeout")
        {
            if (scenario == "disconnect") frontend.close();
            if (scenario == "context")
            {
                enabled = false;
                ++context;
            }
            if (scenario == "timeout") now += 45001;
            worker.tick(++now, enabled, context);
            assert(!receive(reply));
            call(mgmt::Operation::TrackReadStatus, query, mgmt::Status::Expired);
        }
        else
        {
            assert(receive(reply) && receive(reply));
            native_track_fixture::Id other{};
            other[0] = 60;
            call(mgmt::Operation::TrackReadStatus, query, mgmt::Status::Unauthorized, other.data());
            const auto metadata = call(mgmt::Operation::TrackReadStatus, query);
            Reader reader({metadata.data(), metadata.size()});
            uint64_t total = 0;
            geocaching::ByteView digest;
            assert(reader.array(fields, 5) && fields == 5 && reader.binary(echoed, 16) && reader.unsignedInteger(value) && value == operation && reader.unsignedInteger(value) && value == 200 && reader.unsignedInteger(total) && total == reply.size() && reader.binary(digest, 32) && reader.finished());
            const auto hash = package_fixture::hash(reply);
            assert(!std::memcmp(hash.data(), digest.data, 32));
            Bytes copied;
            for (size_t offset = 0; offset < total; offset += 320)
            {
                const auto chunk = call(mgmt::Operation::TrackReadChunk, encode([&](Writer& w)
                                                                                { w.array(3); w.binary({request.data(), 16}); w.unsignedInteger(offset); w.unsignedInteger(std::min<size_t>(320, total - offset)); }));
                Reader part({chunk.data(), chunk.size()});
                geocaching::ByteView bytes;
                assert(part.array(fields, 4) && fields == 4 && part.binary(digest, 32) && !std::memcmp(hash.data(), digest.data, 32) && part.unsignedInteger(value) && value == total && part.unsignedInteger(value) && value == offset && part.binary(bytes, 320) && part.finished());
                copied.insert(copied.end(), bytes.data, bytes.data + bytes.size);
            }
            assert(copied == reply && sends == 1);
            assert(call(mgmt::Operation::TrackReadRelease, query) == Bytes{0x90});
            call(mgmt::Operation::TrackReadStatus, query, mgmt::Status::Expired);
            assert(!frontend.identityBusy());
        }
        assert(std::filesystem::is_empty(fixture->sd / "trailmate/track-read"));
    }
    std::filesystem::remove_all(root);
}
