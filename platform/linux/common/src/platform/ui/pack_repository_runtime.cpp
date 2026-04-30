#include "platform/ui/pack_repository_runtime.h"

#include "platform/ui/settings_store.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace ui::runtime::packs
{
namespace
{

constexpr const char* kPackRootEnv = "TRAIL_MATE_PACK_ROOT";
constexpr const char* kSettingsNs = "extensions";
constexpr const char* kInstalledIdsKey = "installed_package_ids_v1";
constexpr const char* kInstalledStorage = "linux-local";

std::string trim_copy(std::string value)
{
    auto is_space = [](unsigned char ch)
    { return std::isspace(ch) != 0; };

    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [&](char ch)
                             { return !is_space(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(),
                             value.rend(),
                             [&](char ch)
                             { return !is_space(static_cast<unsigned char>(ch)); })
                    .base(),
                value.end());
    return value;
}

std::vector<std::string> split_csv(const std::string& value)
{
    std::vector<std::string> out;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        item = trim_copy(item);
        if (!item.empty())
        {
            out.push_back(item);
        }
    }
    return out;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return {};
    }

    std::ostringstream contents;
    contents << stream.rdbuf();
    return trim_copy(contents.str());
}

std::filesystem::path repo_root_from_source()
{
    std::filesystem::path path = std::filesystem::path(__FILE__).parent_path();
    for (int i = 0; i < 6; ++i)
    {
        path = path.parent_path();
    }
    return path.lexically_normal();
}

std::filesystem::path pack_root()
{
    if (const char* configured = std::getenv(kPackRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }
    return repo_root_from_source() / "packs";
}

std::size_t directory_size_bytes(const std::filesystem::path& root)
{
    std::size_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_regular_file(ec) && !ec)
        {
            total += static_cast<std::size_t>(it->file_size(ec));
        }
    }
    return total;
}

std::vector<std::string> collect_manifest_ids(const std::filesystem::path& root, const char* category)
{
    std::vector<std::string> ids;
    const std::filesystem::path dir = root / category;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec)
    {
        return ids;
    }

    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (!it->is_directory(ec) || ec)
        {
            continue;
        }
        const std::filesystem::path manifest = it->path() / "manifest.ini";
        if (std::filesystem::exists(manifest, ec) && !ec)
        {
            ids.push_back(it->path().filename().string());
        }
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

std::unordered_set<std::string> load_installed_id_set()
{
    std::string persisted;
    if (!::platform::ui::settings_store::get_string(kSettingsNs, kInstalledIdsKey, persisted))
    {
        return {};
    }

    std::unordered_set<std::string> out;
    std::stringstream stream(persisted);
    std::string line;
    while (std::getline(stream, line))
    {
        line = trim_copy(line);
        if (!line.empty())
        {
            out.insert(line);
        }
    }
    return out;
}

bool save_installed_id_set(const std::unordered_set<std::string>& ids)
{
    std::vector<std::string> ordered(ids.begin(), ids.end());
    std::sort(ordered.begin(), ordered.end());

    std::ostringstream stream;
    for (std::size_t i = 0; i < ordered.size(); ++i)
    {
        if (i > 0)
        {
            stream << '\n';
        }
        stream << ordered[i];
    }
    return ::platform::ui::settings_store::put_string(
        kSettingsNs, kInstalledIdsKey, stream.str().c_str());
}

