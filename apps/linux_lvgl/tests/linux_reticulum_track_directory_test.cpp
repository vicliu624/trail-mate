#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_ingress.h"
#include "track_sharing/directory_pool.h"
#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace rt = chat::reticulum;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Ingress = trailmate::linux_app::LinuxReticulumIngress;
struct Store : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
struct Observer
{
    std::array<track_sharing::TrackDirectoryEntry, 8> entries{};
    track_sharing::TrackDirectoryPool pool{entries.data(), entries.size()};
    unsigned calls = 0;
    bool throws = false;
    static bool receive(void* context, const chat::lxmf::TrackDirectoryAnnouncementView& view)
    {
        auto& self = *static_cast<Observer*>(context);
        ++self.calls;
        if (self.throws) throw std::runtime_error("injected observer failure");
        return self.pool.observe(view, 100 + self.calls);
    }
};
std::vector<uint8_t> announcement(Identity& identity, uint8_t nonce, bool service = true, bool correct_metadata = true)
{
    std::array<uint8_t, 128> application{};
    std::array<uint8_t, 16> epoch{};
    epoch[0] = 9;
    auto delivery = identity.destinationHash();
    if (!correct_metadata) delivery[0] ^= 1;
    geocaching::protocol::CmpWriter writer(application.data(), application.size());
    assert(writer.array(5) && writer.unsignedInteger(1) && writer.text(track_sharing::kApplicationType) &&
           writer.binary({delivery.data(), 16}) && writer.binary({epoch.data(), 16}) && writer.text("Test directory"));
    uint8_t name[10], destination[16];
    rt::computeNameHash("trailmate", service ? "track.directory" : "other.directory", name);
    rt::computeDestinationHash(name, identity.identityHash().data(), destination);
    std::vector<uint8_t> payload(identity.publicKey().begin(), identity.publicKey().end());
    payload.insert(payload.end(), name, name + 10);
    payload.insert(payload.end(), {nonce, 0, 0, 0, 0, 0, 0, 0, 0, nonce});
    std::vector<uint8_t> domain(destination, destination + 16);
    domain.insert(domain.end(), payload.begin(), payload.end());
    domain.insert(domain.end(), application.data(), application.data() + writer.size());
    std::array<uint8_t, 64> signature{};
    assert(identity.sign(domain.data(), domain.size(), signature.data()));
    payload.insert(payload.end(), signature.begin(), signature.end());
    payload.insert(payload.end(), application.data(), application.data() + writer.size());
    std::vector<uint8_t> frame(rt::kReticulumMtu);
    size_t size = frame.size();
    assert(rt::buildHeader1Packet(rt::PacketType::Announce, rt::DestinationType::Single, rt::PacketContext::None, false,
                                  destination, payload.data(), payload.size(), frame.data(), &size));
    frame.resize(size);
    return frame;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-track-directory-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        Identity local(root / "local.sqlite"), remote(root / "remote.sqlite"), other(root / "other.sqlite");
        assert(local.begin() && remote.begin() && other.begin());
        Store store;
        chat::MeshPeerDirectoryCore directory(store);
        assert(directory.beginEmpty().succeeded());
        Ingress ingress(local, Ingress::Options{}, &directory);
        Observer first;
        ingress.setTrackDirectoryObserver(Observer::receive, &first);
        auto valid = announcement(remote, 1);
        auto tampered = valid;
        tampered.back() ^= 1;
        assert(!ingress.receive(tampered.data(), tampered.size(), 100, 1) && first.calls == 0);
        auto wrong = announcement(remote, 2, false);
        assert(ingress.receive(wrong.data(), wrong.size(), 110, 1) && first.calls == 0);
        auto self = announcement(local, 1);
        assert(!ingress.receive(self.data(), self.size(), 120, 1) && first.calls == 0);
        assert(ingress.receive(valid.data(), valid.size(), 130, 1) && first.calls == 1 && first.pool.size() == 1);
        const auto peer_id = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data()));
        chat::MeshPeerRecord peer;
        assert(directory.find(peer_id, peer).succeeded() && peer.reticulum.has_public_keys);
        assert(!std::memcmp(peer.reticulum.enc_pub, remote.publicKey().data(), 32) && !std::memcmp(peer.reticulum.sig_pub, remote.publicKey().data() + 32, 32));
        assert(!ingress.receive(valid.data(), valid.size(), 140, 1) && first.calls == 1);
        rt::ParsedPacket parsed;
        assert(rt::parsePacket(valid.data(), valid.size(), &parsed));
        const auto before = ingress.path(parsed.destination_hash, 140)->updated_ms;
        Observer second;
        ingress.setTrackDirectoryObserver(Observer::receive, &second);
        uint8_t tag[16]{3};
        assert(ingress.requestPath(
                   parsed.destination_hash, tag, 150, [](void*, const uint8_t*, size_t)
                   { return true; },
                   nullptr) == Ingress::RequestResult::Sent);
        valid[18] = uint8_t(rt::PacketContext::PathResponse);
        assert(ingress.receive(valid.data(), valid.size(), 160, 1) && second.calls == 1 && second.pool.size() == 1);
        assert(ingress.path(parsed.destination_hash, 160)->updated_ms == before); // No replay freshness extension.
        chat::MeshPeerUserFlags flags{};
        flags.ignored = true;
        assert(directory.setUserFlags(peer_id, flags).succeeded());
        auto ignored = announcement(remote, 3);
        assert(ingress.receive(ignored.data(), ignored.size(), 170, 1) && second.calls == 1);
        flags.ignored = false;
        assert(directory.setUserFlags(peer_id, flags).succeeded());
        second.throws = true;
        auto exceptional = announcement(remote, 4);
        assert(ingress.receive(exceptional.data(), exceptional.size(), 180, 1) && ingress.statistics().projection_errors == 1);
        second.throws = false;
        auto mismatched = announcement(other, 1, true, false);
        assert(ingress.receive(mismatched.data(), mismatched.size(), 190, 1) && second.pool.size() == 1);
        const auto other_id = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(other.destinationHash().data(), other.identityHash().data()));
        assert(directory.find(other_id, peer).code == chat::MeshPeerDirectoryStatusCode::NotFound);
    }
    std::filesystem::remove_all(root);
}
