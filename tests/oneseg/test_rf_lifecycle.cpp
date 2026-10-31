#include "lr2021_iq.h"
#include "platform/linux/packet_radio.h"
#include <cassert>
#include <cmath>

using namespace platform::linux_runtime;
struct Harness
{
    PacketRadioHardware hardware_;
    RadioSettings settings_;
    RadioStatistics statistics_;
    FskSettings fsk_settings_;
    std::mutex mutex_;
    bool online_ = false, fsk_ = false, receiving = false, rf_ready = false;
    unsigned users_ = 0, rf_calls = 0, fail_rf_at = 0;
    struct Hal
    {
        bool healthy() const { return true; }
        void term() {}
        void init() {}
    } hal_;
    struct Radio
    {
        Harness& owner;
        int begin(float, float, uint8_t, uint8_t, uint8_t, int8_t, uint16_t, float)
        {
            return reset();
        }
        int beginGFSK(float, float, float, float, int8_t, uint16_t, float)
        {
            return reset();
        }
        int reset()
        {
            owner.receiving = false;
            owner.rf_ready = owner.hardware_.rf_switch_profile == Lr2021RfSwitchProfile::None;
            return 0;
        }
        int setCRC(uint8_t) { return 0; }
        int setCRC(uint8_t, uint16_t, uint16_t, bool) { return 0; }
        int setSyncWord(const uint8_t*, size_t) { return 0; }
        int setWhitening(bool) { return 0; }
        int startReceive()
        {
            // Any production lifecycle path that misses RF setup fails here.
            assert(owner.rf_ready);
            owner.receiving = true;
            return 0;
        }
    } radio_{*this};
    struct Config
    {
        void invalidate() {}
        template <typename Check>
        bool apply(Radio& radio, const RadioSettings&, Check check)
        {
            return check(radio.startReceive(), "test receive");
        }
    } lora_config_;
    bool check(int code, const char*)
    {
        if (code) online_ = false;
        return code == 0;
    }
    bool fail(const char*) { return false; }
    bool supportsFskConfiguration(const FskSettings& config) const
    {
        return valid_lr2021_fsk_settings(config);
    }
    int iqWrite(uint16_t op, const uint8_t* data, size_t bytes)
    {
        assert(bytes == 2 && (op == 0x0112 || op == 0x0113));
        if (++rf_calls == fail_rf_at) return -77;
        if (op == 0x0113 && data[0] == 6 && data[1] == 0x18) rf_ready = true;
        return 0;
    }
#include "rf_acquire.inc"
#include "rf_board.inc"
#include "rf_fsk.inc"
#include "rf_lora.inc"
#include "rf_reset.inc"
};
int main()
{
    FskSettings fsk;
    fsk.freq_mhz = 433;
    for (const auto profile : {Lr2021RfSwitchProfile::None, Lr2021RfSwitchProfile::Cm0_16E8})
    {
        Harness h;
        h.hardware_.rf_switch_profile = profile;
        assert(h.acquire() && h.receiving && h.online_);
        assert(h.configureFsk(fsk) && h.receiving && h.fsk_);
        assert(h.configureLoRa(h.settings_) && h.receiving && !h.fsk_);
        assert(!h.iqReset() && !h.receiving && h.rf_ready);
        assert(h.rf_calls == (profile == Lr2021RfSwitchProfile::None ? 0u : 16u));
    }
    for (unsigned operation = 0; operation < 4; ++operation)
        for (unsigned failure = 1; failure <= 4; ++failure)
        {
            Harness h;
            h.hardware_.rf_switch_profile = Lr2021RfSwitchProfile::Cm0_16E8;
            if (operation) assert(h.acquire());
            if (operation == 2) assert(h.configureFsk(fsk));
            h.rf_calls = 0;
            h.fail_rf_at = failure;
            if (operation == 0) assert(!h.acquire());
            if (operation == 1) assert(!h.configureFsk(fsk));
            if (operation == 2) assert(!h.configureLoRa(h.settings_));
            if (operation == 3) assert(h.iqReset() == LR2021_IQ_IO);
            assert(!h.online_ && !h.receiving && h.rf_calls == failure);
        }
}
