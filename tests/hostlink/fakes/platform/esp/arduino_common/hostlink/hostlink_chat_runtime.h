#pragma once
#include "hostlink/management_envelope.h"
namespace hostlink::chat_runtime
{
inline bool submitting = false;
constexpr bool supported() { return false; }
inline bool busy() { return submitting; }
inline management::Status process(const management::EnvelopeView&, uint16_t, uint8_t*, size_t, size_t&, bool&)
{
    return management::Status::Unsupported;
}
inline void advance(uint8_t*, size_t, bool) {}
} // namespace hostlink::chat_runtime
