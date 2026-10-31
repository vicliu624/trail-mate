#include "platform/ui/lora_runtime.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>

using platform::ui::lora::ReceivedPacket;
static constexpr uint32_t kIrqRxDone = 2, kIrqHeaderError = 0x20, kIrqCrcError = 0x40, kIrqTimeout = 0x200;
static constexpr uint32_t kIrqReceiveTerminal = kIrqRxDone | kIrqHeaderError | kIrqCrcError | kIrqTimeout;
struct Radio
{
    bool online = true, receive_ok = true;
    int length = 1, read_result = 0;
    uint32_t irq = kIrqRxDone, cleared = 0;
    unsigned reads = 0, lengths = 0, receives = 0;
    bool isOnline() const { return online; }
    uint32_t getIrqFlags() const { return irq; }
    void clearIrqFlags(uint32_t flags) { cleared = flags; }
    int getPacketLength(bool)
    {
        ++lengths;
        return length;
    }
    int readPacket(uint8_t* bytes, size_t size)
    {
        ++reads;
        assert(size == size_t(length));
        bytes[0] = 0x42;
        return read_result;
    }
    float readRssi() const { return -77; }
    bool startReceive()
    {
        ++receives;
        return receive_ok;
    }
} device;
static Radio& radio() { return device; }
namespace platform::ui::lora
{
#include "p4_probe_rx.inc"
}

int main()
{
    using platform::ui::lora::poll_received_packet;
    uint8_t bytes[8]{};
    ReceivedPacket packet;
    assert(!poll_received_packet(nullptr, sizeof(bytes), &packet));
    assert(!poll_received_packet(bytes, 0, &packet));
    assert(!poll_received_packet(bytes, sizeof(bytes), nullptr));
    device.online = false;
    assert(!poll_received_packet(bytes, sizeof(bytes), &packet));
    assert(device.reads == 0 && device.receives == 0);
    device.online = true;
    for (uint32_t progress : {0u, 4u, 8u, 0x10u, 0x1cu})
    {
        device.irq = progress;
        assert(!poll_received_packet(bytes, sizeof(bytes), &packet));
        assert(device.reads == 0 && device.receives == 0);
    }
    for (uint32_t error : {kIrqCrcError, kIrqHeaderError, kIrqTimeout})
    {
        device.irq = kIrqRxDone | error;
        assert(!poll_received_packet(bytes, sizeof(bytes), &packet));
        assert(device.reads == 0 && device.lengths == 0 && packet.size == 0);
        assert(device.cleared == device.irq);
    }
    device.irq = kIrqRxDone;
    device.length = 9;
    assert(!poll_received_packet(bytes, sizeof(bytes), &packet) && device.reads == 0);
    device.length = 1;
    device.read_result = -1;
    assert(!poll_received_packet(bytes, sizeof(bytes), &packet) && packet.size == 0);
    device.read_result = 0;
    device.receive_ok = false;
    assert(!poll_received_packet(bytes, sizeof(bytes), &packet) && packet.size == 0);
    device.receive_ok = true;
    assert(poll_received_packet(bytes, sizeof(bytes), &packet));
    assert(packet.size == 1 && packet.rssi_dbm == -77 && std::isnan(packet.snr_db) && bytes[0] == 0x42);
}
