#include "platform/esp/idf_common/lr2021_irq_mapping.h"
#include "platform/shared/radio/board_radio_lease.h"
#include <cassert>
#include <cstdio>

namespace irq = platform::esp::idf_common::lr2021_irq;
struct FakeRadio
{
    int references = 0, acquisitions = 0, releases = 0;
    bool online = false, allow_acquire = true;
    uint32_t* clock = nullptr;
    bool acquire()
    {
        ++acquisitions;
        if (!allow_acquire)
        {
            if (clock) *clock += 2000;
            return false;
        }
        ++references;
        online = true;
        return true;
    }
    void release()
    {
        assert(references > 0);
        --references;
        ++releases;
    }
    bool isOnline() const { return online; }
};
int main()
{
    assert(irq::logical(RADIOLIB_LR2021_IRQ_TX_DONE) == 0x0001);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_RX_DONE) == 0x0002);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_TIMEOUT) == 0x0200);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR) == 0x0020);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_CRC_ERROR) == 0x0040);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_RX_FIFO) == 0);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_TX_FIFO) == 0);
    const uint32_t progress = irq::logical(RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);
    assert(progress == 0x001c && !(progress & 0x0263));
    assert(irq::physical(0x0001) == RADIOLIB_LR2021_IRQ_TX_DONE);
    assert(irq::physical(0x0002) == RADIOLIB_LR2021_IRQ_RX_DONE);
    assert(irq::physical(0x0200) == RADIOLIB_LR2021_IRQ_TIMEOUT);
    assert(irq::physical(0xffff) == RADIOLIB_LR2021_IRQ_ALL);
    const uint32_t received = RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_CRC_ERROR;
    assert((irq::physical(irq::logical(received)) & received) == received);
    assert(irq::logical(RADIOLIB_LR2021_IRQ_ERROR | RADIOLIB_LR2021_IRQ_CMD_ERROR) == 0);
    platform::shared::radio::BoardRadioLease lease;
    uint32_t now = 0xfffff000u;
    FakeRadio radio;
    radio.clock = &now;
    const auto clock = [&]
    { return now; };
    assert(lease.ensure(radio, clock));
    assert(lease.ensure(radio, clock));
    assert(radio.references == 1 && radio.acquisitions == 1);
    // A foreground reference survives board recovery. A slow failed acquire
    // starts its retry delay after the attempt and across clock wraparound.
    ++radio.references;
    radio.online = false;
    radio.allow_acquire = false;
    now = 0xfffff830u;
    assert(!lease.ensure(radio, clock));
    assert(radio.references == 1 && radio.releases == 1);
    const int attempts = radio.acquisitions;
    assert(!lease.ensure(radio, clock));
    now += 999;
    assert(!lease.ensure(radio, clock) && radio.acquisitions == attempts);
    ++now;
    radio.allow_acquire = true;
    assert(lease.ensure(radio, clock));
    assert(radio.references == 2 && radio.releases == 1);
    std::puts("LR2021 physical/logical IRQ mapping: RX/TX, progress, errors and clear masks verified");
    std::puts("Board radio lease: single reference, offline recovery and wrap-safe delayed retry verified");
}
