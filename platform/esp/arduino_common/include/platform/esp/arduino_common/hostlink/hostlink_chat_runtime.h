#pragma once
#include "hostlink/management_envelope.h"

namespace hostlink::chat_runtime
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
constexpr bool supported()
{
    return true;
}
management::Status process(const management::EnvelopeView& request, uint16_t sequence,
                           uint8_t* body, size_t capacity, size_t& written, bool& deferred);
void advance(uint8_t* scratch, size_t capacity, bool connected);
bool busy();
#else
constexpr bool supported()
{
    return false;
}
inline management::Status process(const management::EnvelopeView&, uint16_t,
                                  uint8_t*, size_t, size_t&, bool&) { return management::Status::Unsupported; }
inline void advance(uint8_t*, size_t, bool) {}
inline bool busy() { return false; }
#endif
} // namespace hostlink::chat_runtime
