#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/ui/wifi_runtime.h"

namespace platform::linux_runtime
{
// NetworkManager's escaped terse records. SSIDs may contain ':' and '\\'.
std::vector<std::string> split_nmcli_record(const std::string& record);
void parse_nmcli_device(const std::string& output, platform::ui::wifi::Status& status);
std::vector<platform::ui::wifi::ScanResult> parse_nmcli_access_points(
    const std::string& output, platform::ui::wifi::Status* status = nullptr);
std::string discover_wireless_interface();
// Empty selection is valid only when exactly one wireless device is present.
std::string select_wireless_interface(const std::filesystem::path& root,
                                      const std::string& requested = {});
} // namespace platform::linux_runtime
