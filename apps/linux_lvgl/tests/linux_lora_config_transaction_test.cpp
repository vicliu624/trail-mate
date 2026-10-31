#include "platform/linux/packet_radio.h"
#include "platform/shared/radio/lora_receive_config.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using Config = platform::linux_runtime::RadioSettings;
using Transaction = platform::shared::radio::LoRaReceiveConfig<Config>;

struct Radio
{
    Config hardware{};
    std::vector<std::string> commands;
    int fail_at = -1;
    bool receiving = true;
    bool explicit_header = false;
    bool inverted = true;
    bool pa_configured = true;

    int command(const char* name)
    {
        commands.emplace_back(name);
        return static_cast<int>(commands.size()) - 1 == fail_at ? -1 : 0;
    }
    template <typename T>
    int write(const char* name, T& field, T value)
    {
        assert(!receiving);
        const int result = command(name);
        if (!result) field = value;
        return result;
    }
    int standby()
    {
        const int result = command("standby");
        if (!result) receiving = false;
        return result;
    }
    int startReceive()
    {
        assert(pa_configured);
        const int result = command("receive");
        if (!result) receiving = true;
        return result;
    }
    int setFrequency(float value)
    {
        const int result = write("frequency", hardware.freq_mhz, value);
        if (!result) pa_configured = false;
        return result;
    }
    int setBandwidth(float value) { return write("bandwidth", hardware.bw_khz, value); }
    int setSpreadingFactor(uint8_t value) { return write("SF", hardware.sf, value); }
    int setCodingRate(uint8_t value) { return write("CR", hardware.cr, value); }
    int setSyncWord(uint8_t value) { return write("sync", hardware.sync_word, value); }
    int setOutputPower(int8_t value)
    {
        const int result = write("power", hardware.tx_power_dbm, value);
        if (!result) pa_configured = true;
        return result;
    }
    int setPreambleLength(uint16_t value) { return write("preamble", hardware.preamble_len, value); }
    int setCRC(uint8_t value) { return write("CRC", hardware.crc_len, value); }
    int explicitHeader() { return write("header", explicit_header, true); }
    int invertIQ(bool value) { return write("IQ", inverted, value); }
    void clear()
    {
        commands.clear();
        fail_at = -1;
    }
};

static bool same(const Config& a, const Config& b)
{
    return a.freq_mhz == b.freq_mhz && a.bw_khz == b.bw_khz && a.sf == b.sf && a.cr == b.cr &&
           a.tx_power_dbm == b.tx_power_dbm && a.preamble_len == b.preamble_len &&
           a.sync_word == b.sync_word && a.crc_len == b.crc_len;
}

static bool apply(Transaction& transaction, Radio& radio, const Config& config)
{
    return transaction.apply(radio, config, [](int result, const char*)
                             { return result == 0; });
}

int main()
{
    const Config base{};
    Transaction transaction;
    Radio radio;
    assert(apply(transaction, radio, base));
    assert(radio.commands.size() == 12 && radio.receiving && radio.explicit_header && !radio.inverted);
    radio.clear();
    assert(apply(transaction, radio, base));
    assert((radio.commands == std::vector<std::string>{"standby", "receive"}));

    // Each independent setting must reach the device, while unchanged settings
    // are omitted. RX must restart even when no modulation value changes.
    for (int field = 0; field < 8; ++field)
    {
        Config next = base;
        switch (field)
        {
        case 0:
            next.freq_mhz += 1.0f;
            break;
        case 1:
            next.bw_khz = 125;
            break;
        case 2:
            next.sf = 9;
            break;
        case 3:
            next.cr = 6;
            break;
        case 4:
            next.tx_power_dbm = 18;
            break;
        case 5:
            next.preamble_len = 20;
            break;
        case 6:
            next.sync_word = 0x34;
            break;
        case 7:
            next.crc_len = 0;
            break;
        }
        radio.clear();
        assert(apply(transaction, radio, next));
        assert(radio.commands.size() == (field == 0 ? 4U : 3U) && same(radio.hardware, next) && radio.receiving);
        assert(apply(transaction, radio, base));
    }

    // Reset/FSK/raw-IQ owners invalidate before changing the modem. The next
    // LoRa request must restore fixed packet settings as well as all fields.
    transaction.invalidate();
    radio.explicit_header = false;
    radio.inverted = true;
    radio.clear();
    assert(apply(transaction, radio, base));
    assert(radio.commands.size() == 12 && radio.explicit_header && !radio.inverted);

    for (int failure = 0; failure < 12; ++failure)
    {
        transaction.invalidate();
        radio.clear();
        radio.fail_at = failure;
        assert(!apply(transaction, radio, base));
        radio.clear();
        assert(apply(transaction, radio, base));
        assert(radio.commands.size() == 12 && radio.receiving && same(radio.hardware, base));
    }

    Config next = base;
    next.freq_mhz += 1.0f;
    next.sf = 9;
    for (int failure = 0; failure < 5; ++failure)
    {
        radio.clear();
        radio.fail_at = failure;
        assert(!apply(transaction, radio, next));
        radio.clear();
        assert(apply(transaction, radio, base));
        assert(radio.commands.size() == 12 && same(radio.hardware, base) && radio.receiving);
    }

    // Transport health may reject a successful chip result after a partial write.
    int checks = 0;
    radio.clear();
    assert(!transaction.apply(radio, next, [&](int result, const char*)
                              { return result == 0 && ++checks != 2; }));
    radio.clear();
    assert(apply(transaction, radio, base));
    assert(radio.commands.size() == 12 && same(radio.hardware, base));
    std::puts("LoRa configuration: full=12, identical=2, SF-only=3, retune=4; PA dependency and failure recovery verified");
}
