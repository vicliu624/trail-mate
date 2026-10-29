#include "platform/linux/gnss_source.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
namespace
{
struct PowerLine final : platform::linux_runtime::GpioPowerLine
{
    std::atomic<unsigned> enables{0}, disables{0};
    bool enable(const platform::linux_runtime::GpioPowerOptions&, int& error) override
    {
        ++enables;
        error = 0;
        return true;
    }
    bool disable(int& error) override
    {
        ++disables;
        error = 0;
        return true;
    }
};
void require(bool value, const char* text)
{
    if (!value) throw std::runtime_error(text);
}
void tick()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    platform::ui::gps::tick_service();
}
std::string command(int master)
{
    std::string result;
    for (unsigned retry = 0; retry < 12; ++retry)
    {
        char bytes[256];
        const auto count = read(master, bytes, sizeof(bytes));
        if (count > 0) result.append(bytes, count);
        if (result.find("\r\n") != result.npos) return result;
        tick();
    }
    return result;
}
void send(int master, const char* text)
{
    const std::string value(text);
    require(write(master, value.data(), value.size()) == static_cast<ssize_t>(value.size()), "test serial write failed");
}
void bootstrap(int master)
{
    const char* expected[] = {"AT\r\n", "AT#XGPS=0\r\n", "AT+CFUN=0\r\n", "AT%XSYSTEMMODE=0,0,1,0\r\n", "AT+CFUN=31\r\n", "AT#XGPS=1,0,1\r\n"};
    for (const auto* value : expected)
    {
        require(command(master) == value, "unexpected AT command/order");
        if (std::string(value) == "AT\r\n")
        {
            const auto noise = std::string(200, 'x') + "OK\r\n";
            send(master, noise.c_str());
            tick();
            char byte;
            require(read(master, &byte, 1) <= 0, "overlong response was mistaken for an acknowledgement");
        }
        send(master, "\r\nOK\r\n");
        tick();
    }
}
} // namespace
int main(int argc, char** argv)
{
    require(argc == 2, "protocol argument missing");
    char temporary[] = "/tmp/trailmate-gnss-test-XXXXXX";
    require(mkdtemp(temporary), "temporary root failed");
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", temporary, 1);
    const int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    require(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0, "PTY setup failed");
    using namespace platform::linux_runtime;
    const auto stable = std::filesystem::path(temporary) / "serial-by-id";
    std::filesystem::create_directory(stable);
    const auto selected = stable / "usb-test-receiver-if00";
    const auto pattern = (stable / "usb-test-*-if00").string();
    int selection_error = 0;
    require(resolve_gnss_serial_device(pattern, selection_error).empty() && selection_error == ENOENT, "missing serial selector accepted");
    std::filesystem::create_symlink(ptsname(master), selected);
    require(resolve_gnss_serial_device(pattern, selection_error) == selected.string() && !selection_error, "stable serial selector failed");
    const auto second = stable / "usb-test-other-if00";
    std::filesystem::create_symlink("/dev/null", second);
    require(resolve_gnss_serial_device(pattern, selection_error).empty() && selection_error == ENOTUNIQ, "ambiguous serial selector chose a device");
    std::filesystem::remove(second);
    std::filesystem::create_symlink("/dev/null", stable / "usb-test-receiver-if02");
    require(resolve_gnss_serial_device(pattern, selection_error) == selected.string(), "wrong USB interface selected");
    GnssSourceOptions options;
    const bool powered = std::string(argv[1]) == "nordic_slm_power";
    auto power = powered ? std::make_shared<PowerLine>() : nullptr;
    options.protocol = powered ? "nordic_slm" : argv[1];
    options.device = pattern;
    options.baud = 115200;
    options.gnss_only_setup = options.modem_lines = options.protocol == "nordic_slm";
    if (powered)
    {
        options.power = {"/dev/test-power", 3, false, 100};
        options.device = std::string(temporary) + "/receiver";
    }
    require(configure_gnss_source(options, power), "GNSS composition failed");
    platform::ui::gps::set_enabled(true);
    platform::ui::gps::tick_service();
    if (powered)
    {
        require(!platform::ui::gps::is_powered(), "startup delay advertised ready power");
        char byte;
        require(read(master, &byte, 1) <= 0, "AT was sent before power startup");
        tick();
        tick();
        require(power->enables == 1, "serial-open retry cycled modem power");
        std::filesystem::create_symlink(ptsname(master), options.device);
    }
    if (options.protocol == "nordic_slm")
    {
        require(!platform::ui::device::gps_ready(), "unresponsive modem advertised ready");
        require(!platform::ui::gps::diagnostics().ready, "diagnostics advertised an uninitialized modem ready");
        bootstrap(master);
        require(platform::ui::device::gps_ready(), "acknowledged GNSS not ready");
        send(master, "#XGPS: 35.457576,139.625090,121.473785,22.199919,0.442868,69.0,\"2021-06-02 06:25:48\"\r\n");
        tick();
        const auto fix = platform::ui::gps::get_data();
        require(fix.valid && std::abs(fix.lat - 35.457576) < 1e-8 && std::abs(fix.speed_mps - 0.442868) < 1e-8, "SLM fix not projected");
        require(fix.satellites == 0, "satellite count fabricated");
        const auto diagnostics = platform::ui::gps::diagnostics();
        require(diagnostics.ready && diagnostics.has_fix && diagnostics.code == ::gps::GpsDiagnosticCode::OK,
                "GNSS diagnostics did not reflect the accepted fix");
        send(master, "#XGPS:1,0\r\n");
        tick();
        require(!platform::ui::device::gps_ready() && !platform::ui::gps::get_data().valid,
                "modem stop indication retained readiness/fix");
        platform::ui::gps::set_enabled(false);
        require(command(master) == "AT#XGPS=0\r\n", "disable did not request stop");
        require(!platform::ui::device::gps_ready() && !platform::ui::gps::get_data().valid, "disable retained readiness/fix");
        platform::ui::gps::set_enabled(true);
        if (powered)
        {
            platform::ui::gps::set_enabled(false);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!power->disables && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(power->disables == 1, "disable did not release power");
            platform::ui::gps::set_enabled(true);
        }
        tick();
        bootstrap(master);
        require(platform::ui::device::gps_ready(), "GNSS restart failed");
        if (powered)
        {
            require(power->enables == 2, "GNSS restart did not reacquire power");
            platform::ui::gps::suspend_runtime();
            require(!platform::ui::gps::is_powered() && !platform::ui::device::gps_ready(), "suspend retained readiness");
        }
    }
    else
    {
        char byte;
        require(read(master, &byte, 1) <= 0, "passive NMEA transmitted commands");
        send(master, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");
        tick();
        const auto fix = platform::ui::gps::get_data();
        require(fix.valid && std::abs(fix.lat - 48.1173) < 1e-6, "legacy NMEA regression");
    }
    platform::ui::gps::set_enabled(false);
    close(master);
    std::filesystem::remove_all(temporary);
}
