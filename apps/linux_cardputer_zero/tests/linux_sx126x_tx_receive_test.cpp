#include "platform/linux/packet_radio.h"
#include "platform/linux/sx126x_radio.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace
{
using platform::linux_runtime::Sx126xPacket;
constexpr uint16_t kIrqTxDone = 0x0001, kIrqRxDone = 0x0002, kIrqHeaderErr = 0x0020,
                   kIrqCrcErr = 0x0040, kIrqTimeout = 0x0200, kIrqAll = 0xffff;
constexpr uint32_t kRxTimeoutInf = 0xffffff;
constexpr uint8_t kLoRaHeaderExplicit = 0, kLoRaCrcOn = 1, kLoRaCrcOff = 0,
                  kLoRaIqStandard = 0, kCmdSetPacketParams = 0x8c, kCmdWriteBuffer = 0x0e,
                  kCmdSetStandby = 0x80, kStandbyRc = 0;
void sleep_ms(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Transmitter
{
    std::mutex mutex_;
    bool online_ = true;
    uint8_t packet_type_ = 1;
    platform::linux_runtime::RadioSettings lora_config_;
    platform::linux_runtime::FskSettings fsk_config_;
    unsigned tx_packets_ = 0, receive_starts = 0;
    unsigned rx_packets_ = 0, rx_crc_errors_ = 0, rx_header_errors_ = 0,
             rx_timeouts_ = 0, rx_invalid_lengths_ = 0, rx_read_errors_ = 0, reads = 0;
    uint16_t irq_mask = kIrqRxDone, dio_mask = kIrqRxDone, outcome = kIrqTxDone;
    bool fail_start = false, fail_write = false, fail_receive = false;
    bool standby = false, fail_standby = false;
    std::string error;
    void updateLastIrqLocked(uint32_t) {}
    int getPacketLengthLocked(uint8_t* offset)
    {
        *offset = 0;
        return 2;
    }
    int readPacketLocked(uint8_t, uint8_t* data, size_t size)
    {
        ++reads;
        std::memset(data, 0x42, size);
        return 0;
    }
    bool readPacketStatusLocked(float* rssi, float* snr)
    {
        *rssi = -70;
        *snr = 8;
        return true;
    }
    // Start in continuous RX, as the resident radio does between sends. A host
    // mutex does not stop that hardware state or protect its shared RX/TX FIFO.
    bool setBufferBaseLocked(uint8_t, uint8_t) { return standby; }
    bool writeCommandLocked(uint8_t command, const uint8_t* bytes, size_t size, bool)
    {
        if (command == kCmdSetStandby)
        {
            assert(size == 1 && bytes[0] == kStandbyRc);
            standby = !fail_standby;
            return standby;
        }
        return command != kCmdSetPacketParams || standby;
    }
    bool transferLocked(const uint8_t*, uint8_t*, size_t) { return standby && !fail_write; }
    bool setDioIrqParamsLocked(uint16_t mask, uint16_t dio)
    {
        irq_mask = mask;
        dio_mask = dio;
        return true;
    }
    bool clearIrqLocked(uint16_t) { return true; }
    bool setTxLocked(uint32_t)
    {
        const bool ready = standby && !fail_start;
        standby = false;
        return ready;
    }
    uint32_t getIrqFlagsLocked() const { return outcome & irq_mask; }
    bool setRxLocked(uint32_t)
    {
        ++receive_starts;
        standby = false;
        return !fail_receive;
    }
    void setErrorLocked(const char* text) { error = text; }
#include "linux_sx126x_tx.inc"
};
} // namespace

int main()
{
    for (const uint8_t modem : {uint8_t{0}, uint8_t{1}})
    {
        for (const uint16_t error : {kIrqCrcErr, kIrqHeaderErr, kIrqTimeout,
                                     uint16_t(kIrqCrcErr | kIrqHeaderErr)})
        {
            for (const uint16_t done : {uint16_t{0}, kIrqRxDone})
            {
                Transmitter radio;
                radio.packet_type_ = modem;
                radio.irq_mask = kIrqAll;
                radio.outcome = error | done;
                Sx126xPacket received;
                assert(!radio.pollReceive(&received));
                assert(radio.reads == 0 && radio.rx_packets_ == 0);
                assert(radio.receive_starts == 1);
                assert(radio.rx_crc_errors_ == unsigned((error & kIrqCrcErr) != 0));
                assert(radio.rx_header_errors_ == unsigned((error & kIrqHeaderErr) != 0));
                assert(radio.rx_timeouts_ == unsigned((error & kIrqTimeout) != 0));
                // A rejected frame must not prevent the following good frame.
                radio.outcome = kIrqRxDone;
                assert(radio.pollReceive(&received));
                assert(radio.reads == 1 && radio.rx_packets_ == 1);
                assert(received.size == 2 && received.data[0] == 0x42);
            }
        }
    }
    const uint8_t payload[] = {0x42, 0x23};
    for (const uint8_t modem : {uint8_t{0}, uint8_t{1}})
    {
        Transmitter radio;
        radio.packet_type_ = modem;
        std::array<uint8_t, 256> maximum{};
        assert(radio.transmit(maximum.data(), 255));
        assert(!radio.transmit(maximum.data(), 256));
        assert(radio.tx_packets_ == 1 && radio.receive_starts == 1);
    }
    for (const uint8_t modem : {uint8_t{0}, uint8_t{1}})
    {
        for (unsigned scenario = 0; scenario < 7; ++scenario)
        {
            Transmitter radio;
            radio.packet_type_ = modem;
            if (scenario == 1) radio.outcome = kIrqTimeout;
            if (scenario == 2) radio.fail_start = true;
            if (scenario == 3) radio.fail_write = true;
            if (scenario == 4) radio.fail_receive = true;
            if (scenario == 5) radio.outcome = 0; // Exercise the host wait timeout.
            if (scenario == 6) radio.fail_standby = true;
            assert(radio.transmit(payload, sizeof(payload)) == (scenario == 0));
            assert(radio.tx_packets_ == (scenario == 0 || scenario == 4 ? 1 : 0));
            assert(radio.receive_starts == 1);
            assert((radio.irq_mask & (kIrqRxDone | kIrqCrcErr | kIrqHeaderErr | kIrqTimeout)) ==
                   (kIrqRxDone | kIrqCrcErr | kIrqHeaderErr | kIrqTimeout));
            assert(radio.dio_mask == kIrqRxDone);
            // Model a subsequent peer frame: it must be visible through the
            // restored chip mask, which SetRx by itself does not change.
            radio.outcome = kIrqRxDone;
            assert(radio.getIrqFlagsLocked() == kIrqRxDone);
        }
    }
    std::puts("Production SX126x TX: LoRa/FSK restore RX IRQs after success, chip/host timeout, start/write failure; RX restart failure is reported");
}
