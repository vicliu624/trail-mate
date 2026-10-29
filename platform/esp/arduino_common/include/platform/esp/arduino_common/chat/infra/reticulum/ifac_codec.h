#pragma once

#include "chat/infra/reticulum/interface_access_codec.h"
#include <cstddef>
#include <cstdint>

namespace chat::reticulum
{
// Interface-local access state. Own in PSRAM; packet storage belongs to caller.
// Calls are serialized by the interface owner. Never copy between interfaces.
class IfacCodec
{
  public:
    static constexpr size_t kMaxTagSize = 64;
    static constexpr size_t kMaxPacketSize = 500;
    static constexpr size_t kMaxCredentialBytes = 128;
    IfacCodec() = default;
    ~IfacCodec() { clear(); }
    IfacCodec(const IfacCodec&) = delete;
    IfacCodec& operator=(const IfacCodec&) = delete;

    // tag_bytes is bytes, unlike the RNS configuration's ifac_size in bits.
    // Empty credentials explicitly select public mode; any error fails closed.
    bool configure(const char* network_name, const char* passphrase, size_t tag_bytes);
    bool encode(uint8_t* frame, size_t& size, size_t capacity);
    bool decode(uint8_t* frame, size_t& size, size_t capacity);
    void clear();

  private:
    InterfaceAccessCodec codec_;
};
static_assert(sizeof(IfacCodec) <= 640);
} // namespace chat::reticulum
