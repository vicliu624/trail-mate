#pragma once
#include "hostlink/management_envelope.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
class HostlinkGeocachingDrafts
{
  public:
    explicit HostlinkGeocachingDrafts(std::filesystem::path database);
    ~HostlinkGeocachingDrafts();
    static bool supported();
    bool routes(const ::hostlink::management::EnvelopeView&);
    bool identityBusy();
    void close(const uint8_t* session = nullptr);
    void advance(uint64_t now, bool connected);
    ::hostlink::management::Status process(const ::hostlink::management::EnvelopeView&, uint64_t now, uint8_t* output, size_t capacity, size_t& written);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
