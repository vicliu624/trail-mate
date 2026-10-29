#pragma once
#include "hostlink/management_envelope.h"
#include "track_sharing/ip_confirmation_request.h"
#include "track_sharing/prepare_request.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// One HostLink worker owns this coordinator. Durable files outlive connection
// owners; a fresh Begin must rebind/revalidate them before any further use.
class HostlinkUploads
{
  public:
    HostlinkUploads(std::filesystem::path sd_root, std::filesystem::path database);
    ~HostlinkUploads();
    static bool supported();
    bool identityBusy() const;
    HostlinkUploads(const HostlinkUploads&) = delete;
    HostlinkUploads& operator=(const HostlinkUploads&) = delete;
    ::hostlink::management::Status process(const ::hostlink::management::EnvelopeView&, uint64_t now,
                                           uint8_t* output, size_t capacity, size_t& written);
    void advance(uint64_t now, bool connected);
    void close(const uint8_t* session);
    ::hostlink::management::Status prepare_install(const uint8_t* session, const track_sharing::PrepareRequest&,
                                                   uint64_t now, const uint8_t actor[32]);
    ::hostlink::management::Status prepare_confirmation(const uint8_t* session, const track_sharing::IpConfirmationRequest&,
                                                        uint64_t now, const uint8_t actor[32]);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
