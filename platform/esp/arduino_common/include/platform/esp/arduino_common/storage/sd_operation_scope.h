#pragma once

#include "sys/shared_spi_access.h"

namespace platform::esp::arduino_common::storage
{
// These scopes contain transport context only. The enclosing runtime owns the
// filesystem mutex and must enter/leave the scope while holding that mutex.
struct SdSpiOperationProfile
{
    sys::runtime::BusAccessPolicy policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    uint32_t wait_ms = 25U;
    const char* owner = "sd_spi_unscoped";
    sys::runtime::BusAcquireStatus last_bus_status = sys::runtime::BusAcquireStatus::Unavailable;
    bool active = false;
};

class SharedSpiSdOperationScope
{
  public:
    void enter(const char* owner, sys::runtime::BusAccessPolicy policy, uint32_t wait_ms)
    {
        previous_ = profile();
        auto& current = profile();
        current.policy = policy;
        current.wait_ms = wait_ms;
        current.owner = owner != nullptr && owner[0] != '\0' ? owner : "sd_runtime";
        current.last_bus_status = sys::runtime::BusAcquireStatus::Acquired;
        current.active = true;
    }
    void leave() { profile() = previous_; }
    sys::runtime::BusAcquireStatus status() const { return profile().last_bus_status; }
    static SdSpiOperationProfile& profile()
    {
        static SdSpiOperationProfile current;
        return current;
    }

  private:
    SdSpiOperationProfile previous_{};
};

class SdmmcSdOperationScope
{
  public:
    void enter(const char*, sys::runtime::BusAccessPolicy, uint32_t) {}
    void leave() {}
    // No shared-bus acquisition can fail. SDMMC I/O errors are reported by
    // the block device/file operation, never disguised as SPI contention.
    sys::runtime::BusAcquireStatus status() const { return sys::runtime::BusAcquireStatus::Acquired; }
};

#if defined(TRAIL_MATE_SDFAT_SDMMC)
using ActiveSdOperationScope = SdmmcSdOperationScope;
#else
using ActiveSdOperationScope = SharedSpiSdOperationScope;
#endif
} // namespace platform::esp::arduino_common::storage
