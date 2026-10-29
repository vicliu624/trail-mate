#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/linux_meshcore_repeater.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
namespace wire = trailmate::linux_app::meshcore_wire;
namespace mc = chat::meshcore;
using Packet = platform::linux_runtime::RadioPacket;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
Packet make(mc::PayloadProfile profile, uint8_t route, uint8_t type,
            const std::vector<uint8_t>& path, const std::vector<uint8_t>& payload)
{
    Packet packet;
    require(mc::buildFrameNoTransport(profile, route, type, path.data(), path.size(), payload.data(), payload.size(),
                                      packet.data.data(), packet.data.size(), &packet.size),
            "fixture frame build failed");
    packet.snr_db = 4;
    return packet;
}
mc::ParsedPacket parse(const wire::Frame& frame)
{
    mc::ParsedPacket result;
    require(mc::parsePacket(frame.bytes.data(), frame.size, &result), "forwarded frame malformed");
    return result;
}
} // namespace

int main()
{
    const auto v1 = mc::PayloadProfile::V1, v2 = mc::PayloadProfile::V2;
    const std::array<uint8_t, 32> own{0x31, 0x72, 1, 2, 3};
    chat::MeshConfig config;
    config.tx_enabled = true;
    config.meshcore_client_repeat = true;
    config.meshcore_forward_profile = chat::MeshCoreForwardProfile::Any;
    config.meshcore_rx_delay_base = 0;
    config.meshcore_airtime_factor = 0;
    wire::Repeater repeater;
    wire::Frame output;
    for (const auto profile : {v1, v2})
    {
        repeater.reset();
        auto input = make(profile, 1, 6, {}, {0x99, 1, 2, 3});
        require(repeater.receive(config, own, input, 100, 0) == wire::Repeater::Disposition::Local, "flood frame was consumed");
        require(repeater.take(100, output), "flood not queued");
        const auto parsed = parse(output);
        const auto hash = mc::payloadHashBytes(profile);
        require(parsed.path_len == hash && !std::memcmp(parsed.path, own.data(), hash), "own path hash was not appended");
        require(parsed.payload_len == 4 && parsed.payload[0] == 0x99 && parsed.payload[3] == 3, "relay changed payload");
        require(output.bytes[0] == (profile == v1 ? 0x19 : 0x59) && output.bytes[1] == (profile == v1 ? 1 : 0x41), "unexpected V1/V2 wire header");
        repeater.receive(config, own, input, 101, 0);
        require(!repeater.queued(), "duplicate flood repeated");
        repeater.reset();
        repeater.sent(input.data.data(), input.size, 100);
        repeater.receive(config, own, input, 101, 0);
        require(!repeater.queued(), "own transmitted frame was echoed");
    }
    repeater.reset();
    config.meshcore_forward_profile = chat::MeshCoreForwardProfile::MultibyteOnly;
    repeater.receive(config, own, make(v1, 1, 6, {}, {1}), 0, 0);
    require(!repeater.queued(), "V1 bypassed multibyte-only policy");
    repeater.receive(config, own, make(v2, 1, 6, {own[0], own[1]}, {2}), 0, 0);
    require(!repeater.queued(), "self-in-path loop admitted");
    config.meshcore_flood_max = 1;
    repeater.receive(config, own, make(v2, 1, 6, {0x12, 0x34}, {3}), 0, 0);
    require(!repeater.queued(), "flood hop limit ignored");
    config.meshcore_flood_max = 16;
    config.meshcore_client_repeat = false;
    repeater.receive(config, own, make(v2, 1, 6, {}, {4}), 0, 0);
    require(!repeater.queued(), "disabled repeater forwarded");
    config.meshcore_client_repeat = true;
    auto direct = make(v2, 2, 2, {own[0], own[1], 0x12, 0x34}, {5, 6, 7});
    require(repeater.receive(config, own, direct, 10, 0) == wire::Repeater::Disposition::Transit, "transit delivered locally");
    require(repeater.take(10, output), "direct hop not queued");
    auto parsed = parse(output);
    require(parsed.path_len == 2 && parsed.path[0] == 0x12 && parsed.path[1] == 0x34 && parsed.payload[0] == 5, "direct path was not shortened correctly");
    repeater.reset();
    repeater.receive(config, own, make(v2, 2, 2, {0x55, 0x66}, {5}), 10, 0);
    require(!repeater.queued(), "unaddressed direct hop forwarded");
    config.meshcore_multi_acks = true;
    const uint32_t before_wrap = std::numeric_limits<uint32_t>::max() - 100;
    repeater.receive(config, own, make(v2, 2, 3, {own[0], own[1], 0x12, 0x34}, {1, 2, 3, 4}), before_wrap, 0);
    require(repeater.queued() == 2 && repeater.take(before_wrap, output), "multi-ACK burst missing");
    parsed = parse(output);
    require(parsed.payload_type == 10 && parsed.payload_len == 5 && parsed.payload[0] == 3 && parsed.payload[4] == 4, "multi-ACK token changed");
    require(!repeater.take(before_wrap + 299, output), "ACK spacing ignored");
    require(repeater.take(before_wrap + 300, output) && parse(output).payload_type == 3, "ACK did not survive clock wrap");
    repeater.reset();
    uint8_t trace_payload[32]{};
    size_t trace_size = 0;
    const uint8_t trace_path[]{own[0], 0x44};
    require(mc::buildTracePayload(1, 2, 0, trace_path, 2, trace_payload, sizeof(trace_payload), &trace_size), "trace fixture failed");
    repeater.receive(config, own, make(v1, 2, 9, {}, {trace_payload, trace_payload + trace_size}), 0, 0);
    require(repeater.take(0, output), "trace relay was incorrectly filtered by V1 policy");
    parsed = parse(output);
    require(parsed.path_len == 1 && parsed.path[0] == 16 && parsed.payload_len == trace_size && !std::memcmp(parsed.payload, trace_payload, trace_size), "trace SNR/payload changed");
    repeater.reset();
    for (unsigned index = 0; index < 9; ++index)
        repeater.receive(config, own, make(v2, 1, 6, {}, {static_cast<uint8_t>(index)}), 0, 0);
    require(repeater.queued() == 8, "relay queue is not bounded");
    require(!repeater.take(5000, output) && !repeater.queued(), "expired forwarding work was transmitted");
    repeater.reset();
    config.meshcore_airtime_factor = std::numeric_limits<float>::quiet_NaN();
    repeater.receive(config, own, make(v2, 1, 6, {}, {99}), 0, 0);
    require(!repeater.queued(), "invalid timing configuration queued a relay");
}
