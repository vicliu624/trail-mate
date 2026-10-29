#include "chat/linux_raw_lora_mesh_adapter.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <unistd.h>

namespace radio = platform::linux_runtime;
namespace vmp = chat::voice::vmp;
struct Radio final : radio::IPacketRadio
{
    bool online = false;
    unsigned transmissions = 0;
    radio::RadioSettings settings;
    std::deque<radio::RadioPacket> incoming;
    const char* name() const override { return "ingress fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        online = true;
        return true;
    }
    void release() override { online = false; }
    bool configureLoRa(const radio::RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        ++transmissions;
        return true;
    }
    bool pollReceive(radio::RadioPacket* out) override
    {
        if (incoming.empty()) return false;
        *out = incoming.front();
        incoming.pop_front();
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "fixture"; }
    radio::RadioSettings appliedLoRaConfig() const override { return settings; }
    radio::RadioStatistics stats() const override { return {}; }
};
struct Sink final : vmp::IControlEnvelopeSink
{
    unsigned calls = 0;
    bool accepts = true;
    std::array<uint8_t, vmp::kControlFrameSize> bytes{};
    vmp::ControlRxMetadata metadata;
    bool enqueueControl(const uint8_t* data, size_t size, const vmp::ControlRxMetadata& signal) override
    {
        assert(size == bytes.size());
        ++calls;
        metadata = signal;
        std::memcpy(bytes.data(), data, size);
        return accepts;
    }
};
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-vmp-ingress-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    for (const auto protocol : {chat::MeshProtocol::Meshtastic, chat::MeshProtocol::MeshCore})
    {
        Radio physical;
        Sink sink;
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(physical, 123);
        chat::MeshConfig config;
        adapter.applyProtocolConfig(protocol, config);
        assert(adapter.begin());
        adapter.setVoiceControlSink(&sink);
        radio::RadioPacket packet;
        packet.size = vmp::kControlFrameSize;
        packet.data[0] = 'V';
        packet.data[1] = 'M';
        packet.data[2] = vmp::kVersion;
        packet.data[3] = 0xff; // Malformed control still cannot enter mesh parsing.
        packet.rssi_dbm = -72;
        packet.snr_db = 6;
        physical.incoming.push_back(packet);
        adapter.tick();
        assert(sink.calls == 1 && sink.bytes[3] == 0xff && sink.metadata.rssi == -72 && sink.metadata.snr == 6);
        std::array<uint8_t, 255> raw;
        size_t length = raw.size();
        assert(!adapter.pollIncomingRawPacket(raw.data(), length, raw.size()));
        chat::MeshIncomingText text;
        chat::MeshIncomingData data;
        assert(!adapter.pollIncomingText(&text) && !adapter.pollIncomingData(&data) && !physical.transmissions);
        sink.accepts = false;
        physical.incoming.push_back(packet);
        adapter.tick();
        assert(sink.calls == 2);
        length = raw.size();
        assert(!adapter.pollIncomingRawPacket(raw.data(), length, raw.size()));
        adapter.setVoiceControlSink(nullptr);
        physical.incoming.push_back(packet);
        adapter.tick();
        length = raw.size();
        assert(sink.calls == 2 && !adapter.pollIncomingRawPacket(raw.data(), length, raw.size()));
        // An ordinary packet remains available to the existing raw monitor.
        packet.size = 3;
        packet.data[0] = 'x';
        packet.data[1] = 0;
        packet.data[2] = 0;
        physical.incoming.push_back(packet);
        adapter.tick();
        length = raw.size();
        assert(adapter.pollIncomingRawPacket(raw.data(), length, raw.size()) && length == 3 && raw[0] == 'x');
    }
    std::filesystem::remove_all(root);
}
