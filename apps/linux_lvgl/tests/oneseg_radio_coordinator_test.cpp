#include "platform/linux/packet_radio.h"
#include <array>
#include <cassert>
#include <chrono>
#include <future>

using namespace platform::linux_runtime;
struct State
{
    unsigned streams = 0, configurations = 0, transmissions = 0;
    bool online = false, supported = true;
    int recovery_error = 0;
    RadioSettings settings;
    std::promise<void>* entered = nullptr;
    std::shared_future<void> resume;
};
struct Radio final : IPacketRadio
{
    State& s;
    explicit Radio(State& state) : s(state) {}
    const char* name() const override { return "test-only"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override { return s.online = true; }
    void release() override { s.online = false; }
    bool configureLoRa(const RadioSettings& settings) override
    {
        ++s.configurations;
        s.settings = settings;
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        ++s.transmissions;
        return true;
    }
    bool pollReceive(RadioPacket*) override { return false; }
    float readRssi() override { return -100; }
    bool isOnline() const override { return s.online; }
    const char* lastError() const override { return "injected TV failure"; }
    RadioSettings appliedLoRaConfig() const override { return s.settings; }
    RadioStatistics stats() const override
    {
        RadioStatistics value;
        value.online = s.online;
        return value;
    }
    bool supportsOneSegIq() const override { return s.supported; }
    bool streamOneSegIq(const OneSegIqRequest& request, OneSegIqResult& result) override
    {
        ++s.streams;
        if (s.entered)
        {
            s.entered->set_value();
            s.resume.wait();
        }
        const uint8_t data[] = {1, 2, 3, 4};
        assert(request.accept(request.context, data, sizeof(data), 0));
        result = {};
        result.validated_bytes = 4;
        result.nominal_rate_hz = 576000;
        result.cancelled = true;
        result.recovery_error = s.recovery_error;
        return !s.recovery_error;
    }
};
int main()
{
    State state;
    PacketRadioCoordinator coordinator(std::make_unique<Radio>(state));
    auto& mesh = coordinator.mesh();
    auto& tool = coordinator.foreground();
    uint8_t staging[1280]{};
    OneSegIqRequest request;
    request.staging = staging;
    request.staging_bytes = sizeof(staging);
    request.accept = [](void*, const uint8_t*, size_t bytes, uint64_t first)
    { return bytes == 4 && first == 0; };
    request.cancelled = [](void*)
    { return true; };
    OneSegIqResult result;
    assert(tool.supportsOneSegIq() && !mesh.supportsOneSegIq());
    assert(!tool.streamOneSegIq(request, result) && !state.streams);
    assert(mesh.acquire());
    RadioSettings settings;
    settings.freq_mhz = 915;
    assert(mesh.configureLoRa(settings));
    assert(!mesh.streamOneSegIq(request, result) && !state.streams);
    assert(tool.acquire());
    auto invalid = request;
    invalid.cancelled = nullptr;
    assert(!tool.streamOneSegIq(invalid, result) && result.capture_error == -1 && !state.streams);
    invalid = request;
    invalid.channel = 63;
    assert(!tool.streamOneSegIq(invalid, result) && !state.streams);
    invalid = request;
    invalid.staging_bytes = 1279;
    assert(!tool.streamOneSegIq(invalid, result) && !state.streams);
    std::promise<void> entered, release;
    state.entered = &entered;
    state.resume = release.get_future().share();
    auto started = entered.get_future();
    auto worker = std::async(std::launch::async, [&]
                             { return tool.streamOneSegIq(request, result); });
    assert(started.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    auto status = std::async(std::launch::async, [&]
                             { (void)tool.isOnline(); (void)tool.stats(); return !tool.acquire() && !coordinator.setEnabled(false); });
    assert(status.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready && status.get());
    const uint8_t byte = 0;
    auto competing = std::async(std::launch::async, [&]
                                { return mesh.transmit(&byte, 1); });
    assert(competing.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    release.set_value();
    assert(worker.get() && result.validated_bytes == 4);
    assert(!competing.get() && !state.transmissions);
    state.entered = nullptr;
    assert(tool.releaseWithStatus());
    assert(state.configurations == 2 && mesh.transmit(&byte, 1));
    assert(tool.acquire());
    state.recovery_error = -5;
    assert(!tool.streamOneSegIq(request, result) && !tool.isOnline());
    const auto calls = state.streams;
    state.recovery_error = 0;
    assert(!tool.streamOneSegIq(request, result) && state.streams == calls);
    assert(tool.configureLoRa(settings) && tool.startReceive());
    assert(tool.streamOneSegIq(request, result));
    assert(tool.releaseWithStatus());
    mesh.release();
}
