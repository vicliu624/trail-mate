#include "chat/linux_reticulum_ingress.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace rt = chat::reticulum;
using Ingress = trailmate::linux_app::LinuxReticulumIngress;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
std::vector<uint8_t> unhex(const char* text)
{
    std::vector<uint8_t> bytes;
    while (*text)
    {
        const auto digit = [](char c)
        { return c <= '9' ? c - '0' : c - 'a' + 10; };
        bytes.push_back(static_cast<uint8_t>((digit(text[0]) << 4) | digit(text[1])));
        text += 2;
    }
    return bytes;
}
std::vector<uint8_t> announce(Identity& identity, uint8_t nonce, bool ratchet = false)
{
    uint8_t name[10];
    rt::computeNameHash("lxmf", "delivery", name);
    std::vector<uint8_t> payload(identity.publicKey().begin(), identity.publicKey().end());
    payload.insert(payload.end(), name, name + sizeof(name));
    payload.insert(payload.end(), {nonce, 0, 0, 0, 0, 0, 0, 0, 0, nonce});
    if (ratchet) payload.insert(payload.end(), identity.publicKey().begin(), identity.publicKey().begin() + 32);
    std::vector<uint8_t> signed_data(identity.destinationHash().begin(), identity.destinationHash().end());
    signed_data.insert(signed_data.end(), payload.begin(), payload.end());
    signed_data.push_back('A');
    uint8_t signature[64];
    assert(identity.sign(signed_data.data(), signed_data.size(), signature));
    payload.insert(payload.end(), signature, signature + sizeof(signature));
    payload.push_back('A');
    std::vector<uint8_t> raw(rt::kReticulumMtu);
    size_t size = raw.size();
    assert(rt::buildHeader1Packet(rt::PacketType::Announce, rt::DestinationType::Single, rt::PacketContext::None,
                                  ratchet, identity.destinationHash().data(), payload.data(), payload.size(), raw.data(), &size));
    raw.resize(size);
    return raw;
}
void requests(Identity& local, Identity& remote)
{
    Ingress::Options options;
    options.request_interval_ms = 20;
    options.request_ttl_ms = 45;
    options.pending_paths = 2;
    Ingress ingress(local, options);
    struct Sender
    {
        bool accepted = true;
        unsigned count = 0;
        std::vector<uint8_t> bytes;
    } sender;
    const auto send = [](void* context, const uint8_t* bytes, size_t size)
    {
        auto& sender = *static_cast<Sender*>(context);
        ++sender.count;
        sender.bytes.assign(bytes, bytes + size);
        return sender.accepted;
    };
    std::array<uint8_t, 16> tag{};
    tag[0] = 7;
    using Result = Ingress::RequestResult;
    assert(ingress.requestPath(remote.destinationHash().data(), tag.data(), 0, send, &sender) == Result::Sent);
    assert(sender.count == 1 && ingress.pendingPath(remote.destinationHash().data()));
    assert(ingress.requestPath(remote.destinationHash().data(), tag.data(), 1, send, &sender) == Result::CoolingDown && sender.count == 1);
    auto raw = announce(remote, 1);
    assert(ingress.receive(raw.data(), raw.size(), 2, 100));
    assert(!ingress.pendingPath(remote.destinationHash().data()));
    const auto updated = ingress.path(remote.destinationHash().data(), 2)->updated_ms;
    assert(ingress.requestPath(remote.destinationHash().data(), tag.data(), 3, send, &sender) == Result::Sent);
    raw[18] = static_cast<uint8_t>(rt::PacketContext::PathResponse);
    auto tampered = raw;
    tampered.back() ^= 1;
    assert(!ingress.receive(tampered.data(), tampered.size(), 4, 100) && ingress.pendingPath(remote.destinationHash().data()));
    assert(ingress.receive(raw.data(), raw.size(), 5, 100));
    assert(!ingress.pendingPath(remote.destinationHash().data()) && ingress.statistics().responses == 1);
    assert(ingress.path(remote.destinationHash().data(), 5)->updated_ms == updated);
    assert(!ingress.receive(raw.data(), raw.size(), 6, 100));
    sender.accepted = false;
    auto other = remote.destinationHash();
    other[0] ^= 1;
    assert(ingress.requestPath(other.data(), tag.data(), 7, send, &sender) == Result::TxFailed);
    assert(!ingress.pendingPath(other.data()) && ingress.statistics().request_errors == 1);
    sender.accepted = true;
    assert(ingress.requestPath(other.data(), tag.data(), 8, send, &sender) == Result::Sent);
    ingress.tick(54);
    assert(!ingress.pendingPath(other.data()));
    std::array<std::array<uint8_t, 16>, 3> targets{};
    for (unsigned i = 0; i < 3; ++i)
    {
        targets[i][0] = i + 1;
        assert(ingress.requestPath(targets[i].data(), tag.data(), 60 + i, send, &sender) == Result::Sent);
    }
    assert(!ingress.pendingPath(targets[0].data()) && ingress.pendingPath(targets[2].data()));
    Ingress wrapped(local, options);
    assert(wrapped.requestPath(other.data(), tag.data(), UINT32_MAX - 5, send, &sender) == Result::Sent);
    assert(wrapped.requestPath(other.data(), tag.data(), 3, send, &sender) == Result::CoolingDown);
    wrapped.tick(50);
    assert(!wrapped.pendingPath(other.data()));
}
int main()
{
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    return 77;
#else
    char root[] = "/tmp/trailmate-reticulum-ingress-XXXXXX";
    assert(mkdtemp(root));
    Identity local(std::filesystem::path(root) / "local.db"), remote(std::filesystem::path(root) / "remote.db");
    assert(local.begin() && remote.begin());
    requests(local, remote);
    const char* references[] = {
#include "reticulum_announce_vectors.inc"
    };
    // Checked-in external direct/rebroadcast and ratchet vectors, verified by
    // the same core component used by the embedded client.
    for (const auto* encoded : references)
    {
        const auto raw = unhex(encoded);
        Ingress ingress(local);
        assert(ingress.receive(raw.data(), raw.size(), 10, 100));
        rt::ParsedPacket parsed;
        assert(rt::parsePacket(raw.data(), raw.size(), &parsed));
        const auto* path = ingress.path(parsed.destination_hash, 10);
        assert(path && path->hops == parsed.hops + 1 && path->cached_announce_len == raw.size());
        assert(path->direct == (parsed.transport_id == nullptr));
        if (parsed.transport_id) assert(!std::memcmp(path->next_hop_transport, parsed.transport_id, 16));
        assert(!ingress.receive(raw.data(), raw.size(), 11, 100) && ingress.statistics().replays == 1);
    }
    Ingress::Options options;
    options.paths = 2;
    options.path_ttl_ms = 100;
    options.packet_ttl_ms = 10;
    options.interface_id = 7;
    Ingress ingress(local, options);
    auto raw = announce(remote, 1);
    assert(ingress.receive(raw.data(), raw.size(), 10, 100));
    assert(ingress.path(remote.destinationHash().data(), 10)->interface_id == 7);
    assert(!ingress.receive(raw.data(), raw.size(), 30, 100)); // Random-blob history survives packet-filter expiry.
    auto newer = announce(remote, 2, true);
    assert(ingress.receive(newer.data(), newer.size(), 31, 100));
    auto stale = announce(remote, 1, true);
    assert(!ingress.receive(stale.data(), stale.size(), 32, 100));
    auto invalid = announce(remote, 3);
    invalid.back() ^= 1;
    assert(!ingress.receive(invalid.data(), invalid.size(), 33, 100));
    assert(ingress.path(remote.destinationHash().data(), 33)->updated_ms == 31);
    invalid = announce(remote, 3);
    invalid[2] ^= 1;
    assert(!ingress.receive(invalid.data(), invalid.size(), 34, 100));
    auto own = announce(local, 1);
    assert(!ingress.receive(own.data(), own.size(), 35, 100) && !ingress.path(local.destinationHash().data(), 35));
    auto too_many_hops = announce(remote, 4);
    too_many_hops[1] = 128;
    assert(!ingress.receive(too_many_hops.data(), too_many_hops.size(), 36, 100));
    assert(!ingress.receive(nullptr, 0, 37, 100));
    ingress.tick(132);
    assert(!ingress.path(remote.destinationHash().data(), 132) && ingress.statistics().paths == 0);
    Identity other(std::filesystem::path(root) / "other.db"), third(std::filesystem::path(root) / "third.db");
    assert(other.begin() && third.begin());
    for (auto* identity : {&remote, &other, &third})
    {
        auto frame = announce(*identity, 7);
        assert(ingress.receive(frame.data(), frame.size(), 150, 100));
    }
    assert(ingress.statistics().paths == 2);
    ingress.reset();
    assert(ingress.statistics().paths == 0 && !ingress.statistics().accepted);
    std::filesystem::remove_all(root);
#endif
}
