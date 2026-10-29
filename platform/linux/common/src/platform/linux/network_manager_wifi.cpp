#include "platform/linux/network_manager_wifi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace platform::linux_runtime
{
std::vector<std::string> split_nmcli_record(const std::string& record)
{
    std::vector<std::string> fields(1);
    bool escaped = false;
    for (const char character : record)
    {
        if (escaped)
        {
            fields.back() += character;
            escaped = false;
        }
        else if (character == '\\') escaped = true;
        else if (character == ':') fields.emplace_back();
        else if (character != '\r') fields.back() += character;
    }
    if (escaped) fields.back() += '\\';
    return fields;
}

void parse_nmcli_device(const std::string& output, platform::ui::wifi::Status& status)
{
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line))
    {
        const auto fields = split_nmcli_record(line);
        if (fields.size() < 2) continue;
        if (fields[0] == "GENERAL.STATE")
        {
            const int state = std::atoi(fields[1].c_str());
            status.supported = state >= 20;
            status.enabled = state >= 30;
            status.connected = state == 100;
            status.state = state == 100 ? platform::ui::wifi::ConnectionState::Connected : state >= 40 && state <= 90 ? platform::ui::wifi::ConnectionState::Connecting
                                                                                       : state >= 30                  ? platform::ui::wifi::ConnectionState::Idle
                                                                                                                      : platform::ui::wifi::ConnectionState::Disabled;
        }
        if (fields[0] == "GENERAL.CONNECTION" && fields[1] != "--")
            std::snprintf(status.ssid, sizeof(status.ssid), "%s", fields[1].c_str());
        if (fields[0].rfind("IP4.ADDRESS", 0) == 0)
            std::snprintf(status.ip, sizeof(status.ip), "%s", fields[1].substr(0, fields[1].find('/')).c_str());
    }
}

std::vector<platform::ui::wifi::ScanResult> parse_nmcli_access_points(
    const std::string& output, platform::ui::wifi::Status* status)
{
    std::vector<platform::ui::wifi::ScanResult> results;
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line))
    {
        const auto fields = split_nmcli_record(line);
        if (fields.size() != 4 || fields[1].empty() || fields[1] == "--") continue;
        const int signal = std::clamp(std::atoi(fields[2].c_str()), 0, 100);
        platform::ui::wifi::ScanResult access_point;
        std::snprintf(access_point.ssid, sizeof(access_point.ssid), "%s", fields[1].c_str());
        // NM exposes quality, not measured RSSI. Project it approximately onto
        // the UI's -100..-50 dBm scale for signal strength bars.
        access_point.rssi = signal / 2 - 100;
        access_point.requires_password = !fields[3].empty() && fields[3] != "--";
        if (status && fields[0] == "*")
        {
            std::snprintf(status->ssid, sizeof(status->ssid), "%s", access_point.ssid);
            status->rssi = access_point.rssi;
        }
        auto previous = std::find_if(results.begin(), results.end(), [&](const auto& item)
                                     { return fields[1] == item.ssid; });
        if (previous == results.end()) results.push_back(access_point);
        else if (access_point.rssi > previous->rssi) *previous = access_point;
    }
    return results;
}

std::string select_wireless_interface(const std::filesystem::path& root, const std::string& requested)
{
    // Linux interface names are at most IFNAMSIZ-1 bytes. Treat the selector as
    // one class entry, never as an arbitrary path or a shell fragment.
    if (!root.is_absolute() || requested.size() > 15 || requested == "." || requested == ".." ||
        requested.find_first_of("/\r\n\0", 0, 4) != std::string::npos) return {};
    std::error_code error;
    if (!requested.empty())
    {
        return std::filesystem::is_directory(root / requested / "wireless", error) && !error ? requested : std::string{};
    }
    std::string selected;
    unsigned inspected = 0;
    for (std::filesystem::directory_iterator entry(root, error), end; !error && entry != end; entry.increment(error))
    {
        if (++inspected > 256) return {};
        const auto marker = entry->path() / "wireless";
        const bool present = std::filesystem::exists(marker, error);
        if (error) return {};
        if (!present) continue;
        const bool wireless = std::filesystem::is_directory(marker, error);
        if (error) return {};
        if (!wireless) continue;
        if (!selected.empty()) return {}; // Never choose the first of several.
        selected = entry->path().filename().string();
    }
    return error ? std::string{} : selected;
}

std::string discover_wireless_interface()
{
#if defined(__linux__)
    const char* selection = std::getenv("TRAIL_MATE_WIFI_INTERFACE");
    return select_wireless_interface("/sys/class/net", selection ? selection : "");
#else
    return {};
#endif
}
} // namespace platform::linux_runtime
