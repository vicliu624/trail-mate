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

bool is_supported();
bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error);
bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error);
bool install_package(const PackageRecord& package, std::string& out_error);

} // namespace ui::runtime::packs