bool load_package_from_dir(const std::filesystem::path& dir, PackageRecord& out_package, std::string& out_error)
{
    const std::filesystem::path package_ini = dir / "package.ini";
    std::ifstream stream(package_ini, std::ios::binary);
    if (!stream.is_open())
    {
        out_error = "Unable to open package.ini";
        return false;
    }

    std::string line;
    while (std::getline(stream, line))
    {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
        {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        const std::string key = trim_copy(line.substr(0, eq));
        const std::string value = trim_copy(line.substr(eq + 1));
        if (key == "id")
        {
            out_package.id = value;
        }
        else if (key == "package_type")
        {
            out_package.package_type = value;
        }
        else if (key == "version")
        {
            out_package.version = value;
        }
        else if (key == "display_name")
        {
            out_package.display_name = value;
        }
        else if (key == "summary")
        {
            out_package.summary = value;
        }
        else if (key == "description")
        {
            out_package.description = read_text_file(dir / value);
        }
        else if (key == "author")
        {
            out_package.author = value;
        }
        else if (key == "homepage")
        {
            out_package.homepage = value;
        }
        else if (key == "min_firmware_version")
        {
            out_package.min_firmware_version = value;
        }
        else if (key == "supported_memory_profiles")
        {
            out_package.supported_memory_profiles = split_csv(value);
        }
        else if (key == "tags")
        {
            out_package.tags = split_csv(value);
        }
    }

    if (out_package.id.empty())
    {
        out_error = "package.ini is missing an id";
        return false;
    }

    if (out_package.display_name.empty())
    {
        out_package.display_name = out_package.id;
    }
    if (out_package.description.empty())
    {
        out_package.description = out_package.summary;
    }

    out_package.provided_locale_ids = collect_manifest_ids(dir, "locales");
    out_package.provided_font_ids = collect_manifest_ids(dir, "fonts");
    out_package.provided_ime_ids = collect_manifest_ids(dir, "ime");
    out_package.archive_size_bytes = directory_size_bytes(dir);
    out_package.archive_path = dir.string();
    out_package.compatible_memory_profile = true;
    out_package.compatible_firmware = true;
    return true;
}

bool build_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    out_packages.clear();
    out_error.clear();

    const std::filesystem::path root = pack_root();
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
    {
        out_error = "Local pack catalog not found.";
        return false;
    }

    const auto installed_ids = load_installed_id_set();
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (!it->is_directory(ec) || ec)
        {
            continue;
        }

        PackageRecord package{};
        std::string package_error;
        if (!load_package_from_dir(it->path(), package, package_error))
        {
            continue;
        }

        if (installed_ids.find(package.id) != installed_ids.end())
        {
            package.installed = true;
            package.installed_record.id = package.id;
            package.installed_record.version = package.version;
            package.installed_record.storage = kInstalledStorage;
            package.installed_record.installed_at_epoch = 0;
        }
        out_packages.push_back(std::move(package));
    }

    std::sort(out_packages.begin(),
              out_packages.end(),
              [](const PackageRecord& lhs, const PackageRecord& rhs)
              {
                  return lhs.display_name < rhs.display_name;
              });

    if (out_packages.empty())
    {
        out_error = "No language packs available";
        return false;
    }

    return true;
}

} // namespace

bool is_supported()
{
    std::error_code ec;
    return std::filesystem::exists(pack_root(), ec) && !ec;
}

bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error)
{
    out_installed.clear();
    out_error.clear();

    std::vector<PackageRecord> catalog;
    if (!build_catalog(catalog, out_error))
    {
        return false;
    }

    for (const PackageRecord& package : catalog)
    {
        if (package.installed)
        {
            out_installed.push_back(package.installed_record);
        }
    }
    return true;
}

bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    return build_catalog(out_packages, out_error);
}

bool install_package(const PackageRecord& package, std::string& out_error)
{
    out_error.clear();
    if (package.id.empty())
    {
        out_error = "Invalid package id";
        return false;
    }

    auto installed_ids = load_installed_id_set();
    installed_ids.insert(package.id);
    if (!save_installed_id_set(installed_ids))
    {
        out_error = "Failed to save installed package state";
        return false;
    }
    return true;
}

bool uninstall_package(const PackageRecord& package, std::string& out_error)
{
    out_error.clear();
    if (package.id.empty())
    {
        out_error = "Invalid package id";
        return false;
    }

    auto installed_ids = load_installed_id_set();
    installed_ids.erase(package.id);
    if (!save_installed_id_set(installed_ids))
    {
        out_error = "Failed to save installed package state";
        return false;
    }
    return true;
}

} // namespace ui::runtime::packs
