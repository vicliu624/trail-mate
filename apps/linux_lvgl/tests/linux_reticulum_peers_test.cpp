#include "chat/linux_reticulum_ingress.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/peer_directory.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace rt = chat::reticulum;
namespace runtime = chat::lxmf::runtime;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Directory = platform::linux_runtime::LinuxPeerDirectory;
using Ingress = trailmate::linux_app::LinuxReticulumIngress;
struct Blob final : chat::IMeshPeerDirectoryBlobStore
{
    std::vector<uint8_t> bytes;
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>& out) override
    {
        out = bytes;
        return bytes.empty() ? chat::MeshPeerDirectoryBlobLoadResult::Missing : chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }
    bool saveBlob(const uint8_t* data, size_t size) override
    {
        bytes.assign(data, data + size);
        return true;
    }
    void clearBlob() override { assert(false); }
};
struct Sink final : runtime::IPeerProjectionSink
{
    unsigned count = 0;
    bool throwing = false;
    runtime::PeerInfo last;
    void publishPeerUpdate(const runtime::PeerInfo& peer) override
    {
        ++count;
        last = peer;
        if (throwing) throw std::runtime_error("UI wake failed");
    }
};
std::vector<uint8_t> frame(Identity& identity, const char* app, const char* aspect, const char* name, uint8_t nonce)
{
    uint8_t name_hash[10], destination[16];
    rt::computeNameHash(app, aspect, name_hash);
    rt::computeDestinationHash(name_hash, identity.identityHash().data(), destination);
    std::vector<uint8_t> prefix(identity.publicKey().begin(), identity.publicKey().end());
    prefix.insert(prefix.end(), name_hash, name_hash + 10);
    prefix.insert(prefix.end(), {nonce, 0, 0, 0, 0, 0, 0, 0, 0, nonce});
    std::vector<uint8_t> data;
    if (!std::strcmp(app, "lxmf") && !std::strcmp(aspect, "delivery"))
    {
        data.resize(128);
        size_t size = data.size();
        assert(chat::lxmf::packPeerAnnounceAppData(name, false, 0, data.data(), &size));
        data.resize(size);
    }
    else data.assign(name, name + std::strlen(name));
    std::vector<uint8_t> signed_data(destination, destination + 16);
    signed_data.insert(signed_data.end(), prefix.begin(), prefix.end());
    signed_data.insert(signed_data.end(), data.begin(), data.end());
    uint8_t signature[64];
    assert(identity.sign(signed_data.data(), signed_data.size(), signature));
    prefix.insert(prefix.end(), signature, signature + 64);
    prefix.insert(prefix.end(), data.begin(), data.end());
    std::vector<uint8_t> raw(rt::kReticulumMtu);
    size_t size = raw.size();
    assert(rt::buildHeader1Packet(rt::PacketType::Announce, rt::DestinationType::Single, rt::PacketContext::None, false,
                                  destination, prefix.data(), prefix.size(), raw.data(), &size));
    raw.resize(size);
    return raw;
}
chat::MeshPeerRecord synthetic(uint8_t prefix, bool zero_node = false)
{
    chat::MeshPeerRecord record;
    record.valid = true;
    uint8_t destination[16]{}, identity[16]{};
    destination[0] = prefix;
    identity[0] = prefix + 10;
    if (!zero_node)
    {
        destination[14] = 0x12;
        destination[15] = 0x34;
    }
    record.identity = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(destination, identity));
    record.reticulum.identity = record.identity.reticulum;
    record.reticulum.delivery = record.reticulum.has_public_keys = true;
    record.reticulum.enc_pub[0] = 1;
    record.reticulum.sig_pub[0] = 2;
    return record;
}
void identityProjection()
{
    Blob blob;
    Directory directory(blob);
    assert(directory.begin().succeeded());
    chat::contacts::ContactService service(directory);
    service.begin();
    service.setActiveProtocol(chat::MeshProtocol::Reticulum);
    auto first = synthetic(1), second = synthetic(2), zero = synthetic(3, true);
    assert(directory.record(first).succeeded() && directory.record(second).succeeded() && directory.record(zero).succeeded());
    chat::MeshPeerUserFlags trusted;
    trusted.trusted = true;
    assert(directory.setUserFlags(first.identity, trusted).succeeded());
    chat::contacts::NodeUpdate update;
    update.has_protocol = true;
    update.protocol = static_cast<uint8_t>(chat::MeshProtocol::Reticulum);
    update.reticulum_identity = second.identity.reticulum;
    update.long_name = "Second identity";
    update.has_is_ignored = true;
    update.is_ignored = true;
    service.applyNodeUpdate(0x1234, update);
    chat::MeshPeerRecord out;
    assert(directory.find(first.identity, out).succeeded() && out.flags.trusted && !out.flags.ignored);
    assert(directory.find(second.identity, out).succeeded() && out.flags.ignored && !std::strcmp(out.display_name, "Second identity"));
    update.reticulum_identity = zero.identity.reticulum;
    update.long_name = "Zero projection";
    update.has_is_ignored = false;
    service.applyNodeUpdate(0, update);
    assert(directory.find(zero.identity, out).succeeded() && !std::strcmp(out.display_name, "Zero projection"));
    const auto projected = service.getAllPeers();
    assert(projected.size() == 3);
}
int main()
{
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    return 77;
#else
    char root[] = "/tmp/trailmate-reticulum-peers-XXXXXX";
    assert(mkdtemp(root));
    Identity local(std::filesystem::path(root) / "local.db"), remote(std::filesystem::path(root) / "remote.db");
    assert(local.begin() && remote.begin());
    Blob blob;
    {
        Directory directory(blob);
        assert(directory.begin().succeeded());
        Ingress::Options options;
        options.peers = 2;
        Ingress ingress(local, options, &directory);
        Sink sink;
        ingress.setPeerProjectionSink(&sink);
        auto raw = frame(remote, "lxmf", "delivery", "Person", 1);
        assert(ingress.receive(raw.data(), raw.size(), 10, 100));
        assert(sink.count == 1 && !std::strcmp(sink.last.display_name, "Person"));
        const auto id = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data()));
        chat::MeshPeerRecord out;
        assert(directory.find(id, out).succeeded() && out.reticulum.has_public_keys && !out.flags.trusted && !out.flags.favorite);
        assert(!std::memcmp(out.reticulum.enc_pub, remote.publicKey().data(), 32) && !std::memcmp(out.reticulum.sig_pub, remote.publicKey().data() + 32, 32));
        chat::MeshPeerUserFlags flags;
        flags.favorite = flags.ignored = flags.trusted = true;
        assert(directory.setUserFlags(id, flags).succeeded() && directory.setUserAlias(id, "Local alias").succeeded());
        raw = frame(remote, "lxmf", "delivery", "Renamed", 2);
        assert(ingress.receive(raw.data(), raw.size(), 11, 101) && sink.count == 1);
        assert(directory.find(id, out).succeeded() && out.flags.favorite && out.flags.ignored && out.flags.trusted && !std::strcmp(out.user_alias, "Local alias"));
        for (const auto& service : {std::make_pair("nomadnetwork", "node"), std::make_pair("lxmf", "propagation"), std::make_pair("custom", "service"), std::make_pair("call", "audio")})
        {
            raw = frame(remote, service.first, service.second, "Service", 3);
            assert(ingress.receive(raw.data(), raw.size(), 12, 102));
            assert(ingress.statistics().peers == 1 && sink.count == 1);
        }
        Identity phone(std::filesystem::path(root) / "phone.db"), extra(std::filesystem::path(root) / "extra.db");
        assert(phone.begin() && extra.begin());
        raw = frame(phone, "lxst", "telephony", "Phone peer", 1);
        assert(ingress.receive(raw.data(), raw.size(), 13, 103) && ingress.peer(phone.destinationHash().data()));
        raw = frame(extra, "lxmf", "delivery", "Extra", 1);
        assert(ingress.receive(raw.data(), raw.size(), 14, 104) && ingress.statistics().peers == 2);
        assert(!ingress.peer(remote.destinationHash().data()) && directory.find(id, out).succeeded());
        sink.throwing = true;
        raw = frame(extra, "lxmf", "delivery", "Extra updated", 2);
        assert(ingress.receive(raw.data(), raw.size(), 15, 105) && ingress.statistics().projection_errors == 1);
        raw.back() ^= 1;
        assert(!ingress.receive(raw.data(), raw.size(), 16, 106));
        assert(!std::strcmp(ingress.peer(extra.destinationHash().data())->display_name, "Extra updated"));
        assert(directory.flush().succeeded());
    }
    {
        Directory reopened(blob);
        assert(reopened.begin().succeeded());
        const auto id = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data()));
        chat::MeshPeerRecord out;
        assert(reopened.find(id, out).succeeded() && out.flags.ignored && !std::strcmp(out.user_alias, "Local alias"));
    }
    identityProjection();
    std::filesystem::remove_all(root);
#endif
}
