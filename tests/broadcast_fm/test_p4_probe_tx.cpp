#include <cassert>
#include <cstddef>
#include <cstdint>

static constexpr uint32_t kIrqTimeout = 0x0200;
static uint32_t now = 0;
namespace sys
{
uint32_t millis_now() { return now; }
} // namespace sys
#define pdMS_TO_TICKS(value) (value)
static void vTaskDelay(uint32_t ticks)
{
    now += ticks;
}
struct Radio
{
    bool online = true, receive_ok = true;
    int transmit_result = 0;
    uint32_t irq = 1, cleared = 0;
    unsigned transmissions = 0, receives = 0;
    bool isOnline() const { return online; }
    int startTransmit(const uint8_t*, size_t)
    {
        ++transmissions;
        return transmit_result;
    }
    uint32_t getIrqFlags() const { return irq; }
    void clearIrqFlags(uint32_t flags) { cleared = flags; }
    bool startReceive()
    {
        ++receives;
        return receive_ok;
    }
} device;
static Radio& radio() { return device; }
#include "p4_probe_tx.inc"

int main()
{
    const uint8_t payload = 0x42;
    assert(!transmit_packet(nullptr, 1) && !transmit_packet(&payload, 0));
    assert(device.transmissions == 0 && device.receives == 0);
    device.online = false;
    assert(!transmit_packet(&payload, 1) && device.transmissions == 0);
    device.online = true;
    device.transmit_result = -1;
    assert(!transmit_packet(&payload, 1) && device.receives == 0);
    device.transmit_result = 0;
    assert(transmit_packet(&payload, 1) && device.receives == 1 && device.cleared == 1);
    device.receive_ok = false;
    assert(!transmit_packet(&payload, 1) && device.receives == 2);
    device.receive_ok = true;
    device.irq = kIrqTimeout;
    assert(!transmit_packet(&payload, 1) && device.receives == 3 && device.cleared == kIrqTimeout);
    device.irq = 0;
    now = UINT32_MAX - 2500;
    assert(!transmit_packet(&payload, 1) && device.receives == 4);
    assert(now == uint32_t(UINT32_MAX - 2500 + 5000u));
    assert(device.cleared == (1 | kIrqTimeout));
}
