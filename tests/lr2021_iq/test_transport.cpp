#include "lr2021_iq.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Pinned RadioLib protocol constants; deliberately asserted at the wire boundary.
#define RADIOLIB_MODULE_SPI_WIDTH_CMD 1
#define RADIOLIB_LR2021_CMD_WRITE_REG_MEM_32 0x0104
#define RADIOLIB_LR2021_CMD_READ_REG_MEM_32 0x0106
#define RADIOLIB_LR2021_CMD_GET_VERSION 0x0101

namespace app
{
struct AppTasks
{
    inline static bool paused = false;
    static bool areRadioTasksPaused() { return paused; }
};
} // namespace app
struct Board
{
    struct Pins
    {
        unsigned nss = 9;
    };
    static const Pins& loraModulePins()
    {
        static const Pins pins;
        return pins;
    }
    inline static bool rx_path_ok = true;
    static Board& instance()
    {
        static Board board;
        return board;
    }
    bool setLoraRfSwitchTransmit(bool tx)
    {
        assert(!tx);
        return rx_path_ok;
    }
};

struct Module
{
    enum BitWidth
    {
        BITS_0 = 0,
        BITS_16 = 16
    };
    struct
    {
        BitWidth widths[3] = {BITS_16, BITS_16, BITS_16};
    } spiConfig;
    uint16_t command = 0;
    std::vector<uint8_t> request;
    unsigned reads = 0;
    bool verified = false;
    int write_error = 0, read_error = 0;
    std::array<uint8_t, 4> reply{0x81, 0x92, 0xa3, 0xb4};
    bool* damage_hal_on_read = nullptr;
    int SPIwriteStream(uint16_t cmd, const uint8_t* data, size_t bytes, bool wait, bool verify)
    {
        assert(wait && spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] == BITS_16);
        command = cmd;
        request.clear();
        if (bytes) request.assign(data, data + bytes);
        verified = verify;
        return write_error;
    }
    int SPIreadStream(uint16_t cmd, uint8_t* data, size_t bytes, bool wait, bool verify)
    {
        assert(cmd == 0 && wait && !verify);
        assert(spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] == BITS_0);
        ++reads;
        for (size_t n = 0; n < bytes; ++n) data[n] = reply[n % reply.size()];
        if (damage_hal_on_read) *damage_hal_on_read = false;
        return read_error;
    }
};
struct Harness
{
    Module module;
    struct
    {
        bool valid = true;
        unsigned terms = 0, inits = 0;
        enum
        {
            GpioLevelLow = 0,
            GpioLevelHigh = 1
        };
        bool bus = false, selected = false, damage_transfer = false;
        unsigned transfers = 0, bus_starts = 0, bus_ends = 0;
        uint16_t fifo_status = 0x0400;
        void spiBeginTransaction()
        {
            ++bus_starts;
            bus = valid;
        }
        void spiEndTransaction()
        {
            ++bus_ends;
            bus = false;
        }
        void digitalWrite(uint32_t pin, unsigned level)
        {
            assert(pin == 9);
            if (!level && !bus) return;
            selected = level == GpioLevelLow;
        }
        void spiTransfer(uint8_t* output, size_t bytes, uint8_t* input)
        {
            if (!valid) return;
            assert(bus && selected && bytes >= 3 && bytes <= 258);
            assert(output[0] == 0 && output[1] == 1);
            ++transfers;
            input[0] = fifo_status >> 8;
            input[1] = fifo_status & 255;
            for (size_t n = 2; n < bytes; ++n)
            {
                assert(output[n] == 0);
                input[n] = static_cast<uint8_t>(n - 2);
            }
            if (damage_transfer) valid = false;
        }
        bool healthy() const { return valid; }
        void term() { ++terms; }
        void init() { ++inits; }
    } hal;
    uint8_t iq_fifo_tx[258]{}, iq_fifo_rx[258]{};
    struct Config
    {
        float freq_mhz = 433.175f, bw_khz = 62.5f;
        unsigned sf = 7;
    } applied;
    unsigned users = 1;
    bool fsk = false, transmitting = false, receiving = true;
    std::atomic<bool> online{true};
    struct
    {
        int error = 0;
        unsigned standby_calls = 0;
        int standby()
        {
            ++standby_calls;
            return error;
        }
    } radio;
    struct
    {
        int error = 0;
        unsigned applies = 0;
        Config restored{};
        template <typename Radio, typename Check>
        bool apply(Radio&, const Config& config, Check check)
        {
            ++applies;
            restored = config;
            return check(error, "fixture apply");
        }
    } lora_config;
    unsigned begins = 0;
    bool begin_ok = true;
    bool beginLoRa(const Config& config)
    {
        assert(&config == &applied);
        ++begins;
        receiving = false;
        return begin_ok;
    }
    bool check(int code, const char*) { return !code && hal.healthy(); }
#include "p4_iq_transport.inc"
};

