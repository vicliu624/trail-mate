#include "platform/esp/arduino_common/storage/sd_operation_scope.h"
#include "platform/esp/arduino_common/storage/sd_transfer_policy.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace platform::esp::arduino_common::storage;
using namespace sys::runtime;
#define CHECK(x)                                \
    do                                          \
    {                                           \
        if (!(x))                               \
        {                                       \
            std::cerr << "failed: " #x << '\n'; \
            std::abort();                       \
        }                                       \
    } while (0)

int main()
{
#if defined(TRAIL_MATE_SDFAT_SHARED_SPI)
    static_assert(ActiveSdTransferPolicy::file_slice_bytes == 512);
#else
    static_assert(ActiveSdTransferPolicy::file_slice_bytes == 16384);
#endif
    auto& profile = SharedSpiSdOperationScope::profile();
    CHECK(!profile.active);
    CHECK(profile.wait_ms == 25);
    SharedSpiSdOperationScope outer;
    outer.enter("read", BusAccessPolicy::BackgroundWorkerBounded, 200);
    CHECK(profile.active && profile.wait_ms == 200);
    profile.last_bus_status = BusAcquireStatus::TimedOut;
    SharedSpiSdOperationScope inner;
    inner.enter("write", BusAccessPolicy::DurableCommit, 250);
    CHECK(profile.policy == BusAccessPolicy::DurableCommit);
    CHECK(inner.status() == BusAcquireStatus::Acquired);
    inner.leave();
    CHECK(outer.status() == BusAcquireStatus::TimedOut);
    CHECK(profile.wait_ms == 200 && std::strcmp(profile.owner, "read") == 0);
    SdmmcSdOperationScope sdmmc;
    sdmmc.enter("sdmmc", BusAccessPolicy::DurableCommit, 500);
    CHECK(sdmmc.status() == BusAcquireStatus::Acquired);
    sdmmc.leave();
    CHECK(outer.status() == BusAcquireStatus::TimedOut);
    outer.leave();
    CHECK(!profile.active && profile.wait_ms == 25);
    std::cout << "policy selection and nested SPI context passed\n";
}
