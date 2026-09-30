#include "platform/esp/arduino_common/chat/infra/lxmf/geocaching_discovery_budget.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/geocaching_discovery_probe.h"
#include <cassert>
#include <vector>

int main()
{
    using namespace chat;
    using lxmf::runtime::GeocachingDiscoveryBudget;
    using lxmf::runtime::RuntimeBudget;
    RuntimeBudget budget;
    budget.phase = "screen";
    budget.drop_public_discovery = true;
    budget.allow_public_discovery = false;
    std::array<uint8_t, 16> destination{};
    std::vector<uint8_t> payload(reticulum::kCombinedPublicKeySize + reticulum::kNameHashSize + 10 + reticulum::kSignatureSize + 1);
    reticulum::computeNameHash("trailmate", "geocache.directory", payload.data() + reticulum::kCombinedPublicKeySize);
    reticulum::ParsedPacket packet{};
    packet.valid = true;
    packet.packet_type = reticulum::PacketType::Announce;
    packet.destination_type = reticulum::DestinationType::Single;
    packet.destination_hash = destination.data();
    packet.payload = payload.data();
    packet.payload_len = payload.size();
    // Signature bytes intentionally invalid: classification is not authentication.
    assert(GeocachingDiscoveryBudget::matches(packet, true, budget));
    assert(!GeocachingDiscoveryBudget::matches(packet, false, budget));
    for (auto phase : {"call", "nomad", "saver"})
    {
        budget.phase = phase;
        assert(!GeocachingDiscoveryBudget::matches(packet, true, budget));
    }
    budget.phase = "screen";
    payload[reticulum::kCombinedPublicKeySize] ^= 1;
    assert(!GeocachingDiscoveryBudget::matches(packet, true, budget));
    payload[reticulum::kCombinedPublicKeySize] ^= 1;
    for (size_t size = 0; size < payload.size(); ++size)
    {
        packet.payload_len = size;
        assert(!GeocachingDiscoveryBudget::matches(packet, true, budget));
    }
    packet.payload_len = payload.size();
    GeocachingDiscoveryBudget gate;
    for (unsigned i = 0; i < 4; ++i) assert(gate.consume(0));
    assert(!gate.consume(0));
    assert(!gate.consume(9999));
    assert(gate.consume(10000));
    GeocachingDiscoveryBudget wrap;
    for (unsigned i = 0; i < 4; ++i) assert(wrap.consume(UINT32_MAX - 5000));
    assert(!wrap.consume(0));
    assert(wrap.consume(5000));

    using lxmf::runtime::GeocachingDiscoveryProbe;
    GeocachingDiscoveryProbe probe;
    assert(!probe.take(0, false, true, budget));
    assert(!probe.take(0, true, false, budget));
    assert(probe.take(0, true, true, budget));
    assert(!probe.take(59999, true, true, budget));
    assert(probe.take(60000, true, true, budget));
    assert(probe.take(120000, true, true, budget));
    assert(!probe.take(179999, true, true, budget));
    assert(probe.take(720000, true, true, budget));
    for (auto phase : {"call", "nomad", "saver"})
    {
        budget.phase = phase;
        assert(!probe.take(780000, true, true, budget));
    }
    budget.phase = "screen";
    assert(probe.take(780000, true, true, budget));
    assert(!probe.take(780001, false, true, budget));
    assert(probe.take(780002, true, true, budget)); // reopen immediately discovers again
    probe.reset();
    assert(probe.take(UINT32_MAX - 30000, true, true, budget));
    assert(!probe.take(29998, true, true, budget));
    assert(probe.take(29999, true, true, budget));
}
