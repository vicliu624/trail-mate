#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"

namespace platform::nrf52::arduino_common::chat::infra
{
namespace
{
IRadioPacketIo* g_radio_packet_io = nullptr;
}

void bindRadioPacketIo(IRadioPacketIo* io)
{
    g_radio_packet_io = io;
}

IRadioPacketIo* radioPacketIo()
{
    return g_radio_packet_io;
}

} // namespace platform::nrf52::arduino_common::chat::infra
