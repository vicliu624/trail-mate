#include "platform/esp/idf_common/lr2021_radio.h"
#include "platform/esp/idf_common/p4_fm_mode_handoff.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace platform::esp::idf_common
{
namespace
{
struct State;
static State& state();
static int operation(const char*);
struct Board
{
    static Board& instance()
    {
        static Board value;
        return value;
    }
    bool setLoraRfSwitchTransmit(bool tx)
    {
        assert(!tx);
        return operation("board.rx") == 0;
    }
};
struct Hal
{
    bool good = true;
    void term() { (void)operation("hal.term"); }
    void init() { good = operation("hal.init") == 0; }
    bool healthy() const { return good; }
};
struct Radio
{
    detail::GfskConfig observed;
    int beginGFSK(float frequency, float bitrate, float deviation, float bandwidth, int8_t power, uint16_t preamble, float tcxo)
    {
        assert(tcxo == 3.0f);
        observed.frequency = frequency;
        observed.bitrate = bitrate;
        observed.deviation = deviation;
        observed.bandwidth = bandwidth;
        observed.power = power;
        observed.preamble = preamble;
        return operation("fsk.begin");
    }
    int setSyncWord(uint8_t* sync, size_t size)
    {
        assert(size <= observed.sync.size());
        observed.sync_size = size;
        std::memcpy(observed.sync.data(), sync, size);
        return operation("fsk.sync");
    }
    int setCRC(uint8_t bytes, uint16_t initial, uint16_t polynomial, bool inverted)
    {
        assert(initial == 0x1d0f && polynomial == 0x1021 && inverted);
        observed.crc = bytes;
        return operation("fsk.crc");
    }
    int setWhitening(bool enabled)
    {
        assert(!enabled);
        return operation("fsk.whitening");
    }
    int standby() { return operation("fsk.standby"); }
};
struct LoRaTransaction
{
    void invalidate() {}
    template <typename Check>
    bool apply(Radio&, int, Check check) { return check(operation("lora.apply"), "lora.apply"); }
};
struct State
{
    std::mutex mutex;
    Hal hal;
    Radio radio;
    LoRaTransaction lora_config;
    int applied = 0;
    uint8_t sync[8]{};
    detail::GfskConfig fsk_profile{}, requested_fsk{};
    bool fsk = false, fsk_profile_valid = false, receiving = false, transmitting = false;
    std::atomic_bool online{true};
    std::string failure;
    bool injected = false;
    std::vector<std::string> calls;
    bool fail(const char*) { return false; }
    bool check(int code, const char*)
    {
        if (!code && hal.healthy()) return true;
        online = receiving = transmitting = false;
        return false;
    }
    bool rfSwitch() { return check(operation("fsk.rf-switch"), "rf switch"); }
    bool receive()
    {
        assert(online);
        receiving = check(operation("fsk.rx"), "rx");
        return receiving;
    }
    bool beginLoRa(int) { return check(operation("lora.begin"), "lora begin"); }
#include "p4_fm_mode_state.inc"
};
static std::unique_ptr<State> current;
static State& state() { return *current; }
static int operation(const char* name)
{
    auto& s = state();
    s.calls.emplace_back(name);
    if (!s.injected && s.failure == name)
    {
        s.injected = true;
        return -1;
    }
    return 0;
}
} // namespace
Lr2021Radio& Lr2021Radio::instance()
{
    static Lr2021Radio value;
    return value;
}
#include "p4_fm_mode_config.inc"
} // namespace platform::esp::idf_common

using namespace platform::esp::idf_common;
using platform::shared::radio::FmIqResult;
static void fresh()
{
    current = std::make_unique<State>();
}
static void configured()
{
    fresh();
    const uint8_t sync[] = {0xaa, 0x12, 0x34, 0x56};
    assert(Lr2021Radio::instance().configureFsk(433.25f, 14, 38.4f, 20, 156.2f, 32, sync, sizeof(sync), 2));
    assert(state().fsk_profile_valid && state().fsk && state().receiving);
    state().calls.clear();
}
static void same(const detail::GfskConfig& a, const detail::GfskConfig& b)
{
    assert(a.frequency == b.frequency && a.bitrate == b.bitrate && a.deviation == b.deviation &&
           a.bandwidth == b.bandwidth && a.power == b.power && a.preamble == b.preamble &&
           a.sync == b.sync && a.sync_size == b.sync_size && a.crc == b.crc);
}
int main()
{
    fresh();
    FmIqResult result;
    unsigned captures = 0;
    assert(detail::withFmPacketMode(state(), result, [&]
                                    { ++captures; return true; }));
    assert(captures == 1 && state().calls.empty());
    state().fsk = true;
    assert(!detail::withFmPacketMode(state(), result, [&]
                                     { ++captures; return true; }));
    assert(captures == 1 && result.capture_error == LR2021_IQ_STATE && state().calls.empty());
    for (bool receiving : {false, true})
    {
        configured();
        state().receiving = receiving;
        result = {};
        const auto saved = state().fsk_profile;
        assert(detail::withFmPacketMode(state(), result, [&]
                                        {
            assert(!state().fsk && state().receiving && state().online);
            assert((state().calls == std::vector<std::string>{"lora.begin", "board.rx", "lora.apply"}));
            state().calls.emplace_back("capture");
            return true; }));
        assert(state().fsk && state().online && state().receiving == receiving);
        same(state().fsk_profile, saved);
        same(state().radio.observed, saved);
        assert(state().calls.back() == (receiving ? "fsk.rx" : "fsk.standby"));
    }
    configured();
    result = {};
    assert(!detail::withFmPacketMode(state(), result, [&]
                                     { result.capture_error = LR2021_IQ_DATA; return false; }));
    assert(state().online && state().fsk && state().receiving && result.capture_error == LR2021_IQ_DATA && !result.recovery_error);
    for (const char* failure : {"lora.begin", "board.rx", "lora.apply"})
    {
        configured();
        state().failure = failure;
        result = {};
        assert(!detail::withFmPacketMode(state(), result, []
                                         { assert(false && "capture ran after staging failure"); return true; }));
        assert(state().injected && state().online && state().fsk && result.capture_error && !result.recovery_error);
        same(state().radio.observed, state().fsk_profile);
    }
    for (const char* failure : {"hal.init", "fsk.begin", "fsk.rf-switch", "fsk.sync", "fsk.crc", "fsk.whitening", "fsk.rx", "fsk.standby"})
    {
        configured();
        state().receiving = std::strcmp(failure, "fsk.standby") != 0;
        state().failure = failure;
        result = {};
        assert(!detail::withFmPacketMode(state(), result, []
                                         { return true; }));
        assert(state().injected && !state().online && result.recovery_error);
    }
    // A failed reconfiguration cannot overwrite the last committed FSK profile.
    configured();
    const auto saved = state().fsk_profile;
    state().failure = "fsk.rx";
    const uint8_t sync[] = {1, 2};
    assert(!Lr2021Radio::instance().configureFsk(434, 10, 50, 25, 200, 24, sync, sizeof(sync), 0));
    same(saved, state().fsk_profile);
    assert(!state().online);
}
