#pragma once
#include "hostlink/management_envelope.h"
#include <array>
#include <string>

namespace platform::linux_runtime
{
// A single HostLink worker owns the session and immutable paged snapshot.
class HostlinkConfigSnapshot
{
  public:
    static bool supported();
    bool identityBusy() const { return awaiting_ || owned_; }
    hostlink::management::Status process(const hostlink::management::EnvelopeView&,
                                         uint8_t*, size_t, size_t& written);
    void close(const uint8_t* session = nullptr);

  private:
    bool awaiting_ = false, owned_ = false;
    std::array<uint8_t, 16> session_{};
    std::array<uint8_t, 32> hash_{};
    std::string document_;
};
} // namespace platform::linux_runtime
