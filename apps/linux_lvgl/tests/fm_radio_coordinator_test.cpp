#include "platform/linux/packet_radio.h"
#include "platform/shared/radio/fm_iq_stream.h"
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
    const char* name() const override { return "TEST FIXTURE - NO RF"; }
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
    const char* lastError() const override { return "injected FM failure"; }
    RadioSettings appliedLoRaConfig() const override { return s.settings; }
    RadioStatistics stats() const override
    {
        RadioStatistics value;
        value.online = s.online;
        return value;
    }
    bool supportsFmIq() const override { return s.supported; }
    bool streamFmIq(const FmIqRequest& request, FmIqResult& result) override
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
        result.nominal_rate_hz = 250000;
        result.cancelled = true;
        result.recovery_error = s.recovery_error;
        if (request.allow_image_calibration_warning)
        {
            result.setup.chip_errors = 0x0080;
            result.setup.calibration_warning = true;
        }
        return !s.recovery_error;
    }
};
int main()
{
    State state;
    PacketRadioCoordinator coordinator(std::make_unique<Radio>(state));
    auto& mesh = coordinator.mesh();
    auto& tool = coordinator.foreground();
    std::array<uint8_t, 1280> staging{};
    FmIqRequest request;
    request.staging = staging.data();
    request.staging_bytes = staging.size();
    request.accept = [](void*, const uint8_t*, size_t n, uint64_t first)
    { return n == 4 && !first; };
    request.cancelled = [](void*)
    { return true; };
    FmIqResult result;
    assert(tool.supportsFmIq() && !mesh.supportsFmIq());
    assert(!tool.streamFmIq(request, result) && !state.streams);
    assert(mesh.acquire());
    RadioSettings settings;
    settings.freq_mhz = 915;
    assert(mesh.configureLoRa(settings));
    assert(!mesh.streamFmIq(request, result) && !state.streams);
    assert(tool.acquire());
    auto invalid = request;
    invalid.frequency_hz = 98000000;
    assert(!tool.streamFmIq(invalid, result) && result.capture_error == -1 && !state.streams);
    invalid = request;
    invalid.cancelled = nullptr;
    assert(!tool.streamFmIq(invalid, result) && !state.streams);
    invalid = request;
    invalid.staging_bytes = 1279;
    assert(!tool.streamFmIq(invalid, result) && !state.streams);
    std::promise<void> entered, release;
    state.entered = &entered;
    state.resume = release.get_future().share();
    auto started = entered.get_future();
    auto worker = std::async(std::launch::async, [&]
                             { return tool.streamFmIq(request, result); });
    assert(started.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    auto status = std::async(std::launch::async, [&]
                             { (void)tool.isOnline(); (void)tool.stats(); return !tool.acquire() && !coordinator.setEnabled(false); });
    assert(status.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready && status.get());
    const uint8_t byte = 0;
    auto competing = std::async(std::launch::async, [&]
                                { return mesh.transmit(&byte, 1); });
    assert(competing.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    release.set_value();
    assert(worker.get() && result.validated_bytes == 4 && result.nominal_rate_hz == 250000);
    assert(!competing.get() && !state.transmissions);
    state.entered = nullptr;
    request.allow_image_calibration_warning = true;
    assert(tool.streamFmIq(request, result) && result.setup.calibration_warning && result.setup.chip_errors == 0x0080);
    assert(tool.releaseWithStatus());
    assert(state.configurations == 2 && mesh.transmit(&byte, 1));
    assert(tool.acquire());
    state.recovery_error = -5;
    assert(!tool.streamFmIq(request, result) && !tool.isOnline());
    const auto calls = state.streams;
    state.recovery_error = 0;
    assert(!tool.streamFmIq(request, result) && state.streams == calls);
    assert(tool.configureLoRa(settings) && tool.startReceive() && tool.streamFmIq(request, result));
    assert(tool.releaseWithStatus());
    mesh.release();
}
