#pragma once

#include "hostlink/management_envelope.h"
#include <cstdint>

namespace hostlink::download_mode
{
#if !defined(ARDUINO)
// Other adapters share management framing but have separate storage owners
// and ROM transition ports. They must not advertise the Arduino adapter.
inline bool supported() { return false; }
inline bool active() { return false; }
inline management::Status request(const management::EnvelopeView&, uint16_t) { return management::Status::Unsupported; }
inline void service() {}
inline void reply_completed(uint32_t, bool) {}
#else
bool supported();
bool active();
management::Status request(const management::EnvelopeView& request, uint16_t sequence);
void service();
void reply_completed(uint32_t cookie, bool delivered);
#endif
} // namespace hostlink::download_mode
