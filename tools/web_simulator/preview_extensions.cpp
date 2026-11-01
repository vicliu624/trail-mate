#include "platform/ui/network_runtime.h"
#include "platform/ui/pack_repository_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include <cstdio>
#include <string>
namespace platform::ui::wifi
{
static Config config{};
static bool connected = false;
bool is_supported() { return true; }
bool load_config(Config& out)
{
    out = config;
    return true;
}
bool save_config(const Config& value)
{
    config = value;
    return true;
}
bool apply_enabled(bool enabled)
{
    config.enabled = enabled;
    if (!enabled) connected = false;
    return true;
}
bool connect(const Config* value)
{
    if (value) config = *value;
    config.enabled = true;
    connected = true;
    return true;
}
void disconnect() { connected = false; }
Status status()
{
    Status out;
    out.supported = true;
    out.enabled = config.enabled;
    out.connected = connected;
    out.state = connected ? ConnectionState::Connected : ConnectionState::Disabled;
    out.has_credentials = true;
    std::snprintf(out.ssid, sizeof(out.ssid), "Preview network");
    return out;
}
} // namespace platform::ui::wifi
namespace ui::runtime::packs
{
static std::uint32_t catalog_revision = 1;
CatalogStatus catalog_status() { return {false, catalog_revision}; }
void request_catalog_refresh() { ++catalog_revision; }
bool is_supported() { return true; }
bool load_installed_packages(std::vector<InstalledPackageRecord>& out, std::string& error)
{
    out.clear();
    error.clear();
    return true;
}
bool fetch_catalog(std::vector<PackageRecord>& out, std::string& error)
{
    out.clear();
    error.clear();
    PackageRecord p;
    p.id = "zh-Hans";
    p.package_type = "locale-bundle";
    p.version = "1.3.0";
    p.display_name = "Simplified Chinese";
    p.summary = "Chinese fonts, translations and Pinyin IME";
    p.provided_locale_ids = {"zh-Hans"};
    p.provided_ime_ids = {"pinyin"};
    p.archive_size_bytes = 1200000;
    p.compatible_firmware = true;
    p.compatible_memory_profile = true;
    out.push_back(p);
    p.id = "ja";
    p.version = "1.2.0";
    p.display_name = "Japanese";
    p.summary = "Japanese fonts and translations (review)";
    p.provided_locale_ids = {"ja"};
    p.provided_ime_ids.clear();
    out.push_back(p);
    return true;
}
bool start_install_package(const PackageRecord&, std::string& error)
{
    error = "Browser preview does not install device packages";
    return false;
}
bool install_package(const PackageRecord& p, std::string& error) { return start_install_package(p, error); }
bool uninstall_package(const PackageRecord&, std::string& error)
{
    error = "No device package is installed in this preview";
    return false;
}
PackageInstallStatus install_status() { return {}; }
} // namespace ui::runtime::packs

namespace platform::ui::network
{
Status status()
{
    const auto link = ::platform::ui::wifi::status();
    return {link.supported, link.connected};
}
} // namespace platform::ui::network
