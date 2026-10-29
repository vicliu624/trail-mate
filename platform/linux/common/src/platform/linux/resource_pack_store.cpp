#include "platform/linux/resource_pack_store.h"
#include "platform/linux/resource_pack_policy.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sys/file.h>
#include <sys/syscall.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace platform::linux_runtime
{
namespace
{
namespace fs = std::filesystem;
constexpr const char* categories[] = {"fonts", "locales", "ime"};
constexpr const char* marker = ".trailmate-resource-store-v1";
constexpr uintmax_t max_bytes = 1024ULL * 1024 * 1024;
constexpr size_t max_entries = 50000;
thread_local const std::function<bool()>* cancellation = nullptr;
void cancel_point()
{
    if (cancellation && *cancellation && (*cancellation)()) throw std::runtime_error("Resource operation cancelled");
}
struct CancellationScope
{
    const std::function<bool()>* previous;
    explicit CancellationScope(const std::function<bool()>& current) : previous(cancellation) { cancellation = &current; }
    ~CancellationScope() { cancellation = previous; }
};
void copy_file(const fs::path& source, const fs::path& destination)
{
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!input || !output) throw std::runtime_error("Cannot copy resource file");
    std::array<char, 64 * 1024> bytes{};
    for (;;)
    {
        cancel_point();
        input.read(bytes.data(), bytes.size());
        output.write(bytes.data(), input.gcount());
        if (!input.gcount()) break;
    }
    output.close();
    if (input.bad() || !output) throw std::runtime_error("Incomplete resource file copy");
}

struct Fd
{
    int value = -1;
    explicit Fd(int descriptor) : value(descriptor) {}
    ~Fd()
    {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

bool identifier(const std::string& value)
{
    return !value.empty() && value != "." && value != ".." && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch)
                       { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.'; });
}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool present(const fs::path& path)
{
    return fs::symlink_status(path).type() != fs::file_type::not_found;
}

void sync_path(const fs::path& path, bool directory)
{
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | (directory ? O_DIRECTORY : 0)));
    require(fd.value >= 0 && ::fsync(fd.value) == 0, "Cannot synchronize resource storage: " + path.string());
}

struct Budget
{
    uintmax_t bytes = 0;
    size_t entries = 0;
    void add(uintmax_t size)
    {
        cancel_point();
        require(++entries <= max_entries && size <= max_bytes - bytes, "Resource snapshot exceeds storage limits");
        bytes += size;
    }
};

void durable_parent_directories(const fs::path& directory)
{
    std::vector<fs::path> missing;
    for (auto path = directory; !present(path); path = path.parent_path()) missing.push_back(path);
    for (auto it = missing.rbegin(); it != missing.rend(); ++it)
    {
        fs::create_directory(*it);
        sync_path(it->parent_path(), true);
    }
}

// Never follow links from either a catalog or an existing installed tree.
void copy_tree(const fs::path& source, const fs::path& destination, Budget& budget)
{
    require(fs::is_directory(fs::symlink_status(source)), "Resource source is not a directory");
    fs::create_directories(destination);
    for (const auto& entry : fs::directory_iterator(source))
    {
        const auto status = entry.symlink_status();
        const auto target = destination / entry.path().filename();
        budget.add(fs::is_regular_file(status) ? entry.file_size() : 0);
        if (fs::is_directory(status)) copy_tree(entry.path(), target, budget);
        else
        {
            require(fs::is_regular_file(status), "Links and special files are not supported in resource packages");
            ::platform::linux_runtime::copy_file(entry.path(), target);
        }
    }
}

std::map<std::string, std::string> manifest(const fs::path& file)
{
    require(fs::is_regular_file(fs::symlink_status(file)) && fs::file_size(file) <= 64 * 1024,
            "Missing or oversized resource manifest");
    std::ifstream input(file);
    require(bool(input), "Cannot read resource manifest");
    std::map<std::string, std::string> values;
    std::string line;
    auto trim = [](std::string text)
    {
        const auto first = text.find_first_not_of(" \t\r");
        return first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    };
    while (std::getline(input, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto split = line.find('=');
        if (split != std::string::npos) values[trim(line.substr(0, split))] = trim(line.substr(split + 1));
    }
    require(input.eof(), "Cannot finish reading resource manifest");
    return values;
}

bool same_file(const fs::path& left, const fs::path& right)
{
    if (fs::file_size(left) != fs::file_size(right)) return false;
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    require(bool(a) && bool(b), "Cannot compare shared resource files");
    std::array<char, 8192> x{}, y{};
    do
    {
        cancel_point();
        a.read(x.data(), x.size());
        b.read(y.data(), y.size());
        require(!a.bad() && !b.bad(), "Shared resource comparison failed");
        if (a.gcount() != b.gcount() || !std::equal(x.begin(), x.begin() + a.gcount(), y.begin())) return false;
    } while (a.gcount());
    return true;
}

void merge_tree(const fs::path& source, const fs::path& destination)
{
    fs::create_directories(destination);
    for (const auto& entry : fs::directory_iterator(source))
    {
        cancel_point();
        const auto target = destination / entry.path().filename();
        if (entry.is_directory())
        {
            require(!present(target) || fs::is_directory(target), "Conflicting resource directory");
            merge_tree(entry.path(), target);
        }
        else if (present(target))
            require(fs::is_regular_file(target) && same_file(entry.path(), target), "Conflicting shared resource: " + target.string());
        else ::platform::linux_runtime::copy_file(entry.path(), target);
    }
}

void merge_resources(const fs::path& source, const fs::path& destination, bool validate)
{
    for (const char* category : categories)
    {
        const auto group = source / category;
        if (!present(group)) continue;
        require(fs::is_directory(group), "Invalid resource category");
        for (const auto& resource : fs::directory_iterator(group))
        {
            const auto id = resource.path().filename().string();
            require(identifier(id) && resource.is_directory(), "Invalid resource directory name");
            if (validate)
            {
                const auto info = manifest(resource.path() / "manifest.ini");
                const auto field = info.find("id");
                require(field != info.end() && field->second == id, "Resource manifest ID does not match directory");
                for (const char* key : {"file", "strings", "ranges"})
                {
                    const auto item = info.find(key);
                    if (item == info.end() || item->second.empty()) continue;
                    const fs::path relative(item->second);
                    require(!relative.is_absolute(), "Absolute resource manifest path");
                    for (const auto& part : relative) require(part != "..", "Resource manifest path escapes package");
                    require(fs::is_regular_file(resource.path() / relative), "Resource manifest references a missing file");
                }
            }
            merge_tree(resource.path(), destination / category / id);
        }
    }
}

void sync_tree(const fs::path& root)
{
    cancel_point();
    for (const auto& entry : fs::directory_iterator(root))
    {
        cancel_point();
        if (entry.is_directory()) sync_tree(entry.path());
        else sync_path(entry.path(), false);
    }
    sync_path(root, true);
}

void validate_dependencies(const fs::path& root)
{
    for (const char* category : {"locales", "ime"})
    {
        if (!present(root / category)) continue;
        for (const auto& entry : fs::directory_iterator(root / category))
        {
            const auto info = manifest(entry.path() / "manifest.ini");
            for (const char* key : {"ui_font_pack", "content_font_pack", "ime_pack"})
            {
                const auto dependency = info.find(key);
                if (dependency == info.end() || dependency->second.empty()) continue;
                const auto& id = dependency->second;
                if (id == "builtin-latin-ui" || id == "builtin-symbol-core" || id == "builtin-emoji-core") continue;
                require(identifier(id) && present(root / (std::strcmp(key, "ime_pack") == 0 ? "ime" : "fonts") / id / "manifest.ini"),
                        "Missing resource dependency: " + id);
            }
        }
    }
}

struct Stage
{
    fs::path path;
    ~Stage()
    {
        std::error_code ec;
        if (!path.empty()) fs::remove_all(path, ec);
    }
};
} // namespace

ResourcePackStore::ResourcePackStore(fs::path root, std::function<void()> before_publish, std::function<bool()> cancelled)
    : root_(fs::absolute(std::move(root)).lexically_normal()), before_publish_(std::move(before_publish)), cancelled_(std::move(cancelled)) {}

bool ResourcePackStore::installed(std::vector<::ui::runtime::packs::InstalledPackageRecord>& out, std::string& error) const
{
    out.clear();
    error.clear();
    try
    {
        if (!present(root_) || !present(root_ / marker)) return true;
        const auto lock_path = root_.parent_path() / ("." + root_.filename().string() + ".lock");
        Fd lock(::open(lock_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        require(lock.value >= 0 && ::flock(lock.value, LOCK_SH | LOCK_NB) == 0, "Resource storage is busy");
        require(fs::is_directory(fs::symlink_status(root_)) && fs::is_directory(fs::symlink_status(root_ / ".base")),
                "Invalid installed resource store");
        const auto packages = root_ / ".packages";
        if (!present(packages)) return true;
        for (const auto& entry : fs::directory_iterator(packages))
        {
            const auto info = manifest(entry.path() / "package.ini");
            const auto id = info.at("id");
            require(identifier(id) && entry.path().filename() == id, "Invalid installed package record");
            ::ui::runtime::packs::InstalledPackageRecord record;
            record.id = id;
            if (auto version = info.find("version"); version != info.end()) record.version = version->second;
            record.storage = "linux-local";
            const auto digest_path = entry.path() / ".archive-sha256";
            if (present(digest_path))
            {
                require(fs::is_regular_file(fs::symlink_status(digest_path)) && fs::file_size(digest_path) <= 65, "Invalid installed archive digest");
                std::ifstream digest(digest_path);
                digest >> record.archive_sha256;
                require(record.archive_sha256.size() == 64 && std::all_of(record.archive_sha256.begin(), record.archive_sha256.end(), [](unsigned char c) { return std::isxdigit(c); }), "Invalid installed archive digest");
            }
            std::ifstream timestamp(entry.path() / ".installed-at");
            require(bool(timestamp >> record.installed_at_epoch), "Invalid installed package timestamp");
            out.push_back(std::move(record));
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b)
                  { return a.id < b.id; });
        return true;
    }
    catch (const std::exception& exception)
    {
        out.clear();
        error = exception.what();
        return false;
    }
}

bool ResourcePackStore::install(const ::ui::runtime::packs::PackageRecord& package, std::string& error)
{
    return transact(&package, package.id, error);
}

bool ResourcePackStore::uninstall(const std::string& id, std::string& error)
{
    return transact(nullptr, id, error);
}

bool ResourcePackStore::transact(const ::ui::runtime::packs::PackageRecord* package, const std::string& id, std::string& error)
{
    error.clear();
    try
    {
        CancellationScope cancelling(cancelled_);
        cancel_point();
        require(identifier(id) && root_ != root_.root_path(), "Invalid resource store or package ID");
        durable_parent_directories(root_.parent_path());
        const auto lock_path = root_.parent_path() / ("." + root_.filename().string() + ".lock");
        Fd lock(::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
        require(lock.value >= 0 && ::flock(lock.value, LOCK_EX | LOCK_NB) == 0, "Resource storage is busy");
        require(!present(root_) || fs::is_directory(fs::symlink_status(root_)), "Resource root must be a real directory");
        std::string pattern = (root_.parent_path() / ("." + root_.filename().string() + ".stage-XXXXXX")).string();
        require(::mkdtemp(pattern.data()) != nullptr, "Cannot create resource staging directory");
        Stage stage{pattern};
        Budget budget;
        if (present(root_)) copy_tree(root_, stage.path, budget);
        if (!present(stage.path / marker))
        {
            require(!present(stage.path / ".packages") && !present(stage.path / ".base"), "Unrecognized resource store metadata");
            fs::create_directories(stage.path / ".base");
            for (const char* category : categories)
                if (present(stage.path / category)) fs::rename(stage.path / category, stage.path / ".base" / category);
            std::ofstream format(stage.path / marker);
            format << "1\n";
            format.close();
            require(bool(format), "Cannot save resource store format");
        }
        require(fs::is_directory(stage.path / ".base"), "Invalid resource store baseline");
        const auto saved = stage.path / ".packages" / id;
        fs::remove_all(saved);
        if (package)
        {
            require(!package->archive_path.empty(), "Package source is missing");
            const auto source = fs::absolute(package->archive_path).lexically_normal();
            const auto info = manifest(source / "package.ini");
            require(info.at("id") == id && info.at("kind") == "package", "Catalog package identity changed");
            copy_tree(source, saved, budget);
            // Recheck the captured manifest, not caller-supplied compatibility flags.
            const auto captured = manifest(saved / "package.ini");
            require(captured.at("id") == id && captured.at("kind") == "package", "Captured package identity changed");
            require(captured.contains("version") && !captured.at("version").empty() &&
                    (package->version.empty() || captured.at("version") == package->version), "Captured package version changed");
            auto checked = *package;
            const auto minimum = captured.find("min_firmware_version");
            checked.min_firmware_version = minimum == captured.end() ? "" : minimum->second;
            const auto profiles = captured.find("supported_memory_profiles");
            checked.supported_memory_profiles = profiles == captured.end() ? std::vector<std::string>{} : resource_memory_profiles(profiles->second);
            evaluate_resource_pack_compatibility(checked, resource_pack_policy());
            require(checked.compatible_firmware, "Package requires an incompatible firmware version");
            require(checked.compatible_memory_profile, "Package does not support the active memory profile");
            bool has_resources = false;
            for (const char* category : categories)
                if (fs::is_directory(saved / category) && !fs::is_empty(saved / category)) has_resources = true;
            require(has_resources, "Package contains no language, font or IME resources");
            std::ofstream timestamp(saved / ".installed-at");
            timestamp << std::time(nullptr) << '\n';
            timestamp.close();
            require(bool(timestamp), "Cannot save package timestamp");
            fs::remove(saved / ".archive-sha256");
            if (!package->archive_sha256.empty())
            {
                require(package->archive_sha256.size() == 64 && std::all_of(package->archive_sha256.begin(), package->archive_sha256.end(), [](unsigned char c) { return std::isxdigit(c); }), "Invalid package archive digest");
                std::ofstream digest(saved / ".archive-sha256");
                digest << package->archive_sha256 << '\n';
                digest.close();
                require(bool(digest), "Cannot save package digest");
            }
        }
        const auto merged = stage.path / ".merged";
        require(!present(merged), "Unrecognized resource merge metadata");
        fs::create_directory(merged);
        merge_resources(stage.path / ".base", merged, false);
        if (present(stage.path / ".packages"))
            for (const auto& installed : fs::directory_iterator(stage.path / ".packages"))
                merge_resources(installed.path(), merged, true);
        validate_dependencies(merged);
        for (const char* category : categories)
        {
            fs::remove_all(stage.path / category);
            if (present(merged / category)) fs::rename(merged / category, stage.path / category);
        }
        fs::remove(merged);
        sync_tree(stage.path);
        sync_path(root_.parent_path(), true);
        if (before_publish_) before_publish_();
        cancel_point();
        cancellation = nullptr; // Publication and cleanup form the final critical section.
        if (!present(root_)) fs::rename(stage.path, root_);
        else
        {
            // Atomic exchange keeps the previous complete snapshot at stage.path.
            require(::syscall(SYS_renameat2, AT_FDCWD, stage.path.c_str(), AT_FDCWD, root_.c_str(), 2U) == 0,
                    "Filesystem does not support atomic resource publication");
        }
        sync_path(root_.parent_path(), true);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
} // namespace platform::linux_runtime
