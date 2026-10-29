#include "platform/linux/network_manager_wifi.h"
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

int main()
{
    namespace fs = std::filesystem;
    using platform::linux_runtime::select_wireless_interface;
    const auto require = [](bool value)
    {
        if (!value) throw std::runtime_error("Wireless interface selection violated");
    };
    char temporary[] = "/tmp/trailmate-wireless-XXXXXX";
    require(mkdtemp(temporary));
    const fs::path root(temporary);
    struct Cleanup
    {
        fs::path root;
        ~Cleanup() { fs::remove_all(root); }
    } cleanup{root};
    require(select_wireless_interface(root).empty());
    fs::create_directory(root / "ethernet-test");
    fs::create_directories(root / "renamed-radio" / "wireless");
    require(select_wireless_interface(root) == "renamed-radio");
    fs::create_directories(root / "other-radio" / "wireless");
    require(select_wireless_interface(root).empty());
    require(select_wireless_interface(root, "other-radio") == "other-radio");
    require(select_wireless_interface(root, "ethernet-test").empty());
    require(select_wireless_interface(root, "missing").empty());
    require(select_wireless_interface(root, "../other-radio").empty());
    require(select_wireless_interface(root, "/other-radio").empty());
    require(select_wireless_interface(root, ".").empty());
    require(select_wireless_interface(root, "..").empty());
    require(select_wireless_interface(root, std::string("other-radio\0extra", 17)).empty());
    require(select_wireless_interface(root, std::string(16, 'x')).empty());
    require(select_wireless_interface("relative").empty());
    fs::rename(root / "other-radio", root / "radio-after");
    require(select_wireless_interface(root, "other-radio").empty());
    require(select_wireless_interface(root, "radio-after") == "radio-after");
    fs::remove_all(root / "renamed-radio");
    require(select_wireless_interface(root) == "radio-after");
    fs::remove_all(root / "radio-after");
    require(select_wireless_interface(root).empty());
}