int main()
{
    {
        Harness fifo;
        std::array<uint8_t, 256> payload{};
        for (const unsigned size : {1u, 64u, 256u})
        {
            assert(fifo.iqFifoRead(payload.data(), size) == 0);
            for (unsigned n = 0; n < size; ++n) assert(payload[n] == static_cast<uint8_t>(n));
            assert(!fifo.hal.bus && !fifo.hal.selected);
        }
        const auto transfers = fifo.hal.transfers;
        assert(fifo.iqFifoRead(nullptr, 64) == LR2021_IQ_ARGUMENT);
        assert(fifo.iqFifoRead(payload.data(), 0) == LR2021_IQ_ARGUMENT);
        assert(fifo.iqFifoRead(payload.data(), 257) == LR2021_IQ_ARGUMENT);
        assert(fifo.hal.transfers == transfers);
        for (const uint16_t status : {0x0000, 0xf000, 0x0408, 0x0200, 0x0800, 0x0430, 0x0407})
        {
            fifo.hal.fifo_status = status;
            payload.fill(0xa5);
            assert(fifo.iqFifoRead(payload.data(), 256) == LR2021_IQ_DATA);
            for (auto value : payload) assert(value == 0xa5);
            assert(!fifo.hal.bus && !fifo.hal.selected);
        }
        fifo.hal.fifo_status = 0x0400;
        fifo.hal.damage_transfer = true;
        assert(fifo.iqFifoRead(payload.data(), 256) == LR2021_IQ_IO);
        assert(!fifo.hal.bus && !fifo.hal.selected && fifo.hal.bus_starts == fifo.hal.bus_ends);
        assert(fifo.iqFifoRead(payload.data(), 256) == LR2021_IQ_IO);
        assert(!fifo.hal.bus && !fifo.hal.selected && fifo.hal.bus_starts == fifo.hal.bus_ends);
    }
    Harness h;
    uint32_t value = 0;
    assert(h.iqRead32(0x802000, &value) == 0);
    assert(h.module.command == 0x0106);
    assert((h.module.request == std::vector<uint8_t>{0x80, 0x20, 0, 1}));
    assert(value == 0x8192a3b4);
    assert(h.module.spiConfig.widths[1] == Module::BITS_16);
    assert(!h.module.verified);
    assert(h.iqWrite32(0x802004, 0xdeadbeef) == 0);
    assert(h.module.command == 0x0104 && h.module.verified);
    assert((h.module.request == std::vector<uint8_t>{0x80, 0x20, 4, 0xde, 0xad, 0xbe, 0xef}));

    h.module.write_error = -42;
    const auto reads = h.module.reads;
    value = 123;
    assert(h.iqRead32(0x802000, &value) == -42);
    assert(value == 123 && h.module.reads == reads);
    assert(h.module.spiConfig.widths[1] == Module::BITS_16);
    assert(h.iqWrite32(0x802000, 0) == -42);

    h.module.write_error = 0;
    h.module.read_error = -43;
    assert(h.iqRead32(0x802000, &value) == -43);
    assert(value == 123);
    assert(h.module.spiConfig.widths[1] == Module::BITS_16);
    h.module.read_error = 0;
    h.hal.valid = false;
    assert(h.iqRead32(0x802000, &value) == LR2021_IQ_IO);
    assert(value == 123 && h.module.spiConfig.widths[1] == Module::BITS_16);
    assert(h.iqWrite32(0x802000, 0) == LR2021_IQ_IO);
    h.hal.valid = true;
    h.module.damage_hal_on_read = &h.hal.valid;
    assert(h.iqRead32(0x802000, &value) == LR2021_IQ_IO);
    assert(value == 123 && h.module.spiConfig.widths[1] == Module::BITS_16);
    h.module.damage_hal_on_read = nullptr;
    h.hal.valid = true;

    h.module.reply = {1, 24, 0, 0};
    app::AppTasks::paused = true;
    lr2021_iq_profile profile{};
    assert(h.iqSnapshot(&profile) == 0);
    assert(profile.lr2021 && profile.configured && profile.exclusive_lora && profile.receiving);
    assert(profile.fw_major == 1 && profile.fw_minor == 24);
    assert(profile.bandwidth_hz == 62500 && profile.spreading_factor == 7);
    app::AppTasks::paused = false;
    assert(h.iqSnapshot(&profile) == 0 && !profile.exclusive_lora);
    app::AppTasks::paused = true;
    h.fsk = true;
    assert(h.iqSnapshot(&profile) == 0 && !profile.exclusive_lora);
    h.fsk = false;
    h.transmitting = true;
    assert(h.iqSnapshot(&profile) == 0 && !profile.exclusive_lora);
    h.transmitting = false;
    h.users = 0;
    assert(h.iqSnapshot(&profile) == 0 && !profile.exclusive_lora);
    h.users = 1;

    assert(h.iqReset() == 0);
    assert(h.hal.terms == 1 && h.hal.inits == 1 && h.begins == 1 && !h.receiving);
    assert(h.applied.bw_khz == 62.5f && h.applied.sf == 7);
    profile.receiving = true;
    assert(h.iqRestore(&profile) == 0 && h.receiving);
    assert(h.lora_config.applies == 1 && h.radio.standby_calls == 0);
    assert(h.lora_config.restored.bw_khz == 62.5f);
    profile.receiving = false;
    assert(h.iqRestore(&profile) == 0 && !h.receiving && h.radio.standby_calls == 1);
    h.radio.error = -42;
    assert(h.iqRestore(&profile) == LR2021_IQ_IO);
    h.radio.error = 0;
    h.lora_config.error = -43;
    assert(h.iqRestore(&profile) == LR2021_IQ_IO);
    h.lora_config.error = 0;
    Board::rx_path_ok = false;
    assert(h.iqRestore(&profile) == LR2021_IQ_IO);
    Board::rx_path_ok = true;
    h.begin_ok = false;
    assert(h.iqReset() == LR2021_IQ_IO);
    h.hal.valid = false;
    const auto begins = h.begins;
    assert(h.iqReset() == LR2021_IQ_IO && h.begins == begins);
    std::puts("P4 IQ transport: two-frame read, command-width restoration, BE address/data and transport/status rejection passed");
    std::puts("P4 IQ recovery: actual firmware/config snapshot, pause/modem/TX guards, stopped reset, RX/standby restoration and failure propagation passed");
}
