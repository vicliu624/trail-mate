#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/linux_meshcore_discovery.h"
#include <cassert>
#include <cstring>
#include <limits>

namespace wire = trailmate::linux_app::meshcore_wire;
namespace mc = chat::meshcore;
using Packet = platform::linux_runtime::RadioPacket;
Packet packet(const wire::Frame& frame)
{
    Packet result;
    result.size = frame.size;
    std::memcpy(result.data.data(), frame.bytes.data(), frame.size);
    result.snr_db = 3.5f;
    return result;
}
int main()
{
    chat::MeshConfig config;
    wire::Discovery scanner, server;
    std::array<uint8_t, 32> scanner_key{}, server_key{};
    scanner_key.fill(0x11);
    server_key.fill(0x22);
    wire::Frame request, response;
    assert(!scanner.makeRequest(config, 0, request));
    assert(scanner.makeRequest(config, 0x01020304, request));
    assert(mc::toHex(request.bytes.data(), request.size) == "6E4080FF04030201");
    scanner.arm(0x01020304, 100);
    assert(scanner.listening(5099) && !scanner.listening(5100));
    wire::DiscoveredPeer peer;
    assert(!server.receive(config, server_key, packet(request), 100, 1700000000, 0, &peer));
    assert(!server.takeResponse(100, response));
    assert(server.takeResponse(2100, response));
    assert(!server.takeResponse(2100, request));
    auto response_packet = packet(response);
    assert(scanner.receive(config, scanner_key, response_packet, 2101, 1700000000, 0, &peer));
    assert(peer.node == 0x22222222 && peer.snr_qdb == 14);
    assert(!scanner.receive(config, scanner_key, response_packet, 2102, 1700000000, 0, &peer));
    scanner.arm(99, 100);
    assert(!scanner.receive(config, scanner_key, response_packet, 2101, 1700000000, 0, &peer));
    scanner.arm(0x01020304, 100);
    assert(!scanner.receive(config, scanner_key, response_packet, 5100, 1700000000, 0, &peer));
    scanner.arm(0x01020304, 0xFFFFFFF0U);
    assert(scanner.receive(config, scanner_key, response_packet, 0x20, 1700000000, 0, &peer));
    scanner.reset();
    assert(!scanner.listening(0x20));
    scanner.arm(77, 100);
    assert(!scanner.takeResponse(5100, response));
    assert(!scanner.listening(101)); // An expired scan cannot revive on a later clock wrap.
    // Queue saturation/drop policy and repeated-request suppression.
    server.reset();
    for (unsigned index = 0; index < 12; ++index)
    {
        assert(scanner.makeRequest(config, index + 1, request));
        assert(!server.receive(config, server_key, packet(request), 100, 1700000000, index, &peer));
        assert(!server.receive(config, server_key, packet(request), 101, 1700000000, index, &peer));
    }
    unsigned count = 0;
    while (server.takeResponse(2101, response)) ++count;
    assert(count == 8);
    assert(scanner.makeRequest(config, 100, request));
    auto disabled = config;
    disabled.tx_enabled = false;
    server.receive(disabled, server_key, packet(request), 100, 1700000000, 0, &peer);
    assert(!server.takeResponse(2100, response));
    server.reset();
    server.receive(config, server_key, packet(request), 100, 1700000000, 0, &peer);
    assert(!server.takeResponse(5100, response)); // Expired work cannot transmit later.
    // Filter, since and prefix-only request handling use the shared wire helpers.
    mc::MeshCoreDiscoverRequestBuildInfo info;
    info.tag = 101;
    info.prefix_only = true;
    info.type_filter = 1 << mc::kMeshCoreAdvertTypeChat;
    uint8_t payload[10]{};
    std::size_t size = 0;
    assert(mc::buildDiscoverRequestControlPayload(info, payload, sizeof(payload), &size));
    request = {};
    assert(mc::buildFrameNoTransport(mc::PayloadProfile::V1, 2, mc::kMeshCorePayloadTypeControl,
                                     nullptr, 0, payload, size, request.bytes.data(), request.bytes.size(), &request.size));
    server.reset();
    server.receive(config, server_key, packet(request), 100, 1700000000, 0, &peer);
    assert(server.takeResponse(2100, response));
    mc::ParsedPacket parsed;
    mc::DecodedDiscoverResponse prefix;
    assert(mc::parsePacket(response.bytes.data(), response.size, &parsed));
    assert(mc::decodeDiscoverResponse(parsed.payload, parsed.payload_len, &prefix) && prefix.pubkey_len == 8);
    info.since = 1800000000;
    info.tag = 102;
    assert(mc::buildDiscoverRequestControlPayload(info, payload, sizeof(payload), &size));
    assert(mc::buildFrameNoTransport(mc::PayloadProfile::V2, 2, mc::kMeshCorePayloadTypeControl,
                                     nullptr, 0, payload, size, request.bytes.data(), request.bytes.size(), &request.size));
    server.receive(config, server_key, packet(request), 100, 1700000000, 0, &peer);
    assert(!server.takeResponse(2100, response));
    // No unsolicited, oversized, relayed or self response becomes a peer.
    response_packet.size = 256;
    assert(!scanner.receive(config, scanner_key, response_packet, 101, 1700000000, 0, &peer));
}
