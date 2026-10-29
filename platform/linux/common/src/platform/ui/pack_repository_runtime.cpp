#include "platform/ui/pack_repository_runtime.h"

#include "platform/linux/resource_pack_store.h"
#include "platform/linux/resource_pack_policy.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/remote_pack_repository.h"
#include "platform/linux/resource_pack_tasks.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace ui::runtime::packs
{
namespace
{

constexpr const char* kPackRootEnv = "TRAIL_MATE_PACK_ROOT";
constexpr std::size_t kTextFileReadChunkBytes = 4096;
constexpr std::size_t kMaxDescriptionBytes = 16 * 1024;

struct InstallWorker
{
    std::mutex status_mutex;
    std::mutex start_mutex;
    PackageInstallStatus status;
    std::thread thread;
    std::atomic_bool stopping{false};
    ~InstallWorker()
    {
        stopping = true;
        if (thread.joinable()) thread.join();
    }
};
InstallWorker& install_worker()
{
    static InstallWorker worker;
    return worker;
}

void set_install_status(PackageInstallPhase phase,
                        bool busy,
                        const std::string& package_id,
                        const char* message,
                        const char* detail = nullptr)
{
    auto& worker = install_worker();
    std::lock_guard<std::mutex> lock(worker.status_mutex);
    worker.status.phase = phase;
    worker.status.busy = busy;
    worker.status.package_id = package_id;
    worker.status.message = message ? message : "";
    worker.status.detail = detail ? detail : "";
    if (phase == PackageInstallPhase::Installing) worker.status.progress_percent = 0;
    else if (phase == PackageInstallPhase::Succeeded) worker.status.progress_percent = 100;
}

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

std::string read_trimmed_text_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return {};
    }

    std::string contents;
    char buffer[kTextFileReadChunkBytes];
    while (stream.good() && contents.size() < kMaxDescriptionBytes)
    {
        const std::size_t remaining = kMaxDescriptionBytes - contents.size();
        const std::size_t chunk = std::min<std::size_t>(sizeof(buffer), remaining);
        stream.read(buffer, static_cast<std::streamsize>(chunk));
        const std::streamsize got = stream.gcount();
        if (got <= 0)
        {
            break;
        }
        contents.append(buffer, static_cast<std::size_t>(got));
    }
    return trim_copy(std::move(contents));
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
    return ::platform::linux_runtime::sd_child("pack-catalog");
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
            out_package.description = read_trimmed_text_file(dir / value);
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
    ::platform::linux_runtime::evaluate_resource_pack_compatibility(out_package, ::platform::linux_runtime::resource_pack_policy());
    return true;
}

bool annotate_installed(std::vector<PackageRecord>& packages, std::string& error)
{
    std::vector<InstalledPackageRecord> records;
    if (!load_installed_packages(records, error)) return false;
    std::map<std::string, InstalledPackageRecord> remaining;
    for (const auto& record : records) remaining.emplace(record.id, record);
    for (auto& package : packages)
    {
        ::platform::linux_runtime::evaluate_resource_pack_compatibility(package, ::platform::linux_runtime::resource_pack_policy());
        if (auto found = remaining.find(package.id); found != remaining.end())
        {
            package.installed = true;
            package.installed_record = found->second;
            package.update_available = package.version != found->second.version ||
                                       (!package.archive_sha256.empty() && package.archive_sha256 != found->second.archive_sha256);
            remaining.erase(found);
        }
    }
    for (const auto& [id, record] : remaining)
    {
        PackageRecord package;
        package.id = package.display_name = id; package.version = record.version;
        package.installed = true; package.installed_record = record;
        packages.push_back(std::move(package));
    }
    std::sort(packages.begin(), packages.end(), [](const auto& left, const auto& right) { return left.display_name < right.display_name; });
    return true;
}

bool build_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    out_packages.clear();
    out_error.clear();

    const std::filesystem::path root = pack_root();
    std::error_code ec;
    const bool has_catalog = std::filesystem::is_directory(root, ec);
    ec.clear();
    std::filesystem::directory_iterator begin;
    if (has_catalog) begin = std::filesystem::directory_iterator(root, ec);
    for (auto it = begin, end = std::filesystem::directory_iterator(); !ec && it != end; it.increment(ec))
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

        out_packages.push_back(std::move(package));
    }
    if (ec)
    {
        out_error = "Unable to read local pack catalog";
        return false;
    }
    if (!annotate_installed(out_packages, out_error)) return false;

    if (out_packages.empty())
    {
        out_error = "No language packs available";
        return false;
    }

    return true;
}

} // namespace

