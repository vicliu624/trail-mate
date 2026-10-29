#include "platform/linux/remote_pack_repository.h"
#include "platform/linux/http_download.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/pack_repository_defaults.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <string_view>
#include <thread>
#include <unistd.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace packs = ::ui::runtime::packs;
PackRepositorySource source()
{
    const auto* url = std::getenv("TRAIL_MATE_PACK_CATALOG_URL");
    const auto* local = std::getenv("TRAIL_MATE_PACK_ROOT");
    if ((url && std::string_view(url) == "off") || ((!url || !*url) && local && *local)) return {};
    PackRepositorySource result;
    result.catalog_url = url && *url ? url : packs::kDefaultCatalogUrl;
    const auto* base = std::getenv("TRAIL_MATE_PACK_BASE_URL");
    result.base_url = base && *base ? base : result.catalog_url == packs::kDefaultCatalogUrl ? packs::kDefaultCatalogBaseUrl :
                      result.catalog_url.substr(0, result.catalog_url.find_last_of('/'));
    return result;
}
std::filesystem::path cache_file(const PackRepositorySource& input)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    const auto identity = input.catalog_url + "\n" + input.base_url;
    uint8_t digest[32]{};
    unsigned size = 0;
    if (EVP_Digest(identity.data(), identity.size(), digest, &size, EVP_sha256(), nullptr) != 1 || size != 32) return {};
    std::string hex;
    for (uint8_t byte : digest) { hex += "0123456789abcdef"[byte >> 4]; hex += "0123456789abcdef"[byte & 15]; }
    return resolve_paths().cache_root / "pack-repository" / (hex + ".json");
#else
    return {};
#endif
}
std::string cached_text(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error ||
        std::filesystem::file_size(path, error) >= 2 * 1024 * 1024 || error) return {};
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void save_cache(const std::filesystem::path& path, const std::string& text)
{
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    const auto temporary = path.string() + ".tmp-" + std::to_string(getpid());
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return;
    size_t offset = 0;
    while (offset < text.size())
    {
        const auto count = ::write(fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += static_cast<size_t>(count);
    }
    const bool synced = offset == text.size() && fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    if (!synced || !closed || ::rename(temporary.c_str(), path.c_str())) ::unlink(temporary.c_str());
}
class CatalogWorker
{
  public:
    std::mutex start_mutex, mutex;
    std::thread thread;
    std::atomic_bool stopping{false};
    packs::CatalogStatus status;
    bool started = false, ready = false;
    PackRepositorySource active;
    std::vector<packs::PackageRecord> packages;
    std::string error;
    ~CatalogWorker() { stop(); }
    void stop() { stopping = true; if (thread.joinable()) thread.join(); }
    void request(bool force)
    {
        const auto next = source();
        if (next.catalog_url.empty()) return;
        std::lock_guard<std::mutex> serial(start_mutex);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (status.busy || (!force && started && next.catalog_url == active.catalog_url && next.base_url == active.base_url)) return;
        }
        if (thread.joinable()) thread.join();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (next.catalog_url != active.catalog_url || next.base_url != active.base_url) { ready = false; packages.clear(); }
            active = next; started = true; status.busy = true; error.clear();
        }
        stopping = false;
        try { thread = std::thread([this, next] { run(next); }); }
        catch (const std::exception& exception)
        {
            std::lock_guard<std::mutex> lock(mutex);
            error = exception.what(); status.busy = false; ++status.revision;
        }
    }
  private:
    void publish(std::vector<packs::PackageRecord> next)
    {
        std::lock_guard<std::mutex> lock(mutex);
        packages = std::move(next); ready = true; ++status.revision;
    }
    void run(PackRepositorySource input)
    {
        std::string failure;
        try
        {
            const auto path = cache_file(input);
            std::vector<packs::PackageRecord> parsed;
            const auto disk = cached_text(path);
            if (!disk.empty() && parse_pack_catalog(disk, input.base_url, parsed, failure)) publish(std::move(parsed));
            platform::ui::http_client::Request request;
            request.url = input.catalog_url.c_str(); request.max_bytes = 2 * 1024 * 1024; request.timeout_ms = 15000;
            std::string text;
            if (http_download(request, [](const uint8_t* data, size_t bytes, void* context)
                               { static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(data), bytes); return true; },
                               &text, failure, nullptr, &stopping) && parse_pack_catalog(text, input.base_url, parsed, failure))
            { save_cache(path, text); publish(std::move(parsed)); failure.clear(); }
        }
        catch (const std::exception& exception) { failure = exception.what(); }
        std::lock_guard<std::mutex> lock(mutex);
        error = std::move(failure); status.busy = false; ++status.revision;
    }
};
CatalogWorker& worker() { static CatalogWorker value; return value; }
}
bool remote_pack_catalog_enabled() { return !source().catalog_url.empty(); }
void refresh_remote_pack_catalog() { worker().request(true); }
packs::CatalogStatus remote_pack_catalog_status() { std::lock_guard<std::mutex> lock(worker().mutex); return worker().status; }
bool remote_pack_catalog_snapshot(std::vector<packs::PackageRecord>& packages, std::string& error)
{
    worker().request(false);
    std::lock_guard<std::mutex> lock(worker().mutex);
    packages = worker().packages;
    error = worker().ready ? "" : worker().status.busy ? "Loading package catalog..." : worker().error;
    return worker().ready;
}
void stop_remote_pack_catalog() { worker().stop(); }
}
