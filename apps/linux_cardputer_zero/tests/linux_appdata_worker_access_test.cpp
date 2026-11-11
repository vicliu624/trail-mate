#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/linux_mesh_worker.h"
#include "platform/linux/packet_radio.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace rt = platform::linux_runtime;

class Radio final : public rt::IPacketRadio
{
  public:
    const char* name() const override { return "channel-access-fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        online = true;
        return true;
    }
    void release() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        online = false;
    }
    bool configureLoRa(const rt::RadioSettings& value) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        settings = value;
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++statistics.tx_packets;
        if (size >= sizeof(chat::meshtastic::PacketHeaderWire))
        {
            chat::meshtastic::PacketHeaderWire header;
            std::memcpy(&header, bytes, sizeof(header));
            ids.push_back(header.id);
        }
        changed.notify_all();
        return true;
    }
    bool pollReceive(rt::RadioPacket*) override { return false; }
    float readRssi() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++rssi_reads;
        return rssi;
    }
    bool isOnline() const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return online;
    }
    const char* lastError() const override { return "fixture"; }
    rt::RadioSettings appliedLoRaConfig() const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return settings;
    }
    rt::RadioStatistics stats() const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto result = statistics;
        result.online = online;
        result.lora_config = settings;
        return result;
    }
    bool sent(uint32_t id) const
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool online = false;
    float rssi = -120;
    unsigned rssi_reads = 0;
    rt::RadioSettings settings;
    rt::RadioStatistics statistics;
    std::vector<uint32_t> ids;
};

int main()
{
    Radio radio;
    trailmate::linux_app::LinuxMeshWorker worker(radio, 0x10203040);
    assert(worker.initialize());
    chat::MeshConfig config;
    config.tx_enabled = true;
    worker.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    const auto ready = std::chrono::steady_clock::now() + 3s;
    while (!worker.isReady() && std::chrono::steady_clock::now() < ready) std::this_thread::sleep_for(10ms);
    assert(worker.isReady());
    const uint8_t body[] = {0x42, 0x12};
    constexpr uint32_t idle_packet = 0x55667780;
    // Wake an idle worker after it has entered its condition-variable wait.
    // A newly queued command must not bypass beginAttempt on that wake-up.
    std::this_thread::sleep_for(80ms);
    assert(worker.sendAppData(chat::ChannelId::PRIMARY, 303, body, sizeof(body), 0, false, idle_packet, false));
    std::this_thread::sleep_for(100ms);
    {
        std::unique_lock<std::mutex> lock(radio.mutex);
        assert(!radio.sent(idle_packet));
        assert(radio.changed.wait_for(lock, 3s, [&]
                                      { return radio.sent(idle_packet); }));
    }
    {
        std::lock_guard<std::mutex> lock(radio.mutex);
        ++radio.statistics.rx_packets;
        radio.rssi = -40;
    }
    std::this_thread::sleep_for(80ms);
    constexpr uint32_t packet = 0x55667788;
    const auto submitted = std::chrono::steady_clock::now();
    assert(worker.sendAppData(chat::ChannelId::PRIMARY, 303, body, sizeof(body), 0, false, packet, false));
    assert(std::chrono::steady_clock::now() - submitted < 100ms);
    std::this_thread::sleep_for(200ms);
    {
        std::lock_guard<std::mutex> lock(radio.mutex);
        assert(!radio.sent(packet));
        radio.rssi = -120;
    }
    {
        std::unique_lock<std::mutex> lock(radio.mutex);
        // The adapter's real startup NodeInfo announcement at five seconds may
        // occupy the first contention opportunity. Let its relay guard finish.
        assert(radio.changed.wait_for(lock, 12s, [&]
                                      { return radio.sent(packet); }));
        // A delayed queue must sleep rather than spinning and starving the UI.
        assert(radio.rssi_reads < 800);
    }
    worker.shutdown();
}
