#include "lr2021_iq.h"
#include "platform/linux/packet_radio.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#define RADIOLIB_MODULE_SPI_WIDTH_CMD 1
#define RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32 0x0104
#define RADIOLIB_LR2021_CMD_READ_REG_MEM_32 0x0106
#define RADIOLIB_LR2021_CMD_GET_VERSION 0x0101

using namespace platform::linux_runtime;
struct Module
{
    enum BitWidth
    {
        BITS_0,
        BITS_16 = 16
    };
    struct
    {
        BitWidth widths[3] = {BITS_16, BITS_16, BITS_16};
    } spiConfig;
    uint16_t command = 0;
    std::vector<uint8_t> request;
    int write_error = 0, read_error = 0;
    bool verified = false;
    unsigned reads = 0;
    std::array<uint8_t, 4> reply{0x12, 0x34, 0x56, 0x78};
    int SPIwriteStream(uint16_t cmd, const uint8_t* data, size_t bytes, bool wait, bool verify)
    {
        assert(wait && spiConfig.widths[1] == BITS_16);
        command = cmd;
        request.clear();
        if (bytes) request.assign(data, data + bytes);
        verified = verify;
        return write_error;
    }
    int SPIreadStream(uint16_t cmd, uint8_t* data, size_t bytes, bool wait, bool verify)
    {
        assert(cmd == 0 && wait && !verify && spiConfig.widths[1] == BITS_0);
        ++reads;
        for (size_t n = 0; n < bytes; ++n) data[n] = reply[n % reply.size()];
        return read_error;
    }
};
struct Harness
{
    Module module_;
    struct Hal
    {
        bool valid = true;
        unsigned terms = 0, inits = 0;
        bool healthy() const { return valid; }
        void term() { ++terms; }
        void init() { ++inits; }
    } hal_;
    struct Radio
    {
        int error = 0;
        unsigned begins = 0, standbys = 0;
        float frequency = 0, tcxo = 0;
        int begin(float freq, float, uint8_t, uint8_t, uint8_t, int8_t, uint16_t, float voltage)
        {
            ++begins;
            frequency = freq;
            tcxo = voltage;
            return error;
        }
        int standby()
        {
            ++standbys;
            return error;
        }
    } radio_;
    struct Config
    {
        int error = 0;
        unsigned invalidations = 0, applies = 0;
        RadioSettings restored;
        void invalidate() { ++invalidations; }
        template <typename Check>
        bool apply(Radio&, const RadioSettings& settings, Check check)
        {
            ++applies;
            restored = settings;
            return check(error, "fixture apply");
        }
    } lora_config_;
    bool online_ = true, fsk_ = false;
    unsigned users_ = 1;
    RadioSettings settings_;
    PacketRadioHardware hardware_;
    bool check(int code, const char*) { return !code && hal_.healthy(); }
#include "linux_iq_transport.inc"
};

int main()
{
    Harness h;
    uint32_t value = 99;
    assert(!h.iqRead32(0x802000, &value) && value == 0x12345678);
    assert(h.module_.command == 0x0106);
    assert((h.module_.request == std::vector<uint8_t>{0x80, 0x20, 0, 1}));
    assert(h.module_.spiConfig.widths[1] == Module::BITS_16);
    assert(!h.iqWrite32(0x802004, 0xdeadbeef));
    assert(h.module_.command == 0x0104 && h.module_.verified);
    assert((h.module_.request == std::vector<uint8_t>{0x80, 0x20, 4, 0xde, 0xad, 0xbe, 0xef}));
    h.module_.read_error = -42;
    value = 99;
    assert(h.iqRead32(0x802000, &value) == -42 && value == 99);
    assert(h.module_.spiConfig.widths[1] == Module::BITS_16);
    h.module_.read_error = 0;
    h.module_.write_error = -43;
    const auto reads = h.module_.reads;
    assert(h.iqRead32(0x802000, &value) == -43 && h.module_.reads == reads && value == 99);
    h.module_.write_error = 0;
    h.hal_.valid = false;
    assert(h.iqWrite32(0x802000, 0) == LR2021_IQ_IO);
    h.hal_.valid = true;

    h.module_.reply = {1, 24, 0, 0};
    h.settings_.freq_mhz = 433;
    h.settings_.bw_khz = 62.5f;
    h.settings_.sf = 7;
    h.settings_.sync_word = 0x34;
    lr2021_iq_profile profile{};
    assert(!h.iqSnapshot(&profile));
    assert(profile.lr2021 && profile.configured && profile.exclusive_lora && profile.receiving);
    assert(profile.fw_major == 1 && profile.fw_minor == 24 && profile.frequency_hz == 433000000);
    assert(profile.bandwidth_hz == 62500 && profile.spreading_factor == 7);
    h.users_ = 2;
    assert(!h.iqSnapshot(&profile) && !profile.exclusive_lora);
    h.users_ = 1;
    h.fsk_ = true;
    assert(!h.iqSnapshot(&profile) && !profile.exclusive_lora);
    h.fsk_ = false;
    h.online_ = false;
    assert(!h.iqSnapshot(&profile) && !profile.configured);
    h.online_ = true;
    h.module_.reply = {1, 25, 0, 0};
    assert(!h.iqSnapshot(&profile) && profile.fw_minor == 25); // Actual ROM, never hardcoded.

    h.hardware_.tcxo_voltage = 1.8f;
    assert(!h.iqReset());
    assert(h.hal_.terms == 1 && h.hal_.inits == 1 && h.radio_.begins == 1);
    assert(h.radio_.frequency == 433 && h.radio_.tcxo == 1.8f && h.lora_config_.invalidations == 1);
    profile.receiving = true;
    assert(!h.iqRestore(&profile) && h.radio_.standbys == 0);
    assert(h.lora_config_.restored.sync_word == 0x34 && h.lora_config_.restored.crc_len == 2);
    profile.receiving = false;
    assert(!h.iqRestore(&profile) && h.radio_.standbys == 1);
    h.radio_.error = -44;
    assert(h.iqRestore(&profile) == LR2021_IQ_IO && h.iqReset() == LR2021_IQ_IO);
    h.radio_.error = 0;
    h.lora_config_.error = -45;
    assert(h.iqRestore(&profile) == LR2021_IQ_IO);
    h.hal_.valid = false;
    const auto begins = h.radio_.begins;
    assert(h.iqReset() == LR2021_IQ_IO && h.radio_.begins == begins);
    h.hal_.valid = true;
    h.hardware_.rf_switch_profile = Lr2021RfSwitchProfile::Cm0_16E8;
    assert(!h.iqReset());
    assert(h.module_.command == 0x0113 && h.module_.verified);
    assert((h.module_.request == std::vector<uint8_t>{6, 0x18}));
    h.module_.write_error = -46;
    assert(h.iqReset() == LR2021_IQ_IO);
    std::puts("Linux IQ production adapter: BE register transactions, command-width recovery, actual ROM/config, exclusive LoRa snapshot, TCXO/reset and prior config restoration passed");
}
