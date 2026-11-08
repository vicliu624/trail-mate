#include "platform/linux/cardputer_cap.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>

namespace platform::linux_runtime
{
namespace
{
bool writePower(const std::string& path, int value)
{
    std::ofstream output(path);
    output << value << '\n';
    output.close();
    return !output.fail();
}
} // namespace

CardputerCap::CardputerCap(std::string power_path, uint32_t settle_ms)
    : power_path_(std::move(power_path)), settle_ms_(settle_ms)
{
}

CardputerCap::~CardputerCap()
{
    if (restore_power_ && !writePower(power_path_, original_power_))
        std::fprintf(stderr, "[Cap] Failed to restore EXT5V power state\n");
}

void CardputerCap::begin()
{
    if (active_) return;
    std::error_code error;
    // This OS interface also identifies the physical Cardputer environment.
    // Desktop screenshot tools and simulator sessions do not manage its power.
    if (!std::filesystem::exists(power_path_, error) || error) return;
    active_ = true;
    std::ifstream input(power_path_);
    if (!(input >> original_power_))
    {
        power_failed_ = true;
        return;
    }
    if (original_power_ != 0) return;
    if (!writePower(power_path_, 1))
    {
        power_failed_ = true;
        return;
    }
    restore_power_ = true;
    std::ifstream readback(power_path_);
    int power = 0;
    power_failed_ = !(readback >> power) || power == 0;
    if (!power_failed_) std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms_));
    std::fprintf(stderr, "[Cap] EXT5V %s\n", power_failed_ ? "enable failed" : "enabled");
}

const char* CardputerCap::takeNotice(uint64_t elapsed_ms, const CardputerCapObservation& observation)
{
    if (!active_ || decided_) return nullptr;
    if (power_failed_)
    {
        decided_ = true;
        return "Cap power unavailable. Check device permissions.";
    }
    if (observation.radio_online || observation.gps_chars != 0)
    {
        decided_ = true;
        std::fprintf(stderr, "[Cap] Detected by %s\n", observation.radio_online ? "LoRa response" : "GPS traffic");
        return nullptr;
    }
    if (elapsed_ms < 8000) return nullptr;
    // A user's disabled services are not evidence of a missing module.
    if (!observation.radio_enabled || !observation.gps_enabled)
    {
        decided_ = true;
        return nullptr;
    }
    decided_ = true;
    std::fprintf(stderr, "[Cap] No module response: %s\n", observation.radio_error.c_str());
    if (observation.radio_error.find("Permission denied") != std::string::npos ||
        observation.radio_error.find("Device or resource busy") != std::string::npos)
        return "Cap access unavailable. Check permissions or other apps.";
    // All-zero SPI readback (or no SPI endpoint) plus no UART traffic cannot
    // identify a connected module. Do not claim that the OS device nodes prove it.
    if (!observation.radio_endpoint_present || observation.radio_error.find("status=00") != std::string::npos ||
        observation.radio_error.find("No such file") != std::string::npos)
        return "Cap not detected. Local apps remain available.";
    return "Cap not responding. Check module connection and power.";
}

} // namespace platform::linux_runtime
