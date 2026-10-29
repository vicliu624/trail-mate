#include "platform/linux/packet_radio.h"
#include <cstring>
#include <memory>

namespace
{
using namespace platform::linux_runtime;
struct State
{
    unsigned acquired = 0, released = 0, configured = 0, transmitted = 0, polled = 0;
    bool online = false, fail_config = false, fail_receive = false, sticky_online = false;
    RadioSettings settings;
};
class FakeRadio final : public IPacketRadio
{
  public:
    explicit FakeRadio(std::shared_ptr<State> state) : state_(std::move(state)) {}
    const char* name() const override { return "fake"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        ++state_->acquired;
        state_->online = true;
        return true;
    }
    void release() override
    {
        ++state_->released;
        if (!state_->sticky_online) state_->online = false;
    }
    bool configureLoRa(const RadioSettings& settings) override
    {
        ++state_->configured;
        if (state_->fail_config) return false;
        state_->settings = settings;
        return true;
    }
    bool startReceive() override { return !state_->fail_receive; }
    bool supportsFsk() const override { return true; }
    bool configureFsk(const FskSettings&) override { return !state_->fail_config; }
    bool transmit(const uint8_t*, std::size_t) override
    {
        ++state_->transmitted;
        return true;
    }
    bool pollReceive(RadioPacket* packet) override
    {
        ++state_->polled;
        packet->size = 1;
        return true;
    }
    float readRssi() override { return -118; }
    bool isOnline() const override { return state_->online; }
    const char* lastError() const override { return state_->fail_config ? "injected configuration failure" : state_->fail_receive ? "injected receive failure"
                                                                                                                                  : ""; }
    RadioSettings appliedLoRaConfig() const override { return state_->settings; }
    RadioStatistics stats() const override
    {
        RadioStatistics stats;
        stats.online = state_->online;
        stats.lora_config = state_->settings;
        return stats;
    }

  private:
    std::shared_ptr<State> state_;
};
} // namespace

int main()
{
    using namespace platform::linux_runtime;
    const auto state = std::make_shared<State>();
    {
        PacketRadioCoordinator radio(std::make_unique<FakeRadio>(state));
        auto& mesh = radio.mesh();
        auto& tool = radio.foreground();
        if (!mesh.acquire()) return 1;
        RadioSettings settings;
        settings.freq_mhz = 433.175f;
        if (!mesh.configureLoRa(settings) || !tool.acquire() || tool.acquire()) return 2;
        settings.freq_mhz = 915.0f;
        if (!tool.configureLoRa(settings) || state->acquired != 1) return 3;
        RadioPacket packet;
        const uint8_t data = 42;
        if (mesh.pollReceive(&packet) || mesh.transmit(&data, 1) || state->polled || state->transmitted) return 4;
        settings.freq_mhz = 868.0f;
        if (!mesh.configureLoRa(settings) || state->settings.freq_mhz != 915.0f) return 5;
        if (mesh.appliedLoRaConfig().freq_mhz != 868.0f || !mesh.isOnline()) return 6;
        if (!tool.transmit(&data, 1) || state->transmitted != 1) return 7;
        tool.release();
        if (state->settings.freq_mhz != 868.0f || !mesh.pollReceive(&packet) || state->polled != 1) return 8;
        if (std::strlen(mesh.lastError()) != 0) return 16;
        if (tool.transmit(&data, 1)) return 9;
        mesh.release();
        if (state->released != 1 || mesh.isOnline()) return 10;
        if (!mesh.acquire() || !mesh.configureLoRa(settings) || !tool.acquire()) return 11;
        state->fail_config = true;
        tool.release();
        if (mesh.isOnline() || mesh.transmit(&data, 1)) return 12;
        // A remaining mesh lease must be released by coordinator destruction.
    }
    if (state->acquired != 2 || state->released != 2) return 13;
    const auto power = std::make_shared<State>();
    power->sticky_online = true; // Legacy standby can keep its driver-online flag.
    {
        PacketRadioCoordinator radio(std::make_unique<FakeRadio>(power));
        auto& mesh = radio.mesh();
        auto& tool = radio.foreground();
        RadioSettings settings;
        const uint8_t byte = 1;
        if (!mesh.acquire() || !mesh.configureLoRa(settings) || !radio.setEnabled(false)) return 17;
        if (mesh.isOnline() || mesh.startReceive() || mesh.transmit(&byte, 1) || mesh.configureLoRa(settings)) return 18;
        if (power->released != 1 || !radio.setEnabled(true) || mesh.isOnline()) return 19;
        mesh.release();
        if (power->released != 1 || !mesh.acquire() || !mesh.configureLoRa(settings)) return 20;
        if (!tool.acquire() || radio.setEnabled(false)) return 21;
        tool.release();
        mesh.release();
    }
    if (power->acquired != 2 || power->released != 2) return 22;
    const auto failure = std::make_shared<State>();
    {
        PacketRadioCoordinator radio(std::make_unique<FakeRadio>(failure));
        auto& mesh = radio.mesh();
        auto& tool = radio.foreground();
        RadioSettings settings;
        const uint8_t byte = 1;
        RadioPacket packet;
        if (!mesh.acquire() || !mesh.configureLoRa(settings)) return 23;
        failure->fail_config = true; // Backend retains online/old settings on rejection.
        settings.freq_mhz = 915;
        if (mesh.configureLoRa(settings) || mesh.isOnline() || mesh.stats().online) return 24;
        if (mesh.transmit(&byte, 1) || mesh.pollReceive(&packet) || mesh.startReceive()) return 25;
        if (failure->transmitted || failure->polled || std::strstr(mesh.lastError(), "injected") == nullptr) return 26;
        failure->fail_config = false;
        if (!mesh.configureLoRa(settings) || !mesh.startReceive() || !mesh.isOnline()) return 27;
        if (!tool.acquire()) return 28;
        failure->fail_config = true;
        FskSettings fsk;
        fsk.freq_mhz = 915;
        if (tool.configureFsk(fsk) || tool.isOnline() || tool.transmit(&byte, 1)) return 29;
        failure->fail_config = false;
        if (!tool.configureFsk(fsk) || !tool.startReceive() || !tool.isOnline()) return 30;
        tool.release();
        if (!mesh.isOnline() || !mesh.transmit(&byte, 1)) return 31;
        failure->fail_receive = true;
        if (mesh.startReceive() || mesh.isOnline() || mesh.transmit(&byte, 1)) return 32;
        failure->fail_receive = false;
        if (!mesh.configureLoRa(settings) || !mesh.startReceive()) return 33;
        if (!tool.acquire()) return 34;
        failure->fail_config = true;
        tool.release();
        failure->fail_config = false;
        if (mesh.isOnline() || !mesh.configureLoRa(settings) || !mesh.startReceive() || !mesh.isOnline()) return 35;
        mesh.release();
    }
    if (failure->acquired != 1 || failure->released != 1) return 36;
    PacketRadioHardware disabled;
    disabled.driver = "none";
    if (!configure_packet_radio(disabled) || mesh_packet_radio().hardwareCandidatePresent()) return 14;
    if (configure_packet_radio(disabled)) return 15;
    return 0;
}
