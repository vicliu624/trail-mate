#include "lr2021_iq.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <string>

struct Fifo
{
    struct Hal
    {
        bool valid = true;
        unsigned calls = 0;
        uint16_t status = 0x0400;
        bool healthy() const { return valid; }
        void spiTransfer(uint8_t* tx, size_t n, uint8_t* rx)
        {
            ++calls;
            assert(n >= 3 && n <= 258 && tx[0] == 0 && tx[1] == 1);
            for (size_t j = 2; j < n; ++j) assert(tx[j] == 0);
            std::fill_n(rx, n, 0x42);
            rx[0] = status >> 8;
            rx[1] = status & 255;
        }
    } hal_;
    std::array<uint8_t, 258> iq_fifo_tx_{}, iq_fifo_rx_{};
#include "fifo.inc"
};
#define SPI_IOC_WR_MAX_SPEED_HZ 1
#define SPI_IOC_RD_MAX_SPEED_HZ 2
#define SPI_IOC_MESSAGE(n) 3
struct spi_ioc_transfer
{
    uintptr_t tx_buf, rx_buf;
    uint32_t len, speed_hz;
    uint8_t bits_per_word;
};
unsigned transfers = 0;
uint32_t last_transfer_speed = 0;
bool transfer_ok = true;
int ioctl(int fd, unsigned command, spi_ioc_transfer* transfer)
{
    assert(fd == 7 && command == 3 && transfer->bits_per_word == 8);
    ++transfers;
    last_transfer_speed = transfer->speed_hz;
    std::memcpy(reinterpret_cast<void*>(transfer->rx_buf), reinterpret_cast<const void*>(transfer->tx_buf), transfer->len);
    return transfer_ok ? static_cast<int>(transfer->len) : -1;
}
unsigned writes = 0, reads = 0;
uint32_t clock_value = 1000000;
bool write_ok = true, read_ok = true, wrong_readback = false;
int ioctl(int fd, unsigned command, uint32_t* value)
{
    assert(fd == 7);
    if (command == SPI_IOC_WR_MAX_SPEED_HZ)
    {
        ++writes;
        if (!write_ok) return -1;
        clock_value = *value;
        return 0;
    }
    assert(command == SPI_IOC_RD_MAX_SPEED_HZ);
    ++reads;
    if (!read_ok) return -1;
    *value = wrong_readback ? 123 : clock_value;
    return 0;
}
struct LinuxRadioLibHal
{
    int spi_ = 7;
    bool iq_mode_ = false;
    struct
    {
        struct
        {
            uint32_t spi_speed_hz = 1000000;
        } bus;
    } hardware_;
    std::string error;
    bool healthy() const { return error.empty(); }
    void fail(const char* message) { error = message; }
    bool setIqTransferMode(bool enabled);
    unsigned last_delay = 0;
    bool waitReady() const { return healthy(); }
    uint32_t spiClockHz() const { return iq_mode_ ? 16000000u : hardware_.bus.spi_speed_hz; }
    void delayMicroseconds(unsigned value) { last_delay = value; }
    void spiTransfer(uint8_t*, size_t, uint8_t*);
};
#include "clock.inc"
#include "transfer.inc"
int main()
{
    Fifo f;
    uint8_t data[256]{};
    assert(f.iqFifoRead(data, sizeof(data)) == 0 && f.hal_.calls == 1);
    for (auto v : data) assert(v == 0x42);
    assert(f.iqFifoRead(nullptr, 1) == LR2021_IQ_ARGUMENT);
    assert(f.iqFifoRead(data, 0) == LR2021_IQ_ARGUMENT);
    assert(f.iqFifoRead(data, 257) == LR2021_IQ_ARGUMENT && f.hal_.calls == 1);
    for (uint16_t status : {uint16_t(0), uint16_t(0xffff), uint16_t(0x0408), uint16_t(0x0430), uint16_t(0x0407)})
    {
        f.hal_.status = status;
        std::fill_n(data, sizeof(data), 0x11);
        assert(f.iqFifoRead(data, sizeof(data)) == LR2021_IQ_DATA);
        for (auto v : data) assert(v == 0x11);
    }
    f.hal_.valid = false;
    assert(f.iqFifoRead(data, 1) == LR2021_IQ_IO);
    LinuxRadioLibHal h;
    uint8_t tx[] = {1, 2, 3}, rx[3]{};
    h.spiTransfer(tx, sizeof(tx), rx);
    assert(last_transfer_speed == 1000000 && h.last_delay == 100 && std::memcmp(tx, rx, sizeof(tx)) == 0);
    assert(h.setIqTransferMode(true) && h.iq_mode_ && clock_value == 16000000);
    h.spiTransfer(tx, sizeof(tx), rx);
    assert(last_transfer_speed == 16000000 && h.last_delay == 1);
    assert(h.setIqTransferMode(false) && !h.iq_mode_ && clock_value == 1000000);
    h.spiTransfer(tx, sizeof(tx), rx);
    assert(last_transfer_speed == 1000000 && h.last_delay == 100);
    write_ok = false;
    assert(!h.setIqTransferMode(true) && h.iq_mode_);
    write_ok = true;
    assert(h.setIqTransferMode(false) && clock_value == 1000000);
    h.error.clear();
    read_ok = false;
    assert(!h.setIqTransferMode(true));
    read_ok = true;
    assert(h.setIqTransferMode(false));
    h.error.clear();
    wrong_readback = true;
    assert(!h.setIqTransferMode(true));
    wrong_readback = false;
    assert(h.setIqTransferMode(false));
    h.spi_ = -1;
    assert(!h.setIqTransferMode(true) && h.setIqTransferMode(false));
    h.spi_ = 7;
    h.error.clear();
    transfer_ok = false;
    h.spiTransfer(tx, sizeof(tx), rx);
    assert(!h.healthy());
    const auto before = transfers;
    h.spiTransfer(tx, sizeof(tx), rx);
    assert(transfers == before);
}
