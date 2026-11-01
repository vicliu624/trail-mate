#include "platform/linux/oneseg_capture_process.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
using namespace platform::linux_runtime;
struct State
{
    std::array<uint8_t, 1280> staging{};
    std::vector<uint8_t> bytes;
    bool reject = false, cancel = false, timed = false;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    static bool accept(void* context, const uint8_t* data, size_t count, uint64_t pair)
    {
        auto& self = *static_cast<State*>(context);
        assert(!(count & 1) && pair * 2 == self.bytes.size());
        if (self.reject) return false;
        self.bytes.insert(self.bytes.end(), data, data + count);
        return true;
    }
    static bool cancelled(void* context)
    {
        auto& self = *static_cast<State*>(context);
        return (self.cancel && self.bytes.size() == 2048) ||
               (self.timed && std::chrono::steady_clock::now() - self.started > std::chrono::milliseconds(50));
    }
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    for (const char* mode : {"normal", "error", "missing", "magic", "short", "extra", "odd", "rate", "reject", "reject_receipt", "cancel", "hung"})
    {
        setenv("ONESEG_CAPTURE_FIXTURE", mode, 1);
        State state;
        state.reject = !std::strcmp(mode, "reject") || !std::strcmp(mode, "reject_receipt");
        state.cancel = !std::strcmp(mode, "cancel");
        state.timed = !std::strcmp(mode, "hung");
        OneSegIqRequest request{13, state.staging.data(), state.staging.size(), State::accept, State::cancelled, &state};
        OneSegIqResult result;
        const bool okay = run_oneseg_capture_process(argv[1], "/fixture.conf", request, 450000, result);
        assert(okay == (!std::strcmp(mode, "normal") || state.cancel));
        assert(result.validated_bytes == state.bytes.size());
        for (size_t i = 0; i < state.bytes.size(); ++i) assert(state.bytes[i] == uint8_t(i));
        if (!std::strcmp(mode, "normal")) assert(state.bytes.size() == 2048 && result.elapsed_us == 1234567);
        if (!std::strcmp(mode, "error")) assert(result.capture_error == -4);
        if (state.reject) assert(result.sink_rejected && result.capture_error == -4);
        if (state.cancel) assert(result.cancelled && !result.capture_error);
        if (state.timed) assert(result.cancelled && result.capture_error &&
                                std::chrono::steady_clock::now() - state.started < std::chrono::seconds(3));
        int status = 0;
        assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
        std::fprintf(stderr, "capture process %s passed\n", mode);
    }
    State state;
    OneSegIqRequest request{13, state.staging.data(), state.staging.size(), State::accept, State::cancelled, &state};
    OneSegIqResult result;
    assert(!run_oneseg_capture_process("/does-not-exist", "/fixture.conf", request, 450000, result));
    assert(result.capture_error);
    setenv("ONESEG_CAPTURE_FIXTURE", "tuning", 1);
    request.reception.enabled = true;
    request.reception.gain_step = 13;
    request.reception.spi_clock_hz = 16000000;
    assert(run_oneseg_capture_process(argv[1], "/fixture.conf", request, 450000, result));
    assert(result.nominal_rate_hz == 450000 && state.bytes.size() == 2048);
    request.reception.spi_clock_hz = 1000000;
    assert(!run_oneseg_capture_process(argv[1], "/fixture.conf", request, 450000, result));
    assert(result.capture_error);
}
