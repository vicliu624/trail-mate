#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>

namespace chat::runtime
{

struct SelfIdentityInput
{
    NodeId node_id = 0;
    const char* configured_long_name = nullptr;
    const char* configured_short_name = nullptr;
    const char* fallback_long_prefix = nullptr;
    const char* fallback_ble_prefix = nullptr;
    bool allow_short_hex_fallback = true;
    const uint8_t* mac_addr = nullptr;
    size_t mac_addr_len = 0;
};

class SelfIdentityProvider
{
  public:
    virtual ~SelfIdentityProvider() = default;
    virtual bool readSelfIdentityInput(SelfIdentityInput* out) const = 0;
};

} // namespace chat::runtime
