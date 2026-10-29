#include "native_track_directory_fixture.h"
#include "platform/linux/hostlink_track_publication_execution.h"
#include "platform/linux/hostlink_track_signer.h"
#include <deque>
namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using package_fixture::Bytes;
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string scenario(argv[2]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-publication-network-" + std::to_string(getpid()));
    {
        const bool assets = scenario.rfind("v2", 0) == 0;
        auto fixture = std::make_unique<native_track_fixture::Fixture>(root, argv[1], assets);
        native::LinuxReticulumIdentity server(root / "directory.sqlite");
        assert(server.begin());
        native_track_fixture::Directory remote(*fixture, server);
        auto& directory = remote.binding;
        ts::CommitRequest request{{fixture->operation.data(), 16}, {fixture->object.data(), 32}, {fixture->revision.data(), 32}, {directory.delivery, 16}, {directory.epoch, 16}, fixture->author.generation(), 1};
        native::UploadIo io(fixture->sd);
        assert(io.lock());
        native::HostlinkTrackSigner signer(fixture->author);
        std::array<uint8_t, 448> scratch{};
        std::deque<Bytes> outgoing;
        uint32_t message = 0;
        const auto make_execution = [&]
        {
            auto input = std::make_unique<native::HostlinkTrackPublicationAdmission>(request, directory, io, fixture->author.publicKey().data(), fixture->actor.data());
            mgmt::Status status;
            do
            {
                status = input->step(signer, scratch.data(), scratch.size());
            } while (status == mgmt::Status::Busy);
            assert(status == mgmt::Status::Ok);
            return std::make_unique<native::HostlinkTrackPublicationExecution>(
                std::move(input), fixture->author,
                [&](const uint8_t* destination, const uint8_t* key, chat::lxmf::ByteSpan data, chat::MessageId id)
                {assert(id && !std::memcmp(destination,directory.delivery,16) && !std::memcmp(key,directory.public_key,64));if(scenario=="dispatch")return false;outgoing.emplace_back(data.data,data.data+data.size);return true; },
                [&]
                { return ++message; });
        };
        auto execution = make_execution();
        assert(execution->ready() && execution->result()->phase == ts::PublicationPhase::Accepted);
        if (scenario == "cancel")
        {
            assert(execution->cancel(fixture->author.generation(), {fixture->actor.data(), 32}) == ts::PublicationStoreResult::Ready);
            assert(execution->result()->phase == ts::PublicationPhase::Cancelled && outgoing.empty());
        }
        else if (scenario == "media")
        {
            std::filesystem::rename(fixture->sd, root / "old-sd");
            std::filesystem::create_directories(fixture->sd);
            assert(execution->advance(1) == ts::NetworkSessionResult::Conflict && outgoing.empty());
        }
        else if (scenario == "dispatch")
        {
            assert(execution->advance(1) == ts::NetworkSessionResult::Rejected && outgoing.empty());
            assert(execution->result()->phase != ts::PublicationPhase::Confirmed);
        }
        else if (scenario == "v2_corrupt")
        {
            std::array<char, 160> path{};
            assert(ts::resolveObjectPath(request.object, ts::ObjectFile::AttachmentResource, {fixture->package.image_hash.data(), 32}, path.data(), path.size()));
            std::ofstream(fixture->sd / (path.data() + 1)) << std::string(fixture->package.image.size(), 'x');
            ts::NetworkSessionResult result;
            unsigned steps = 0;
            do
            {
                result = execution->advance(++steps);
                assert(steps < 10000);
            } while (result == ts::NetworkSessionResult::Progress);
            assert(result == ts::NetworkSessionResult::Conflict && outgoing.empty() && execution->result()->phase == ts::PublicationPhase::Accepted);
        }
        else
        {
            assert(scenario == "confirm" || scenario == "lost_upload" || scenario == "lost_publish" || scenario == "forged" || scenario == "source" ||
                   scenario == "cancel_publish" || scenario == "v2" || scenario == "v2_lost" || scenario == "v2_restart" || scenario == "v2_pause");
            bool finished = false, dropped = false, restarted = false, paused = false;
            for (uint64_t now = 1; now < 500000 && !finished; now += 100)
            {
                const auto result = execution->advance(now);
                if (result == ts::NetworkSessionResult::Confirmed)
                {
                    finished = true;
                    break;
                }
                if (scenario == "v2_pause" && !paused)
                {
                    execution->pause();
                    assert(execution->advance(now + 1) == ts::NetworkSessionResult::Paused);
                    assert(execution->resume(now + 2));
                    paused = true;
                }
                if (outgoing.empty()) continue;
                auto packet = std::move(outgoing.front());
                outgoing.pop_front();
                auto reply = remote.handle(packet, scenario == "forged");
                if (remote.last_operation == 7)
                {
                    assert(execution->result()->phase == ts::PublicationPhase::Publishing);
                    if (scenario == "cancel_publish") assert(execution->cancel(fixture->author.generation(), {fixture->actor.data(), 32}) == ts::PublicationStoreResult::Conflict);
                }
                const bool lose = (scenario == "lost_upload" && remote.last_operation == 5) || (scenario == "lost_publish" && remote.last_operation == 7) ||
                                  (scenario == "v2_lost" && remote.last_operation == 5 && remote.last_hash == fixture->package.image_hash);
                if (lose && !dropped)
                {
                    dropped = true;
                    continue;
                }
                auto source = server.destinationHash();
                if (scenario == "source") source[0] ^= 1;
                Bytes domain(source.begin(), source.end());
                domain.insert(domain.end(), reply.begin(), reply.end());
                const auto hash = package_fixture::hash(domain);
                const chat::lxmf::CustomDeliveryView view{{source.data(), 16}, {fixture->author.destinationHash().data(), 16}, {hash.data(), 32}, {reply.data(), reply.size()}};
                constexpr char type[] = "trailmate.track.v1";
                const chat::lxmf::ByteSpan application{reinterpret_cast<const uint8_t*>(type), sizeof(type) - 1};
                const bool accepted = execution->receive(application, view);
                if (scenario == "source" || (scenario == "forged" && remote.last_operation == 7))
                {
                    assert(!accepted && execution->result()->phase != ts::PublicationPhase::Confirmed);
                    finished = true;
                    break;
                }
                assert(accepted && execution->receive(application, view));
                if (scenario == "v2_restart" && !restarted && remote.last_operation == 5 && remote.last_hash == fixture->package.image_hash)
                {
                    assert(execution->result()->phase == ts::PublicationPhase::Accepted);
                    execution.reset();
                    execution = make_execution();
                    restarted = true;
                    assert(!execution->receive(application, view));
                }
            }
            assert(finished);
            if (scenario != "source" && scenario != "forged")
            {
                assert(execution->result()->phase == ts::PublicationPhase::Confirmed);
                if (scenario == "lost_upload" || scenario == "lost_publish" || scenario == "v2_lost") assert(dropped);
                if (scenario == "v2_restart") assert(restarted);
                if (scenario == "v2_pause") assert(paused);
                execution.reset();
                auto history = std::make_unique<native::HostlinkTrackPublicationAdmission>(request, directory, io, fixture->author.publicKey().data(), fixture->actor.data(), true);
                mgmt::Status status;
                do
                {
                    status = history->step(signer, scratch.data(), scratch.size());
                } while (status == mgmt::Status::Busy);
                assert(status == mgmt::Status::Ok && history->result()->phase == ts::PublicationPhase::Confirmed);
            }
        }
    }
    std::filesystem::remove_all(root);
}
