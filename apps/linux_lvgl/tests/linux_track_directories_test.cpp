#include "chat/linux_track_directories.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/hostlink_track_directories.h"
#include "platform/linux/track_directory_runtime.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
using Identity = native::LinuxReticulumIdentity;
using Worker = trailmate::linux_app::LinuxTrackDirectories;
using Writer = geocaching::protocol::CmpWriter;
using Reader = geocaching::protocol::CmpReader;
using Bytes = std::vector<uint8_t>;
uint64_t clock_ms() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
template <class Fn>
Bytes encode(Fn fn)
{
    Bytes bytes(448);
    Writer writer(bytes.data(), bytes.size());
    fn(writer);
    assert(writer.good());
    bytes.resize(writer.size());
    return bytes;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-directory-bridge-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto database = root / "local.sqlite";
    native::HostlinkTrackDirectories frontend(database), wrong(root / "other.sqlite");
    assert(!frontend.supported());
    uint8_t session[16]{1}, second[16]{2};
    const auto call = [&](mgmt::Operation operation, const Bytes& body, mgmt::Status expected = mgmt::Status::Ok, const uint8_t* requester = nullptr)
    {
        std::array<uint8_t, 448> output{};
        size_t written = 0;
        mgmt::EnvelopeView request;
        request.session = requester ? requester : session;
        request.code = uint16_t(operation);
        request.body = body.data();
        request.body_size = body.size();
        assert(frontend.process(request, output.data(), output.size(), written) == expected);
        return Bytes(output.begin(), output.begin() + written);
    };
    const auto page = encode([](Writer& w)
                             {w.array(1);w.unsignedInteger(0); });
    const auto continuation = encode([](Writer& w)
                                     {w.array(1);w.unsignedInteger(1); });
    {
        Identity local(database), remote(root / "remote.sqlite");
        assert(local.begin() && remote.begin());
        std::vector<std::array<uint8_t, 16>> paths;
        Bytes outbound;
        uint32_t messages = 0;
        Worker::Transport transport;
        transport.request_path = [&](const uint8_t* target)
        { std::array<uint8_t,16> value{}; std::copy_n(target,16,value.begin()); paths.push_back(value); return true; };
        transport.send = [&](const uint8_t* destination, const uint8_t* key, chat::lxmf::ByteSpan data, chat::MessageId id)
        { assert(id && !std::memcmp(destination,remote.destinationHash().data(),16) && !std::memcmp(key,remote.publicKey().data(),64)); outbound.assign(data.data,data.data+data.size); return true; };
        transport.next_message = [&]
        { return ++messages; };
        Worker worker(database, local, std::move(transport));
        worker.tick(clock_ms(), true, true, 1);
        assert(frontend.supported() && !wrong.supported());
        {
            native::TrackDirectoryPublisher duplicate(database);
            assert(!duplicate.valid());
        }
        assert(frontend.supported());
        const auto captured_before_refresh = clock_ms() - 10;
        assert(call(mgmt::Operation::TrackDirectoryList, page) == Bytes({0x92, 0xc0, 0x90}));
        worker.tick(captured_before_refresh, true, true, 1);
        call(mgmt::Operation::TrackDirectoryList, page, mgmt::Status::Conflict, second);
        worker.tick(clock_ms() + 1, true, true, 1);
        std::array<uint8_t, 16> epoch{}, discovery{};
        epoch[0] = 7;
        discovery[0] = 8;
        const auto advertisement = [&]
        { return encode([&](Writer& w)
                        { w.array(5);w.unsignedInteger(1);w.text(track_sharing::kApplicationType);w.binary({remote.destinationHash().data(),16});w.binary({epoch.data(),16});w.text("Test directory"); }); };
        auto metadata = advertisement();
        const auto observe = [&]
        { const chat::lxmf::TrackDirectoryAnnouncementView view{{discovery.data(),16},{remote.destinationHash().data(),16},{remote.publicKey().data(),64},{metadata.data(),metadata.size()}}; return worker.observe(view,clock_ms()+2); };
        assert(observe());
        worker.tick(clock_ms() + 3, true, true, 1);
        assert(!outbound.empty());
        const auto request_id = [&]
        {
            Reader reader({outbound.data(), outbound.size()});
            size_t fields = 0;
            uint64_t value = 0;
            geocaching::ByteView id;
            assert(reader.array(fields, 6) && fields == 6);
            for (const unsigned expected : {1, 0, 0}) assert(reader.unsignedInteger(value) && value == expected);
            assert(reader.binary(id, 16) && id.size == 16);
            std::array<uint8_t, 16> result{};
            std::copy_n(id.data, 16, result.begin());
            return result;
        };
        auto nonce = request_id();
        const auto response = [&](std::array<uint8_t, 16> request, std::array<uint8_t, 16> incarnation)
        {
            return encode([&](Writer& w)
                          { w.array(6);w.unsignedInteger(1);w.unsignedInteger(1);w.unsignedInteger(0);w.binary({request.data(),16});w.unsignedInteger(200);
                w.array(8);w.unsignedInteger(1);w.text(track_sharing::kApplicationType);w.binary({remote.publicKey().data(),64});w.binary({incarnation.data(),16});
                w.unsignedInteger(8388608);w.unsignedInteger(1024);w.unsignedInteger(32);w.array(9);for(unsigned i=0;i<9;++i)w.unsignedInteger(i); });
        };
        const auto receive = [&](const Bytes& bytes, uint8_t message)
        {
            std::array<uint8_t, 32> hash{};
            hash[0] = message;
            const chat::lxmf::CustomDeliveryView view{{remote.destinationHash().data(), 16}, {local.destinationHash().data(), 16}, {hash.data(), 32}, {bytes.data(), bytes.size()}};
            constexpr char type[] = "trailmate.track.v1";
            return worker.receive({reinterpret_cast<const uint8_t*>(type), sizeof(type) - 1}, view, clock_ms() + 4);
        };
        auto bad_nonce = nonce;
        bad_nonce[0] ^= 1;
        assert(!receive(response(bad_nonce, epoch), 1));
        auto bad_epoch = epoch;
        bad_epoch[0] ^= 1;
        assert(!receive(response(nonce, bad_epoch), 2));
        const auto valid = response(nonce, epoch);
        assert(receive(valid, 3) && receive(valid, 3));
        worker.tick(clock_ms() + 5, true, true, 1);
        const auto listed = call(mgmt::Operation::TrackDirectoryList, page);
        Reader reader({listed.data(), listed.size()});
        size_t fields = 0;
        geocaching::ByteView raw;
        std::string_view name;
        uint64_t number = 0;
        assert(reader.array(fields, 2) && reader.nil() && reader.array(fields, 1) && fields == 1 && reader.array(fields, 8) && fields == 8);
        assert(reader.binary(raw, 16) && !std::memcmp(raw.data, remote.destinationHash().data(), 16));
        assert(reader.binary(raw, 64) && !std::memcmp(raw.data, remote.publicKey().data(), 64));
        assert(reader.binary(raw, 16) && !std::memcmp(raw.data, epoch.data(), 16) && reader.text(name, 40) && name == "Test directory");
        for (const uint64_t expected : {1, 8388608, 1024, 32}) assert(reader.unsignedInteger(number) && number == expected);
        assert(reader.finished());
        call(mgmt::Operation::TrackDirectoryList, page, mgmt::Status::Conflict, second);
        frontend.close(second);
        assert(call(mgmt::Operation::TrackDirectoryList, page) == listed);
        epoch[0] = 9;
        metadata = advertisement();
        assert(observe());
        worker.tick(clock_ms() + 6, true, true, 1);
        const auto changed_nonce = request_id();
        assert(changed_nonce != nonce);
        assert(receive(valid, 3)); // Duplicate acceptance cannot restore an old epoch.
        assert(!receive(response(nonce, epoch), 4));
        assert(receive(response(changed_nonce, epoch), 5));
        frontend.close(session);
        call(mgmt::Operation::TrackDirectoryList, continuation, mgmt::Status::Expired, second);
        assert(call(mgmt::Operation::TrackDirectoryList, page, mgmt::Status::Ok, second) == Bytes({0x92, 0xc0, 0x90}));
        worker.tick(clock_ms() + 7, true, true, 1);
        for (unsigned index = 1; index <= 8; ++index)
        {
            uint8_t target[16]{};
            target[0] = index;
            const auto request = encode([&](Writer& w)
                                        {w.array(1);w.binary({target,16}); });
            call(mgmt::Operation::TrackDirectoryDiscover, request, mgmt::Status::Ok, second);
            call(mgmt::Operation::TrackDirectoryDiscover, request, mgmt::Status::Ok, second); // Queue deduplication.
        }
        uint8_t ninth[16]{9};
        call(mgmt::Operation::TrackDirectoryDiscover, encode([&](Writer& w)
                                                             {w.array(1);w.binary({ninth,16}); }),
             mgmt::Status::Busy, second);
        worker.tick(clock_ms() + 8, true, true, 1);
        assert(paths.size() == 1);
        frontend.close(second);
        worker.tick(clock_ms() + 9, true, true, 1);
        assert(paths.size() == 1);
        call(mgmt::Operation::TrackDirectoryList, page);
        worker.tick(clock_ms() + 30001, true, true, 1);
        call(mgmt::Operation::TrackDirectoryList, continuation, mgmt::Status::Expired);
        call(mgmt::Operation::TrackDirectoryList, page);
        worker.tick(clock_ms() + 10, false, true, 2);
        assert(!receive(valid, 3));
        call(mgmt::Operation::TrackDirectoryDiscover, encode([&](Writer& w)
                                                             {w.array(1);w.binary({ninth,16}); }),
             mgmt::Status::Busy);
    }
    assert(!frontend.supported());
    call(mgmt::Operation::TrackDirectoryList, page, mgmt::Status::Unsupported);
    std::filesystem::remove_all(root);
}
