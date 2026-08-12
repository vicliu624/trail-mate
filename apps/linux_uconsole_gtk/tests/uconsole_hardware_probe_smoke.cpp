#include "platform/ui/settings_store.h"
#include "uconsole/uconsole_hardware_probe.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{

void setEnv(const char* name, const char* value)
{
    setenv(name, value, 1);
}

void clearEnv(const char* name)
{
    unsetenv(name);
}

const char* envValue(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      "trailmate_uconsole_hardware_probe_smoke";
    std::error_code error{};
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "settings", error);
    assert(!error);

    setEnv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str());
    ::platform::ui::settings_store::clear_namespace("uconsole_gps");
    clearEnv("TRAIL_MATE_GPS_DEVICE");
    clearEnv("TRAIL_MATE_GPS_BAUD");
    clearEnv("TRAIL_MATE_GPS_NMEA_FILE");
    clearEnv("TRAIL_MATE_GPS_VALID");
    clearEnv("TRAIL_MATE_GPS_LAT");
    clearEnv("TRAIL_MATE_GPS_LNG");

    assert(trailmate::uconsole::isAllowedUConsoleGpsDevicePath(""));
    assert(trailmate::uconsole::isAllowedUConsoleGpsDevicePath("/dev/ttyS0"));
    assert(!trailmate::uconsole::isAllowedUConsoleGpsDevicePath(
        "/dev/ttyACM0"));

    const auto blank = trailmate::uconsole::loadUConsoleGpsSourceSettings();
    assert(!blank.is_configured);
    assert(blank.device_path.empty());
    assert(blank.baud == 9600);

    trailmate::uconsole::saveUConsoleGpsSourceSettings(
        {.is_configured = false, .device_path = "", .baud = 38400});
    const auto reset = trailmate::uconsole::loadUConsoleGpsSourceSettings();
    assert(!reset.is_configured);
    assert(reset.device_path.empty());
    assert(reset.baud == 9600);

    trailmate::uconsole::applyUConsoleGpsSourceSettings(blank);
    const bool default_available =
        std::filesystem::exists("/dev/ttyS0") &&
        std::filesystem::exists("/dev/gpiochip0");
    assert(trailmate::uconsole::isUsingUConsoleAio2DefaultGps() ==
           default_available);
    if (default_available)
    {
        assert(std::string(envValue("TRAIL_MATE_GPS_DEVICE")) ==
               "/dev/ttyS0");
        assert(std::string(envValue("TRAIL_MATE_GPS_BAUD")) == "9600");
    }

    setEnv("TRAIL_MATE_GPS_NMEA_FILE", "/tmp/fix.nmea");
    trailmate::uconsole::applyUConsoleGpsSourceSettings(blank);
    assert(!trailmate::uconsole::isUsingUConsoleAio2DefaultGps());
    assert(std::string(envValue("TRAIL_MATE_GPS_DEVICE")).empty());

    const trailmate::uconsole::UConsoleGpsSourceSettings custom{
        .is_configured = true, .device_path = "/dev/ttyAMA2", .baud = 38400};
    trailmate::uconsole::applyUConsoleGpsSourceSettings(custom);
    assert(std::string(envValue("TRAIL_MATE_GPS_DEVICE")).empty());

    clearEnv("TRAIL_MATE_GPS_NMEA_FILE");
    setEnv("TRAIL_MATE_GPS_DEVICE", "/dev/ttyAMA3");
    trailmate::uconsole::applyUConsoleGpsSourceSettings(custom);
    assert(std::string(envValue("TRAIL_MATE_GPS_DEVICE")) == "/dev/ttyAMA3");

    clearEnv("TRAIL_MATE_GPS_DEVICE");
    trailmate::uconsole::applyUConsoleGpsSourceSettings(custom);
    assert(std::string(envValue("TRAIL_MATE_GPS_DEVICE")) == "/dev/ttyAMA2");
    assert(std::string(envValue("TRAIL_MATE_GPS_BAUD")) == "38400");

    std::filesystem::remove_all(root, error);
    return 0;
}
