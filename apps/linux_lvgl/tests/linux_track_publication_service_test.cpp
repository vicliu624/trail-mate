#include "chat/linux_track_publication.h"
#include "native_track_directory_fixture.h"
#include "platform/linux/hostlink_track_publication.h"
#include "platform/linux/track_publication_runtime.h"
#include <deque>

namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using Worker = trailmate::linux_app::LinuxTrackPublication;
using package_fixture::Bytes;
using package_fixture::encode;
using package_fixture::Writer;
using Reader = geocaching::protocol::CmpReader;
struct Job
{
    ts::PublicationJournal value;
    bool active = false;
};
Job decode(const Bytes& bytes)
{
    Reader reader({bytes.data(), bytes.size()});
    size_t fields = 0;
    geocaching::ByteView field;
    uint64_t number = 0;
    Job result;
    assert(reader.array(fields, 14) && fields == 14);
    const auto binary = [&](uint8_t* output, size_t size)
    {assert(reader.binary(field,size) && field.size==size);std::copy_n(field.data,size,output); };
    binary(result.value.operation, 16);
    binary(result.value.object, 32);
    binary(result.value.revision_hash, 32);
    assert(reader.unsignedInteger(number));
    result.value.generation = number;
    assert(reader.unsignedInteger(number));
    result.value.revision = number;
    binary(result.value.directory, 16);
    binary(result.value.directory_epoch, 16);
    binary(result.value.file_hash, 32);
    assert(reader.unsignedInteger(number));
    result.value.file_bytes = number;
    assert(reader.unsignedInteger(number));
    result.value.offset = number;
    assert(reader.unsignedInteger(number));
    result.value.phase = ts::PublicationPhase(number);
    assert(reader.unsignedInteger(result.value.sequence));
    binary(result.value.author_hash, 32);
    assert(reader.unsignedInteger(number) && number <= 1 && reader.finished());
    result.active = number;
    assert(ts::publicationJournalValid(result.value));
    return result;
}
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string scenario(argv[2]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-publication-service-" + std::to_string(getpid()));
    {
        auto fixture = std::make_unique<native_track_fixture::Fixture>(root, argv[1], scenario == "v2");
        native::LinuxReticulumIdentity server(root / "directory.sqlite");
        assert(server.begin());
        native_track_fixture::Directory remote(*fixture, server);
        native::HostlinkTrackPublication frontend(fixture->database), wrong(root / "wrong.sqlite");
        assert(!frontend.supported());
        std::deque<Bytes> outgoing;
        uint32_t next_message = 0;
        unsigned sends = 0, rejected = 0;
        uint64_t now = 1, context = 1;
        bool enabled = true, network = scenario != "cancel" && scenario != "cancel_idle", drop = scenario == "lost", dropped = false;
        const auto make_worker = [&]
        {
            return std::make_unique<Worker>(
                fixture->sd, fixture->database, fixture->author,
                [&](const uint8_t* session, geocaching::ByteView delivery, geocaching::ByteView epoch, ts::PublicationDirectory& output)
                {assert(!std::memcmp(session,fixture->session.data(),16));if(std::memcmp(delivery.data,remote.binding.delivery,16)||std::memcmp(epoch.data,remote.binding.epoch,16))return mgmt::Status::Conflict;output=remote.binding;return mgmt::Status::Ok; },
                [&](const uint8_t* target, const uint8_t* key, chat::lxmf::ByteSpan bytes, chat::MessageId id)
                {assert(id && !std::memcmp(target,remote.binding.delivery,16) && !std::memcmp(key,remote.binding.public_key,64));if(!network)return false;++sends;outgoing.emplace_back(bytes.data,bytes.data+bytes.size);return true; },
                [&]
                { return ++next_message; });
        };
        auto worker = make_worker();
        worker->tick(now, enabled, context);
        assert(frontend.supported() && !wrong.supported());
        const auto drive = [&]
        {
            now += 100;
            worker->tick(now, enabled, context);
            if (outgoing.empty()) return;
            auto packet = std::move(outgoing.front());
            outgoing.pop_front();
            auto reply = remote.handle(packet, scenario == "forged");
            if (remote.last_operation == 7 && drop && !dropped)
            {
                dropped = true;
                return;
            }
            Bytes domain(server.destinationHash().begin(), server.destinationHash().end());
            domain.insert(domain.end(), reply.begin(), reply.end());
            const auto hash = package_fixture::hash(domain);
            const chat::lxmf::CustomDeliveryView view{{server.destinationHash().data(), 16}, {fixture->author.destinationHash().data(), 16}, {hash.data(), 32}, {reply.data(), reply.size()}};
            constexpr char type[] = "trailmate.track.v1";
            const bool accepted = worker->receive({reinterpret_cast<const uint8_t*>(type), sizeof(type) - 1}, view, now);
            if (scenario == "forged" && remote.last_operation == 7)
            {
                assert(!accepted);
                ++rejected;
            }
            else assert(accepted);
        };
        const auto submit = encode([&](Writer& w)
                                   {w.array(7);w.binary({fixture->operation.data(),16});w.binary({fixture->object.data(),32});w.binary({fixture->revision.data(),32});
            w.unsignedInteger(fixture->author.generation());w.unsignedInteger(1);w.binary({remote.binding.delivery,16});w.binary({remote.binding.epoch,16}); });
        const auto query = encode([&](Writer& w)
                                  {w.array(3);w.binary({fixture->operation.data(),16});w.unsignedInteger(fixture->author.generation());w.unsignedInteger(1); });
        const auto poll = [&](mgmt::Operation operation, const Bytes& body, mgmt::Status expected = mgmt::Status::Ok)
        {
            std::array<uint8_t, 448> bytes{};
            size_t written = 0;
            mgmt::EnvelopeView request{fixture->session.data(), nullptr, uint16_t(operation), body.data(), body.size()};
            mgmt::Status status;
            unsigned attempts = 0;
            do
            {
                status = frontend.process(request, bytes.data(), bytes.size(), written);
                if (status == mgmt::Status::Busy) drive();
                assert(++attempts < 10000);
            } while (status == mgmt::Status::Busy);
            assert(status == expected);
            return Bytes(bytes.begin(), bytes.begin() + written);
        };
        const auto admission = poll(mgmt::Operation::TrackCommit, submit);
        assert(!admission.empty());
        auto status = decode(poll(mgmt::Operation::JobGet, query));
        assert(status.value.phase != ts::PublicationPhase::Confirmed && status.active);
        if (scenario == "cancel" || scenario == "cancel_idle")
        {
            if (scenario == "cancel_idle")
            {
                enabled = false;
                ++context;
                drive();
            }
            const auto cancelled = decode(poll(mgmt::Operation::JobCancel, query));
            assert(cancelled.value.phase == ts::PublicationPhase::Cancelled && !cancelled.active && !sends);
            worker.reset();
            worker = make_worker();
            worker->tick(++now, enabled, context);
            assert(decode(poll(mgmt::Operation::JobCancel, query)).value.phase == ts::PublicationPhase::Cancelled);
        }
        else
        {
            assert(scenario == "confirm" || scenario == "v2" || scenario == "lost" || scenario == "detached" || scenario == "forged");
            if (scenario == "detached") frontend.close(fixture->session.data());
            for (unsigned step = 0; step < 10000; ++step)
            {
                drive();
                status = decode(poll(mgmt::Operation::JobGet, query));
                if ((status.value.phase == ts::PublicationPhase::Confirmed && !status.active) || (scenario == "forged" && !status.active)) break;
                assert(step < 9999);
            }
            if (scenario == "forged") assert(rejected && status.value.phase == ts::PublicationPhase::Publishing && !status.active);
            else
            {
                assert(status.value.phase == ts::PublicationPhase::Confirmed && !status.active);
                if (scenario == "lost") assert(dropped);
                const auto previous = sends;
                worker.reset();
                assert(!frontend.supported());
                worker = make_worker();
                worker->tick(++now, enabled, context);
                const auto recovered = decode(poll(mgmt::Operation::JobGet, query));
                assert(recovered.value.phase == ts::PublicationPhase::Confirmed && !recovered.active && recovered.value.sequence == status.value.sequence && sends == previous);
            }
        }
        assert(fixture->author.ready() && fixture->author.generation() == 1);
    }
    std::filesystem::remove_all(root);
}
