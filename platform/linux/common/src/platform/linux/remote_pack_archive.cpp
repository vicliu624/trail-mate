#include "platform/linux/remote_pack_repository.h"
#include "platform/linux/http_download.h"
#include "platform/linux/resource_pack_policy.h"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cctype>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace fs = std::filesystem;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check_cancel(const std::atomic_bool* value) { require(!value || !value->load(), "Package installation cancelled"); }
struct Fd
{
    int value;
    explicit Fd(int input) : value(input) {}
    ~Fd() { if (value >= 0) ::close(value); }
};
bool write_all(int fd, const uint8_t* data, size_t bytes)
{
    while (bytes)
    {
        const auto count = ::write(fd, data, bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count;
        bytes -= static_cast<size_t>(count);
    }
    return true;
}
bool within(const fs::path& child, const fs::path& root)
{
    const auto relative = child.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}
fs::path entry_path(std::string name)
{
    while (!name.empty() && name.back() == '/') name.pop_back();
    require(!name.empty() && name.size() <= 512 && name.find_first_of("\\:\r\n") == name.npos, "Invalid ZIP member path");
    fs::path path(name);
    require(!path.is_absolute(), "Absolute ZIP member path");
    for (const auto& part : path) require(part != "." && part != ".." && part.string().size() <= 255, "ZIP member escapes package");
    require(!path.begin()->string().starts_with('.'), "Reserved package metadata path");
    return path.lexically_normal();
}
void extract_zip(const fs::path& input, const fs::path& output, const std::atomic_bool* cancelled, const std::function<void(int)>& progress)
{
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    require(bool(reader), "ZIP reader unavailable");
    require(archive_read_support_filter_none(reader.get()) == ARCHIVE_OK && archive_read_support_format_zip(reader.get()) == ARCHIVE_OK, "ZIP support unavailable");
    require(archive_read_open_filename(reader.get(), input.c_str(), 64 * 1024) == ARCHIVE_OK, "Cannot open package ZIP");
    std::set<std::string> names;
    uint64_t total = 0;
    std::array<uint8_t, 64 * 1024> scratch{};
    archive_entry* entry = nullptr;
    int result;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK)
    {
        check_cancel(cancelled);
        require(names.size() < 50000, "Package ZIP has too many entries");
        const char* name = archive_entry_pathname_utf8(entry);
        require(name && !archive_entry_symlink(entry) && !archive_entry_hardlink(entry) && archive_entry_is_encrypted(entry) == 0, "Links/encryption are not supported in package ZIPs");
        const auto relative = entry_path(name);
        require(names.insert(relative.generic_string()).second, "Duplicate ZIP member");
        const auto destination = output / relative;
        const auto type = archive_entry_filetype(entry);
        require(type == AE_IFDIR || type == AE_IFREG, "Special ZIP members are not supported");
        if (type == AE_IFDIR) { fs::create_directories(destination); continue; }
        const auto declared = archive_entry_size(entry);
        require(declared >= 0 && uint64_t(declared) <= 1024ULL * 1024 * 1024 - total, "Package ZIP exceeds extracted size limit");
        fs::create_directories(destination.parent_path());
        Fd file(::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        require(file.value >= 0, "Cannot create extracted package file");
        uint64_t written = 0;
        for (;;)
        {
            check_cancel(cancelled);
            const auto count = archive_read_data(reader.get(), scratch.data(), scratch.size());
            require(count >= 0, "Corrupt or truncated package ZIP");
            if (!count) break;
            written += static_cast<uint64_t>(count);
            require(written <= uint64_t(declared) && write_all(file.value, scratch.data(), static_cast<size_t>(count)), "Invalid ZIP size or package write failure");
        }
        require(written == uint64_t(declared) && ::fsync(file.value) == 0, "Incomplete extracted package file");
        total += written;
        if (progress) progress(75);
    }
    require(result == ARCHIVE_EOF && archive_read_has_encrypted_entries(reader.get()) == 0, "Invalid package ZIP directory");
    require(fs::is_regular_file(output / "package.ini"), "Package manifest missing from ZIP root");
}
} // namespace

