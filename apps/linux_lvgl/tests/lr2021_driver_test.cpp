#include "platform/linux/packet_radio.h"
#include <chrono>
#include <cstring>

int main()
{
    platform::linux_runtime::PacketRadioHardware hardware;
    hardware.driver = "lr2021";
    hardware.bus.gpiochip = "/nonexistent-trailmate-test/gpiochip";
    hardware.bus.spi_device = "/nonexistent-trailmate-test/spidev";
    auto driver = platform::linux_runtime::make_lr2021_radio(hardware);
    if (driver->hardwareCandidatePresent()) return 1;
    const auto start = std::chrono::steady_clock::now();
    if (driver->acquire() || driver->isOnline() || driver->stats().online) return 2;
    if (!std::strstr(driver->lastError(), "open gpiochip")) return 3;
    if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(100)) return 4;
    driver->release();
    return 0;
}
