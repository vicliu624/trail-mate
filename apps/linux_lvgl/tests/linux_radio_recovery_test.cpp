#include "chat/linux_raw_lora_mesh_adapter.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace port = platform::linux_runtime;
uint32_t fake_time = 0;
uint32_t now() { return fake_time; }
struct Radio final : port::IPacketRadio
{
    mutable unsigned probes = 0;
    unsigned acquired = 0, released = 0, configured = 0, receive_started = 0, transmitted = 0;
    bool present = true, online = false, fail_acquire = false, fail_config = false, fail_receive = false;
    port::RadioSettings settings;
    const char* name() const override { return "recovery test"; }
    bool hardwareCandidatePresent() const override
    {
        ++probes;
        return present;
    }
    bool acquire() override
    {
        ++acquired;
        if (fail_acquire)
        {
            fake_time += 300; // A slow operation completes after the initial cooldown duration.
            return false;
        }
        online = true;
        return true;
    }
    void release() override { ++released; } // Deliberately keep an optimistic legacy online flag.
    bool configureLoRa(const port::RadioSettings& value) override
    {
        ++configured;
        if (fail_config) return false;
        settings = value;
        return true;
    }
    bool startReceive() override
    {
        ++receive_started;
        return !fail_receive;
    }
    bool transmit(const uint8_t*, std::size_t) override
    {
        ++transmitted;
        return true;
    }
    bool pollReceive(port::RadioPacket*) override { return false; }
    float readRssi() override { return -100; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "injected backend failure"; }
    port::RadioSettings appliedLoRaConfig() const override { return settings; }
    port::RadioStatistics stats() const override { return {}; }
};
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-radio-recovery-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    Radio radio;
    {
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(radio, 123, now);
        assert(adapter.begin() && adapter.isReady() && radio.receive_started == 1);
        chat::MeshConfig settings;
        radio.fail_config = true;
        adapter.applyConfig(settings);
        assert(!adapter.isReady() && radio.released == 1);
        const auto attempts = radio.acquired;
        for (unsigned i = 0; i < 20; ++i) adapter.tick();
        chat::MessageId id;
        assert(!adapter.sendText(chat::ChannelId::PRIMARY, "blocked", &id));
        assert(radio.acquired == attempts && radio.transmitted == 0);
        radio.fail_config = false;
        adapter.applyConfig(settings); // Explicit edit bypasses the old cooldown.
        assert(adapter.isReady() && radio.receive_started == 2);
        radio.fail_receive = true;
        adapter.applyConfig(settings);
        assert(!adapter.isReady());
        const auto rx_fail_attempts = radio.acquired;
        for (unsigned i = 0; i < 20; ++i) adapter.tick();
        assert(radio.acquired == rx_fail_attempts && !adapter.isReady());
        radio.fail_receive = false;
        adapter.applyConfig(settings);
        assert(adapter.isReady());
        radio.online = false;
        radio.fail_acquire = true;
        adapter.tick();
        const auto slow_attempts = radio.acquired;
        for (unsigned i = 0; i < 20; ++i) adapter.tick();
        assert(radio.acquired == slow_attempts); // Slow I/O did not consume its own cooldown.
        radio.fail_acquire = false;
        adapter.applyConfig(settings);
        assert(adapter.isReady());
        radio.online = false;
        radio.present = false;
        adapter.tick();
        const auto probes = radio.probes;
        for (unsigned i = 0; i < 20; ++i)
        {
            adapter.tick();
            (void)adapter.isReady();
            (void)adapter.statusText();
        }
        assert(radio.probes == probes);
        radio.present = true;
        adapter.applyConfig(settings);
        assert(adapter.isReady());
    }
    // Failed acquisition holds no lease; every successful acquisition is released.
    assert(radio.released + 1 == radio.acquired);
    std::filesystem::remove_all(root);
}
