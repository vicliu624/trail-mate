#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ui::runtime::packs
{

struct InstalledPackageRecord
{
    std::string id;
    std::string version;
    std::string archive_sha256;
    std::string storage;
    std::uint64_t installed_at_epoch = 0;
};

struct PackageRecord
{
    std::string id;
    std::string package_type;
    std::string version;
    std::string display_name;
    std::string summary;
    std::string description;
    std::string author;
    std::string homepage;
    std::string min_firmware_version;
    std::vector<std::string> supported_memory_profiles;
    std::vector<std::string> tags;
    std::vector<std::string> provided_locale_ids;
    std::vector<std::string> provided_font_ids;
    std::vector<std::string> provided_ime_ids;
    std::size_t estimated_unique_font_ram_bytes = 0;
    std::size_t archive_size_bytes = 0;
    std::string archive_path;
    std::string archive_sha256;
    std::string download_url;
    bool compatible_memory_profile = true;
    bool compatible_firmware = true;
    bool installed = false;
    bool update_available = false;
    InstalledPackageRecord installed_record{};
};

enum class PackageInstallPhase : std::uint8_t
{
    Idle = 0,
    Installing,
    Succeeded,
    Failed,
};

struct PackageInstallStatus
{
    PackageInstallPhase phase = PackageInstallPhase::Idle;
    bool busy = false;
    int progress_percent = -1;
    std::string package_id;
    std::string message;
    std::string detail;
};

struct CatalogStatus
{
    bool busy = false;
    std::uint32_t revision = 0;
};
CatalogStatus catalog_status();
void request_catalog_refresh();

bool is_supported();
bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error);
bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error);
bool start_install_package(const PackageRecord& package, std::string& out_error);
PackageInstallStatus install_status();
bool install_package(const PackageRecord& package, std::string& out_error);
bool uninstall_package(const PackageRecord& package, std::string& out_error);

} // namespace ui::runtime::packs
