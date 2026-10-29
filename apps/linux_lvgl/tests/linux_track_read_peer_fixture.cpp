#include "chat/linux_track_read.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/hostlink_runtime.h"
#include "signed_route_package_fixture.h"
#include "track_sharing/network_request.h"
#include <chrono>
#include <iostream>
#include <poll.h>
#include <thread>
#include <unistd.h>

namespace native = platform::linux_runtime;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
uint64_t clock_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char** argv)
{
    assert(argc == 3);
    const auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    for (const auto* key : {"TRAIL_MATE_SETTINGS_ROOT", "TRAIL_MATE_CACHE_ROOT", "TRAIL_MATE_SD_ROOT", "TRAIL_MATE_STATE_ROOT"}) setenv(key, root.c_str(), 1);
    setenv("TRAIL_MATE_HOSTLINK_BIND", "127.0.0.1", 1);
    native::LinuxReticulumIdentity local(native::sqlite_database_path()), server(root / "test-directory.sqlite");
    assert(local.begin() && server.begin());
    const auto package = package_fixture::make(argv[2], false);
    std::array<uint8_t, 16> epoch{};
    epoch[0] = 9;
    const auto metadata = encode([&](Writer& w)
                                 { w.array(4); w.binary({server.publicKey().data(), 64}); w.binary({server.destinationHash().data(), 16}); w.binary({epoch.data(), 16}); w.binary({package.signed_manifest.data(), package.signed_manifest.size()}); });
    std::ofstream output(root / "read-fixture.cmp", std::ios::binary);
    assert(output.write(reinterpret_cast<const char*>(metadata.data()), metadata.size()));
    output.close();
    std::vector<Bytes> outgoing;
    uint32_t message = 0;
    trailmate::linux_app::LinuxTrackRead worker(
        root, native::sqlite_database_path(), local,
        [&](const uint8_t*, geocaching::ByteView delivery, geocaching::ByteView selected_epoch, track_sharing::PublicationDirectory& directory)
        {
            if (std::memcmp(delivery.data, server.destinationHash().data(), 16) || std::memcmp(selected_epoch.data, epoch.data(), 16)) return hostlink::management::Status::Conflict;
            directory = {};
            std::copy_n(server.publicKey().data(), 64, directory.public_key);
            std::copy_n(server.destinationHash().data(), 16, directory.delivery);
            std::copy_n(epoch.data(), 16, directory.epoch);
            directory.file_limit = 8388608;
            directory.chunk_limit = 1024;
            directory.query_limit = 32;
            return hostlink::management::Status::Ok;
        },
        [&](const uint8_t* target, const uint8_t* key, chat::lxmf::ByteSpan bytes, chat::MessageId id)
        {
            assert(id && !std::memcmp(target, server.destinationHash().data(), 16) && !std::memcmp(key, server.publicKey().data(), 64));
            outgoing.emplace_back(bytes.data, bytes.data + bytes.size);
            return true;
        },
        [&]
        { return ++message; });
    worker.tick(clock_ms(), true, 1);
    platform::ui::hostlink::start();
    std::string endpoint;
    const auto deadline = clock_ms() + 5000;
    while (endpoint.empty())
    {
        assert(clock_ms() < deadline);
        std::ifstream file(root / "hostlink/endpoint.txt");
        std::getline(file, endpoint);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << endpoint << std::endl;
    while (true)
    {
        pollfd input{STDIN_FILENO, POLLIN, 0};
        if (poll(&input, 1, 0) > 0) break;
        worker.tick(clock_ms(), true, 1);
        for (const auto& request : outgoing)
        {
            geocaching::protocol::CmpReader reader({request.data(), request.size()});
            size_t fields = 0;
            uint64_t version = 0, role = 0, operation = 0, budget = 0;
            geocaching::ByteView id;
            assert(reader.array(fields, 6) && fields == 6 && reader.unsignedInteger(version) && version == 1 && reader.unsignedInteger(role) && role == 0 && reader.unsignedInteger(operation) && reader.binary(id, 16) && reader.unsignedInteger(budget) && budget == 8192);
            const auto response = encode([&](Writer& w)
                                         {
                w.array(6); w.unsignedInteger(1); w.unsignedInteger(1); w.unsignedInteger(operation); w.binary(id); w.unsignedInteger(200);
                if (operation == 0)
                {
                    w.array(8); w.unsignedInteger(1); w.text(track_sharing::kApplicationType); w.binary({server.publicKey().data(), 64}); w.binary({epoch.data(), 16});
                    w.unsignedInteger(8388608); w.unsignedInteger(1024); w.unsignedInteger(32); w.array(9); for (unsigned op = 0; op <= 8; ++op) w.unsignedInteger(op);
                }
                else if (operation == 2)
                {
                    geocaching::protocol::CmpReader signed_reader({package.signed_manifest.data(), package.signed_manifest.size()});
                    geocaching::ByteView manifest, signature;
                    assert(signed_reader.array(fields, 2) && signed_reader.binary(manifest, 4096) && signed_reader.binary(signature, 64));
                    w.array(2); w.array(2); w.binary(manifest); w.binary(signature); w.unsignedInteger(1);
                }
                else if (operation == 1) { w.array(3); w.unsignedInteger(100); w.nil(); w.array(0); }
                else
                {
                    assert(operation == 8 && reader.array(fields, 3) && fields == 3);
                    geocaching::ByteView hash;
                    uint64_t offset = 0, length = 0;
                    assert(reader.binary(hash, 32) && !std::memcmp(hash.data, package.kml_hash.data(), 32) && reader.unsignedInteger(offset) && reader.unsignedInteger(length) && offset + length <= package.kml.size());
                    w.array(4); w.binary(hash); w.unsignedInteger(package.kml.size()); w.unsignedInteger(offset); w.binary({package.kml.data() + offset, size_t(length)});
                } });
            const auto hash = package_fixture::hash(response);
            const chat::lxmf::CustomDeliveryView view{{server.destinationHash().data(), 16}, {local.destinationHash().data(), 16}, {hash.data(), 32}, {response.data(), response.size()}};
            constexpr char type[] = "trailmate.track.v1";
            assert(worker.receive({reinterpret_cast<const uint8_t*>(type), sizeof(type) - 1}, view, clock_ms()));
        }
        outgoing.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    platform::ui::hostlink::stop();
}