CatalogStatus catalog_status() { return ::platform::linux_runtime::remote_pack_catalog_status(); }
void request_catalog_refresh() { if (::platform::linux_runtime::remote_pack_catalog_enabled()) ::platform::linux_runtime::refresh_remote_pack_catalog(); }

bool is_supported()
{
    if (::platform::linux_runtime::remote_pack_catalog_enabled()) return true;
    std::error_code ec;
    if (std::filesystem::is_directory(pack_root(), ec) && !ec) return true;
    std::vector<InstalledPackageRecord> installed;
    std::string error;
    return load_installed_packages(installed, error) && !installed.empty();
}

bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error)
{
    return ::platform::linux_runtime::ResourcePackStore(::platform::linux_runtime::resource_pack_root())
        .installed(out_installed, out_error);
}

bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    if (::platform::linux_runtime::remote_pack_catalog_enabled())
    {
        if (::platform::linux_runtime::remote_pack_catalog_snapshot(out_packages, out_error))
            return annotate_installed(out_packages, out_error);
        const auto remote_error = out_error;
        if (build_catalog(out_packages, out_error)) return true;
        out_error = remote_error;
        return false;
    }
    return build_catalog(out_packages, out_error);
}

namespace
{
bool install_package_impl(const PackageRecord& package, std::string& out_error, const std::atomic_bool* cancelled,
                          std::function<void(int)> progress)
{
    namespace native = ::platform::linux_runtime;
    auto ready = package;
    std::unique_ptr<native::StagedResourcePack> staged;
    const auto root = native::resource_pack_root();
    if (!package.download_url.empty())
    {
        staged = native::stage_remote_pack(package, native::resolve_paths().cache_root, root, out_error, cancelled, progress);
        if (!staged) return false;
        ready.archive_path = staged->directory();
    }
    if (progress) progress(85);
    return native::ResourcePackStore(root, [progress] { if (progress) progress(95); },
                                     [cancelled] { return cancelled && cancelled->load(); }).install(ready, out_error);
}
}
bool install_package(const PackageRecord& package, std::string& out_error) { return install_package_impl(package, out_error, nullptr, {}); }

bool start_install_package(const PackageRecord& package, std::string& out_error)
{
    auto& worker = install_worker();
    std::lock_guard<std::mutex> start_lock(worker.start_mutex);
    if (install_status().busy)
    {
        out_error = "A package installation is already running";
        return false;
    }
    if (worker.thread.joinable()) worker.thread.join();
    worker.stopping = false;
    out_error.clear();
    set_install_status(PackageInstallPhase::Installing,
                       true,
                       package.id,
                       "Installing package...",
                       package.id.c_str());
    try
    {
        worker.thread = std::thread([package]
                                    {
            std::string error;
            bool ok = false;
            try { ok = install_package_impl(package, error, &install_worker().stopping, [](int percent)
                                             { std::lock_guard<std::mutex> lock(install_worker().status_mutex); install_worker().status.progress_percent = percent; }); }
            catch (const std::exception& exception) { error = exception.what(); }
            set_install_status(ok ? PackageInstallPhase::Succeeded : PackageInstallPhase::Failed,
                               false, package.id, ok ? "Package installed" : error.c_str(), package.id.c_str()); });
        return true;
    }
    catch (const std::exception& exception)
    {
        out_error = exception.what();
        set_install_status(PackageInstallPhase::Failed, false, package.id, out_error.c_str());
        return false;
    }
}

PackageInstallStatus install_status()
{
    auto& worker = install_worker();
    std::lock_guard<std::mutex> lock(worker.status_mutex);
    return worker.status;
}

bool uninstall_package(const PackageRecord& package, std::string& out_error)
{
    return ::platform::linux_runtime::ResourcePackStore(::platform::linux_runtime::resource_pack_root())
        .uninstall(package.id, out_error);
}

} // namespace ui::runtime::packs

namespace platform::linux_runtime
{
void stop_resource_pack_tasks()
{
    stop_remote_pack_catalog();
    auto& worker = ::ui::runtime::packs::install_worker();
    worker.stopping = true;
    if (worker.thread.joinable()) worker.thread.join();
}
}
