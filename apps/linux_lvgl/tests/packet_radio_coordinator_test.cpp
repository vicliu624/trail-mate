#include "platform/linux/packet_radio.h"
#include <chrono>
#include <cstring>
#include <future>
#include <memory>

namespace
{
using namespace platform::linux_runtime;
struct State
{
    unsigned acquired = 0, released = 0, configured = 0, transmitted = 0, polled = 0;
    bool online = false, fail_config = false, fail_receive = false, sticky_online = false;
    RadioSettings settings;
    bool iq_supported = true, iq_ok = true, iq_offline = false;
    unsigned iq_captured = 0;
    size_t iq_bytes = 1024;
    int iq_restore_error = 0;
    std::promise<void>* iq_entered = nullptr;
    std::shared_future<void> iq_continue;
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
    bool supportsIqCapture() const override { return state_->iq_supported; }
    bool captureIq(uint32_t* words, uint32_t*, size_t, RadioIqCaptureResult& result) override
    {
        ++state_->iq_captured;
        if (state_->iq_entered)
        {
            state_->iq_entered->set_value();
            state_->iq_continue.wait();
        }
        result = {};
        result.bytes = state_->iq_bytes;
        result.nominal_rate_hz = 125000;
        result.restore_error = state_->iq_restore_error;
        if (!state_->iq_ok) result.capture_error = static_cast<int>(IqCaptureError::Data);
        if (state_->iq_offline) state_->online = false;
        words[0] = 0x40004000;
        return state_->iq_ok;
    }
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

int check_iq_lease()
{
    using namespace platform::linux_runtime;
    const auto state = std::make_shared<State>();
    PacketRadioCoordinator coordinator(std::make_unique<FakeRadio>(state));
    auto& mesh = coordinator.mesh();
    auto& tool = coordinator.foreground();
    uint32_t storage[512]{};
    auto* words = storage;
    auto* scratch = storage + 256;
    RadioIqCaptureResult result;
    if (mesh.supportsIqCapture() || !tool.supportsIqCapture()) return 100;
    if (tool.captureIq(words, scratch, 256, result) || state->iq_captured) return 101;
    RadioSettings settings;
    if (!mesh.acquire() || !mesh.configureLoRa(settings) || !tool.acquire()) return 102;
    if (mesh.captureIq(words, scratch, 256, result) || state->iq_captured) return 103;
    if (tool.captureIq(words, words + 1, 256, result) || result.capture_error != -1 || state->iq_captured) return 104;
    if (tool.captureIq(nullptr, scratch, 256, result) || result.capture_error != -1 || state->iq_captured) return 105;
    if (tool.captureIq(words, scratch, 255, result) || result.capture_error != -1 || state->iq_captured) return 106;
    if (!tool.captureIq(words, scratch, 256, result) || result.bytes != 1024 || words[0] != 0x40004000) return 107;
    RadioPacket packet;
    if (mesh.pollReceive(&packet) || state->polled) return 108;
    settings.freq_mhz = 868;
    if (!mesh.configureLoRa(settings) || state->settings.freq_mhz == 868) return 109;
    if (!tool.releaseWithStatus() || state->settings.freq_mhz != 868 || !mesh.pollReceive(&packet)) return 110;
    if (!coordinator.setEnabled(false)) return 111;
    if (tool.captureIq(words, scratch, 256, result) || result.bytes) return 112;
    mesh.release();
    if (!coordinator.setEnabled(true) || !mesh.acquire() || !mesh.configureLoRa(settings) || !tool.acquire()) return 113;
    state->iq_supported = false;
    if (tool.supportsIqCapture() || tool.captureIq(words, scratch, 256, result)) return 114;
    state->iq_supported = true;
    state->iq_ok = false; // Backend deliberately leaves a partial byte count.
    if (tool.captureIq(words, scratch, 256, result) || result.bytes || result.nominal_rate_hz || !tool.isOnline()) return 115;
    state->iq_ok = true;
    state->iq_bytes = 512;
    if (tool.captureIq(words, scratch, 256, result) || result.bytes || result.capture_error != -4) return 116;
    state->iq_bytes = 1024;
    state->iq_restore_error = -5; // A sticky backend-online flag cannot override failed recovery.
    if (tool.captureIq(words, scratch, 256, result) || result.bytes || tool.isOnline() || tool.stats().online) return 117;
    const uint8_t byte = 1;
    if (tool.transmit(&byte, 1)) return 118;
    state->iq_restore_error = 0;
    if (!tool.configureLoRa(settings) || !tool.startReceive() || !tool.captureIq(words, scratch, 256, result)) return 119;
    state->iq_offline = true;
    if (tool.captureIq(words, scratch, 256, result) || result.bytes || tool.isOnline()) return 120;
    mesh.release();
    tool.release();
    const auto unavailable = make_unavailable_packet_radio("no hardware");
    if (unavailable->supportsIqCapture() || unavailable->captureIq(words, scratch, 256, result) || result.bytes) return 121;
    return 0;
}

int check_iq_concurrency()
{
    using namespace platform::linux_runtime;
    const auto state = std::make_shared<State>();
    PacketRadioCoordinator coordinator(std::make_unique<FakeRadio>(state));
    auto& mesh = coordinator.mesh();
    auto& tool = coordinator.foreground();
    if (!mesh.acquire() || !mesh.configureLoRa(RadioSettings{}) || !tool.acquire() || !tool.isOnline() || !tool.stats().online) return 122;
    uint32_t storage[512]{};
    RadioIqCaptureResult result;
    std::promise<void> entered, proceed;
    state->iq_entered = &entered;
    state->iq_continue = proceed.get_future().share();
    auto captured = std::async(std::launch::async, [&]
                               { return tool.captureIq(storage, storage + 256, 256, result); });
    entered.get_future().wait();
    auto cached = std::async(std::launch::async, [&]
                             { return tool.isOnline() && tool.stats().online; });
    const bool nonblocking = cached.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    auto polled = std::async(std::launch::async, [&]
                             { RadioPacket packet; return mesh.pollReceive(&packet); });
    const uint8_t byte = 1;
    auto transmitted = std::async(std::launch::async, [&]
                                  { return tool.transmit(&byte, 1); });
    const bool deferred = transmitted.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    // Always release the producer before inspecting assertions, including on a
    // failed nonblocking-query check, so no future destructor can deadlock.
    proceed.set_value();
    const bool success = captured.get();
    const bool online = cached.get();
    const bool delivered = polled.get();
    const bool sent = transmitted.get();
    if (!nonblocking || !online || !success || delivered || state->polled || !deferred || !sent || state->transmitted != 1) return 123;
    state->iq_entered = nullptr;
    RadioPacket packet;
    if (!tool.releaseWithStatus() || !mesh.pollReceive(&packet)) return 124;
    mesh.release();
    return 0;
}

int main()
{
    if (const int result = check_iq_lease()) return result;
    if (const int result = check_iq_concurrency()) return result;
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
