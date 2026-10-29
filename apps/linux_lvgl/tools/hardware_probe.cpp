#include "platform/desktop/device_options.h"
#include "platform/linux/rtc_clock.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "platform/ui/network_runtime.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    bool expect_wifi = false, expect_battery = false, expect_keyboard = false, expect_rtc = false;
    bool request_scan = false;
    bool expect_network = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--expect-wifi") expect_wifi = true;
        else if (argument == "--expect-battery") expect_battery = true;
        else if (argument == "--expect-keyboard") expect_keyboard = true;
        else if (argument == "--expect-rtc") expect_rtc = true;
        else if (argument == "--expect-network") expect_network = true;
        else if (argument == "--scan") request_scan = true;
        else return 2;
    }
    auto battery = platform::ui::device::battery_info();
    if (expect_battery)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!battery.available && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            battery = platform::ui::device::battery_info();
        }
    }
    const bool keyboard = platform::desktop::hardwareKeyboardPresent();
    const bool wireless = platform::ui::wifi::is_supported();
    platform::ui::wifi::Status wifi;
    if (wireless)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        do
        {
            wifi = platform::ui::wifi::status();
            if (wifi.supported) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while (std::chrono::steady_clock::now() < deadline);
    }
    battery = platform::ui::device::battery_info();
    auto rtc = platform::linux_runtime::linux_rtc_snapshot();
    if (expect_rtc)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!rtc.valid && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            rtc = platform::linux_runtime::linux_rtc_snapshot();
        }
    }
    std::printf("rtc_available=%d rtc_valid=%d rtc_system_clock_source=%d\n", rtc.available, rtc.valid, rtc.system_clock_source);
    auto network = platform::ui::network::status();
    const auto network_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expect_network ? 2 : 1);
    while ((!network.supported || (expect_network && !network.connected)) && std::chrono::steady_clock::now() < network_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        network = platform::ui::network::status();
    }
    std::printf("network_supported=%d network_connected=%d\n", network.supported, network.connected);
    std::printf("battery_available=%d battery_level=%d charging=%d external_power=%d keyboard=%d wifi_supported=%d wifi_connected=%d\n",
                battery.available, battery.level, battery.charging, battery.external_power, keyboard, wifi.supported, wifi.connected);
    if (request_scan)
    {
        platform::ui::wifi::ScanResult networks[16];
        std::size_t count = 0;
        const bool scanned = platform::ui::wifi::scan(networks, 16, count);
        std::printf("wifi_scan_ok=%d networks=%zu\n", scanned, count);
        if (!scanned) return 1;
    }
    return (expect_battery && !battery.available) || (expect_keyboard && !keyboard) ||
                   (expect_wifi && (!wireless || !wifi.supported)) || (expect_rtc && !rtc.valid) || (expect_network && !network.connected)
               ? 1
               : 0;
}
