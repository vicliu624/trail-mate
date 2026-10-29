#pragma once
#include "hostlink/management_envelope.h"

namespace platform::linux_runtime
{
// Worker-owned configuration transfer protocol. SQLite owns resumable bytes
// and the atomic commit receipt; no frontend pointer crosses this boundary.
class HostlinkConfigTransfer
{
  public:
    static bool supported();
    bool identityBusy() const;
    bool routes(const hostlink::management::EnvelopeView&) const;
    hostlink::management::Status process(const hostlink::management::EnvelopeView&,
                                         uint8_t*, size_t, size_t& written);
};
} // namespace platform::linux_runtime