StagedResourcePack::~StagedResourcePack()
{
    std::error_code ignored;
    fs::remove_all(owner_, ignored);
}

std::unique_ptr<StagedResourcePack> stage_remote_pack(
    const ::ui::runtime::packs::PackageRecord& package, const fs::path& cache_root, const fs::path& installed_root,
    std::string& error, const std::atomic_bool* cancelled, std::function<void(int)> progress)
{
    error.clear();
    try
    {
        check_cancel(cancelled);
        require(!package.id.empty() && !package.version.empty() && package.archive_size_bytes && package.archive_size_bytes <= 128U * 1024U * 1024U && package.archive_sha256.size() == 64 &&
                (package.download_url.starts_with("https://") || package.download_url.starts_with("http://")) && package.download_url.find('\0') == std::string::npos, "Invalid remote package descriptor");
        auto checked = package;
        evaluate_resource_pack_compatibility(checked, resource_pack_policy());
        require(checked.compatible_firmware && checked.compatible_memory_profile, "Package is incompatible with this application");
        fs::create_directories(cache_root);
        const auto cache = fs::weakly_canonical(fs::absolute(cache_root));
        const auto installed = fs::weakly_canonical(fs::absolute(installed_root));
        require(!within(cache, installed) && !within(installed, cache), "Package staging overlaps resource storage");
        fs::create_directories(cache / "pack-downloads");
        std::string pattern = (cache / "pack-downloads" / ".download-XXXXXX").string();
        require(::mkdtemp(pattern.data()) != nullptr, "Cannot create package staging directory");
        std::unique_ptr<StagedResourcePack> staged(new StagedResourcePack(pattern));
        fs::create_directory(staged->directory());
        Fd archive_file(::open(staged->archive().c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        require(archive_file.value >= 0, "Cannot create package archive file");
        platform::ui::http_client::Request request;
        request.url = package.download_url.c_str();
        request.timeout_ms = 60000;
        request.max_bytes = static_cast<uint32_t>(package.archive_size_bytes);
        request.progress = [](uint32_t bytes, uint32_t total, void* context)
        { auto* update = static_cast<std::function<void(int)>*>(context); if (*update && total) (*update)(int(uint64_t(bytes) * 70 / total)); };
        request.progress_context = &progress;
        platform::ui::http_client::TransferStats stats;
        const bool fetched = http_download(request, [](const uint8_t* data, size_t bytes, void* context)
                                            { return write_all(static_cast<Fd*>(context)->value, data, bytes); },
                                            &archive_file, error, &stats, cancelled);
        if (!fetched) return nullptr;
        require(stats.bytes == package.archive_size_bytes && ::fsync(archive_file.value) == 0 && ::lseek(archive_file.value, 0, SEEK_SET) == 0, "Package archive length/write mismatch");
#if defined(TRAIL_MATE_HAS_OPENSSL)
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        require(digest && EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) == 1, "Package digest unavailable");
        std::array<uint8_t, 64 * 1024> scratch{};
        for (;;)
        {
            check_cancel(cancelled);
            const auto count = ::read(archive_file.value, scratch.data(), scratch.size());
            if (count < 0 && errno == EINTR) continue;
            require(count >= 0, "Cannot verify package archive");
            if (!count) break;
            require(EVP_DigestUpdate(digest.get(), scratch.data(), static_cast<size_t>(count)) == 1, "Cannot hash package archive");
        }
        uint8_t bytes[32]{};
        unsigned count = 0;
        require(EVP_DigestFinal_ex(digest.get(), bytes, &count) == 1 && count == 32, "Cannot finish package digest");
        const char* hex = "0123456789abcdef";
        std::string actual;
        for (uint8_t value : bytes) { actual += hex[value >> 4]; actual += hex[value & 15]; }
        auto expected = package.archive_sha256;
        std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return std::tolower(c); });
        require(actual == expected, "Package archive SHA-256 mismatch");
#else
        require(false, "Package digest backend unavailable");
#endif
        extract_zip(staged->archive(), staged->directory(), cancelled, progress);
        check_cancel(cancelled);
        return staged;
    }
    catch (const std::exception& exception) { error = exception.what(); return nullptr; }
}
} // namespace platform::linux_runtime
