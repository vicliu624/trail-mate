#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "chat/linux_reticulum_links.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <filesystem>
#include <unistd.h>
#include <vector>

using Links = trailmate::linux_app::LinuxReticulumLinks;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
namespace r = chat::reticulum;
struct Wire
{
    std::deque<std::vector<uint8_t>> packets;
    bool fail = false;
    static bool send(void* context, const uint8_t* data, size_t size)
    {
        auto& self = *static_cast<Wire*>(context);
        if (self.fail) return false;
        self.packets.emplace_back(data, data + size);
        return true;
    }
    std::vector<uint8_t> pop()
    {
        assert(!packets.empty());
        auto value = std::move(packets.front());
        packets.pop_front();
        return value;
    }
};
struct Events
{
    unsigned admitted = 0, received = 0, closed = 0, validated = 0;
    bool accept = true;
    Links::CallPeer peer;
    std::vector<uint8_t> payload;
    Links::CallHooks hooks()
    {
        return {
            [](void* context, const Links::CallPeer& peer)
            {
                auto& self = *static_cast<Events*>(context);
                ++self.admitted;
                self.peer = peer;
                return self.accept;
            },
            [](void* context, const Links::CallPeer& peer, const uint8_t* data, size_t size)
            {
                auto& self = *static_cast<Events*>(context);
                assert(!std::memcmp(peer.link, self.peer.link, 16));
                ++self.received;
                self.payload.assign(data, data + size);
                return true;
            },
            [](void* context, const Links::CallPeer&)
            { ++static_cast<Events*>(context)->closed; },
            this};
    }
};
std::array<uint8_t, 16> callDestination(const Identity& identity)
{
    uint8_t name[10];
    std::array<uint8_t, 16> result;
    r::computeNameHash("lxst", "telephony", name);
    r::computeDestinationHash(name, identity.identityHash().data(), result.data());
    return result;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-call-links-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    Identity alice(root / "alice.sqlite"), bob(root / "bob.sqlite");
    assert(alice.begin() && bob.begin());
    const auto alice_destination = callDestination(alice), bob_destination = callDestination(bob);
    Events a, b;
    Links caller(alice), callee(bob);
    caller.setCallHooks(a.hooks());
    callee.setCallHooks(b.hooks());
    Wire ab, ba;
    uint8_t link[16];
    assert(caller.openCall(bob.destinationHash().data(), bob.publicKey().data(), link, 0, Wire::send, &ab) == Links::OpenResult::Invalid);
    assert(caller.openCall(alice_destination.data(), alice.publicKey().data(), link, 0, Wire::send, &ab) == Links::OpenResult::Invalid);
    assert(caller.openCall(bob_destination.data(), alice.publicKey().data(), link, 0, Wire::send, &ab) == Links::OpenResult::Invalid);
    assert(caller.openCall(bob_destination.data(), bob.publicKey().data(), link, 0, Wire::send, &ab) == Links::OpenResult::Opening);
    auto request = ab.pop();
    // Without a registered call owner the receiver never allocates a call link.
    {
        Links disabled(bob);
        assert(disabled.receive(request.data(), request.size(), 1, Wire::send, &ba));
        assert(disabled.statistics().sessions == 0 && ba.packets.empty());
    }
    assert(callee.receive(request.data(), request.size(), 1, Wire::send, &ba));
    auto proof = ba.pop();
    auto forged = proof;
    forged.back() ^= 1;
    assert(caller.receive(forged.data(), forged.size(), 2, Wire::send, &ab));
    assert(a.admitted == 0 && ab.packets.empty());
    assert(caller.receive(proof.data(), proof.size(), 3, Wire::send, &ab));
    assert(a.admitted == 1 && a.peer.initiator && !std::memcmp(a.peer.identity, bob.identityHash().data(), 16));
    auto rtt = ab.pop();
    auto identify = ab.pop();
    auto incoming_hooks = b.hooks();
    // This isolated fixture records validation without admitting the caller.
    struct Validation
    {
        static void ready(void* context, const Links::CallPeer& peer)
        {
            assert(!peer.initiator);
            assert(std::all_of(std::begin(peer.identity), std::end(peer.identity), [](uint8_t byte)
                               { return byte == 0; }));
            ++static_cast<Events*>(context)->validated;
        }
    };
    incoming_hooks.validated = Validation::ready;
    callee.setCallHooks(incoming_hooks);
    assert(callee.receive(rtt.data(), rtt.size(), 4, Wire::send, &ba));
    assert(b.admitted == 0); // Ephemeral handshake alone cannot admit a caller.
    assert(b.validated == 1);
    assert(callee.sendCallSignal(link, chat::reticulum::lxst::kStatusAvailable, 4, Wire::send, &ba));
    auto early_signal = ba.pop();
    assert(caller.receive(early_signal.data(), early_signal.size(), 4, Wire::send, &ab));
    assert(a.received == 1);
    uint8_t signal[32];
    size_t size = sizeof(signal);
    assert(chat::reticulum::lxst::encodeSignalling(chat::reticulum::lxst::kStatusAvailable, signal, &size));
    assert(caller.sendCall(link, signal, size, 5, Wire::send, &ab));
    auto available = ab.pop();
    assert(callee.receive(available.data(), available.size(), 6, Wire::send, &ba));
    assert(b.received == 0 && ba.packets.empty());
    forged = identify;
    forged.back() ^= 1;
    assert(callee.receive(forged.data(), forged.size(), 7, Wire::send, &ba));
    assert(b.admitted == 0);
    assert(callee.receive(identify.data(), identify.size(), 8, Wire::send, &ba));
    assert(b.admitted == 1 && !b.peer.initiator && !std::memcmp(b.peer.destination, alice_destination.data(), 16));
    assert(callee.receive(identify.data(), identify.size(), 9, Wire::send, &ba));
    assert(b.admitted == 1);
    assert(callee.receive(available.data(), available.size(), 10, Wire::send, &ba));
    assert(b.received == 1 && b.payload == std::vector<uint8_t>(signal, signal + size));
    assert(callee.sendCall(link, signal, size, 11, Wire::send, &ba));
    auto response = ba.pop();
    assert(caller.receive(response.data(), response.size(), 12, Wire::send, &ab));
    assert(a.received == 2);
    assert(!caller.sendCall(link, signal, 501, 13, Wire::send, &ab));
    // Delivery and telephony links to the same identity remain independent.
    assert(caller.open(bob.destinationHash().data(), bob.publicKey().data(), 14, Wire::send, &ab) == Links::OpenResult::Opening);
    assert(caller.statistics().sessions == 2);
    ab.packets.clear();
    assert(caller.closeCall(link, 15, Wire::send, &ab));
    auto close = ab.pop();
    assert(callee.receive(close.data(), close.size(), 16, Wire::send, &ba));
    assert(a.closed == 1 && b.closed == 1);
    assert(!caller.sendCall(link, signal, size, 17, Wire::send, &ab) && !caller.closeCall(link, 17, Wire::send, &ab));
    assert(callee.receive(available.data(), available.size(), 18, Wire::send, &ba) && b.received == 1);
    caller.reset();
    callee.reset();
    // Admission denial sends an authenticated close and never delivers audio.
    b.accept = false;
    assert(caller.openCall(bob_destination.data(), bob.publicKey().data(), link, 100, Wire::send, &ab) == Links::OpenResult::Opening);
    request = ab.pop();
    assert(callee.receive(request.data(), request.size(), 101, Wire::send, &ba));
    proof = ba.pop();
    assert(caller.receive(proof.data(), proof.size(), 102, Wire::send, &ab));
    rtt = ab.pop();
    identify = ab.pop();
    assert(callee.receive(rtt.data(), rtt.size(), 103, Wire::send, &ba));
    assert(callee.receive(identify.data(), identify.size(), 104, Wire::send, &ba));
    assert(b.admitted == 2 && b.received == 1 && b.closed == 2);
    close = ba.pop();
    assert(caller.receive(close.data(), close.size(), 105, Wire::send, &ab));
    assert(a.closed == 2);
    caller.reset();
    callee.reset();
    b.accept = true;
    ab.fail = true;
    assert(caller.openCall(bob_destination.data(), bob.publicKey().data(), link, 200, Wire::send, &ab) == Links::OpenResult::TxFailed);
    assert(caller.statistics().sessions == 0);
    ab.fail = false;
    assert(caller.openCall(bob_destination.data(), bob.publicKey().data(), link, 300, Wire::send, &ab) == Links::OpenResult::Opening);
    caller.tick(15301);
    assert(caller.statistics().active == 0 && a.closed == 4);
    caller.reset();
    callee.reset();
    std::filesystem::remove_all(root);
}
