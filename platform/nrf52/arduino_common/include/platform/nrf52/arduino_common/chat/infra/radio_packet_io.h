#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>

namespace platform::nrf52::arduino_common::chat::infra
{

struct RadioPacket
{
    uint8_t data[256] = {};
    size_t size = 0;
    ::chat::RxMeta rx_meta{};
};

class IRadioPacketIo
{
  public:
    virtual ~IRadioPacketIo() = default;
    virtual bool begin() = 0;
    virtual void applyConfig(::chat::MeshProtocol protocol, const ::chat::MeshConfig& config) = 0;
    virtual bool transmit(const uint8_t* data, size_t size) = 0;
    virtual bool pollReceive(RadioPacket* out_packet) = 0;
};

void bindRadioPacketIo(IRadioPacketIo* io);
IRadioPacketIo* radioPacketIo();

} // namespace platform::nrf52::arduino_common::chat::infra
