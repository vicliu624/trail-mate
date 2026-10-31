#include "platform/linux/packet_radio.h"
#include <RadioLib.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <mutex>

using namespace platform::linux_runtime;
struct Receiver
{
    std::mutex mutex_;
    bool online_ = true, fsk_ = false;
    struct
    {
        bool valid = true;
        unsigned resets = 0;
        const unsigned GpioLevelLow = 0;
        bool healthy() const { return valid; }
        void digitalWrite(uint32_t, unsigned level)
        {
            assert(level == 0);
            ++resets;
        }
    } hal_;
    struct
    {
        uint32_t irq = 0;
        size_t size = 8;
        unsigned reads = 0, receives = 0, standbys = 0;
        int receive_error = 0, read_error = 0;
        uint32_t getIrqFlags() { return irq; }
        int startReceive()
        {
            ++receives;
            return receive_error;
        }
        int standby()
        {
            ++standbys;
            return 0;
        }
        size_t getPacketLength() { return size; }
        float getRSSI() { return -60; }
        float getSNR() { return 9; }
        int readData(uint8_t* output, size_t bytes)
        {
            ++reads;
            std::fill(output, output + bytes, 0x42);
            return read_error;
        }
    } radio_;
    RadioStatistics statistics_;
    RadioSettings settings_;
    FskSettings fsk_settings_;
    PacketRadioHardware hardware_;
    bool check(int code, const char*)
    {
        if (!code && hal_.healthy()) return true;
        online_ = false;
        return false;
    }
#include "linux_lr2021_rx.inc"
};
int main()
{
    const uint32_t invalid[] = {RADIOLIB_LR2021_IRQ_CRC_ERROR, RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR,
                                RADIOLIB_LR2021_IRQ_LEN_ERROR, RADIOLIB_LR2021_IRQ_ADDR_ERROR};
    for (const auto flag : invalid)
    {
        for (const auto done : {uint32_t{0}, uint32_t{RADIOLIB_LR2021_IRQ_RX_DONE}})
        {
            Receiver receiver;
            receiver.radio_.irq = flag | done;
            RadioPacket packet;
            packet.data.fill(0xa5);
            assert(!receiver.pollReceive(&packet));
            assert(packet.size == 0 && packet.data.front() == 0xa5);
            assert(!receiver.radio_.reads && receiver.radio_.receives == 1 && receiver.online_);
            assert(receiver.statistics_.rx_packets == 0);
            if (flag == RADIOLIB_LR2021_IRQ_CRC_ERROR) assert(receiver.statistics_.rx_crc_errors == 1);
            if (flag == RADIOLIB_LR2021_IRQ_LEN_ERROR) assert(receiver.statistics_.rx_invalid_lengths == 1);
            if (flag == RADIOLIB_LR2021_IRQ_ADDR_ERROR || flag == RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR)
                assert(receiver.statistics_.rx_header_errors == 1);
        }
    }
    for (const auto flag : {RADIOLIB_LR2021_IRQ_ERROR, RADIOLIB_LR2021_IRQ_CMD_ERROR, RADIOLIB_LR2021_IRQ_PA_OCP_OVP})
    {
        Receiver receiver;
        receiver.radio_.irq = flag | RADIOLIB_LR2021_IRQ_RX_DONE;
        RadioPacket packet;
        assert(!receiver.pollReceive(&packet));
        assert(!receiver.online_ && !receiver.radio_.reads && !receiver.radio_.receives);
        assert(receiver.radio_.standbys == 1 && receiver.hal_.resets == 1);
        assert(receiver.statistics_.last_irq_flags == receiver.radio_.irq);
    }
    {
        Receiver receiver;
        receiver.radio_.irq = RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID |
                              RADIOLIB_LR2021_IRQ_RX_FIFO | RADIOLIB_LR2021_IRQ_TX_FIFO | RADIOLIB_LR2021_IRQ_TX_DONE;
        RadioPacket packet;
        assert(!receiver.pollReceive(&packet));
        assert(!receiver.radio_.reads && !receiver.radio_.receives && receiver.online_);
        receiver.radio_.irq = RADIOLIB_LR2021_IRQ_TIMEOUT;
        assert(!receiver.pollReceive(&packet));
        assert(receiver.statistics_.rx_timeouts == 1 && receiver.radio_.receives == 1);
    }
    {
        Receiver receiver;
        receiver.radio_.irq = RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_LEN_ERROR;
        receiver.radio_.receive_error = -42;
        RadioPacket packet;
        assert(!receiver.pollReceive(&packet) && !receiver.online_);
    }
    {
        Receiver receiver;
        receiver.radio_.irq = RADIOLIB_LR2021_IRQ_RX_DONE;
        RadioPacket packet;
        assert(receiver.pollReceive(&packet));
        assert(packet.size == 8 && packet.data[0] == 0x42 && packet.rssi_dbm == -60 && packet.snr_db == 9);
        assert(receiver.statistics_.rx_packets == 1 && receiver.radio_.receives == 1);
    }
    std::puts("Linux LR2021 production RX: damaged-frame rejection with/without RX_DONE, fatal reset/offline, progress preservation and valid packet acceptance passed");
}
