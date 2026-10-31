#include "platform/linux/packet_radio.h"
#include "platform/shared/radio/fm_iq_stream.h"
#include <chrono>
#include <cstring>

int main()
{
    platform::linux_runtime::PacketRadioHardware hardware;
    hardware.driver = "lr2021";
    hardware.rf_switch_profile = platform::linux_runtime::Lr2021RfSwitchProfile::Cm0_16E8;
    hardware.bus.gpiochip = "/nonexistent-trailmate-test/gpiochip";
    hardware.bus.spi_device = "/nonexistent-trailmate-test/spidev";
    auto driver = platform::linux_runtime::make_lr2021_radio(hardware);
    if (driver->hardwareCandidatePresent()) return 1;
    const auto start = std::chrono::steady_clock::now();
    if (driver->acquire() || driver->isOnline() || driver->stats().online) return 2;
    if (!std::strstr(driver->lastError(), "open gpiochip")) return 3;
    if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(100)) return 4;
    // The real continuous virtual port must reject before touching a missing
    // device or invoking either worker callback when it has no acquired owner.
    if (!driver->supportsOneSegIq()) return 5;
    platform::linux_runtime::OneSegIqRequest request;
    platform::linux_runtime::OneSegIqResult result;
    bool called = false;
    request.context = &called;
    request.cancelled = [](void* context)
    { *static_cast<bool*>(context) = true; return true; };
    if (driver->streamOneSegIq(request, result) || result.capture_error != -2 || called) return 6;
    if (driver->isOnline() || driver->stats().online) return 7;
    auto generic = hardware;
    generic.rf_switch_profile = platform::linux_runtime::Lr2021RfSwitchProfile::None;
    const auto unprofiled = platform::linux_runtime::make_lr2021_radio(generic);
    if (unprofiled->supportsOneSegIq()) return 8;
    if (!driver->supportsFmIq() || unprofiled->supportsFmIq()) return 9;
    platform::linux_runtime::FmIqRequest fm;
    platform::linux_runtime::FmIqResult fm_result;
    fm.context = &called;
    fm.cancelled = request.cancelled;
    if (driver->streamFmIq(fm, fm_result) || fm_result.capture_error != -2 || called) return 10;
    driver->release();
    return 0;
}
