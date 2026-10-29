#include "platform/ui/route_storage.h"
#include "hostlink/route_storage_lease.h"
#include "track_sharing/image_source_binding.h"
#include "track_sharing/image_source_binding_store.h"
#include "track_sharing/file_integrity.h"
#include "track_sharing/installed_package_path.h"
#include "platform/linux/http_download.h"
#include "platform/linux/route_image_tasks.h"
#include "platform/linux/route_image_cache.h"
#include "platform/linux/route_package_images.h"
#include "../linux/route_image_source_io.h"
#include "../linux/hostlink_upload_io.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

#include "platform/linux/runtime_paths.h"

namespace platform::ui::route_storage
{
namespace
{

std::string s_route_dir_cache{};
std::mutex s_image_batch_mutex;
RouteImageDownloadStatus s_image_batch_status{};
bool s_image_batch_running = false;
thread_local const std::atomic_bool* s_transfer_cancelled = nullptr;
struct WorkerOwner
{
    std::atomic_bool stopping{false};
    std::thread thread;
    ~WorkerOwner() { stop(); }
    void stop()
    {
        stopping = true;
        if (thread.joinable()) thread.join();
    }
};
WorkerOwner& worker_owner()
{
    static WorkerOwner owner;
    return owner;
}
std::mutex s_image_commit_mutex;
RouteImageTaskPresentation s_image_batch_presentation =
    RouteImageTaskPresentation::Hidden;

std::filesystem::path route_dir_path()
{
    return ::platform::linux_runtime::resolve_paths().sd_root / "routes";
}

bool ensure_route_dir()
{
    std::error_code ec;
    const std::filesystem::path dir = route_dir_path();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        return false;
    }
    s_route_dir_cache = dir.string();
    return true;
}

bool is_safe_asset_id(const std::string& asset_id)
{
    if (asset_id.empty() || asset_id == "." || asset_id == ".." ||
        (asset_id.size() > 48 && !track_sharing::isInstalledPackageAssetId(asset_id)))
    {
        return false;
    }
    for (unsigned char ch : asset_id)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
        {
            continue;
        }
        return false;
    }
    return true;
}

bool parse_route_image_name(const std::string& name,
                            std::size_t max_count,
                            std::size_t& out_index)
{
    out_index = 0;
    if (max_count == 0 || name.size() != 12 || name.rfind("img-", 0) != 0)
    {
        return false;
    }
    std::uint32_t value = 0;
    for (std::size_t offset = 4; offset < 8; ++offset)
    {
        const char ch = name[offset];
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint32_t>(ch - '0');
    }
    if (name[8] != '.' ||
        std::tolower(static_cast<unsigned char>(name[9])) != 'j' ||
        std::tolower(static_cast<unsigned char>(name[10])) != 'p' ||
        std::tolower(static_cast<unsigned char>(name[11])) != 'g' ||
        value == 0 ||
        value > max_count)
    {
        return false;
    }
    out_index = static_cast<std::size_t>(value - 1U);
    return true;
}

std::filesystem::path resolve_route_path(const std::string& path_or_name)
{
    std::filesystem::path candidate(path_or_name);
    if (candidate.is_absolute())
    {
        return candidate;
    }
    return route_dir_path() / candidate;
}

void set_batch_status_locked(RouteImageDownloadPhase phase,
                             bool busy,
                             const std::string& asset_id,
                             std::size_t total,
                             std::size_t processed,
                             std::size_t saved,
                             std::size_t failed,
                             std::size_t current_index,
                             std::uint32_t bytes,
                             const char* message,
                             const char* error)
{
    s_image_batch_status.phase = phase;
    s_image_batch_status.busy = busy;
    s_image_batch_status.presentation = s_image_batch_presentation;
    s_image_batch_status.asset_id = asset_id;
    s_image_batch_status.total = total;
    s_image_batch_status.processed = processed;
    s_image_batch_status.saved = saved;
    s_image_batch_status.failed = failed;
    s_image_batch_status.current_index = current_index;
    s_image_batch_status.bytes = bytes;
    s_image_batch_status.current_bytes = 0;
    s_image_batch_status.current_total_bytes = 0;
    s_image_batch_status.message = message ? message : "";
    s_image_batch_status.error = error ? error : "";
}

void promote_batch_presentation_locked(RouteImageTaskPresentation presentation)
{
    if (presentation == RouteImageTaskPresentation::UserVisible)
    {
        s_image_batch_presentation = presentation;
        s_image_batch_status.presentation = presentation;
    }
}

bool cache_one_image(const std::string& asset_id, const RouteImageCacheItem& item, const std::atomic_bool* cancelled)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!item.source_hash_valid) return false;
    namespace native = ::platform::linux_runtime;
    const auto root = route_dir_path();
    const std::filesystem::path input(item.source_path);
    const auto relative = input.is_absolute() ? input.lexically_relative(root) : input;
    std::filesystem::path source, preview, view;
    std::size_t index = 0;
    if (relative.parent_path() != std::filesystem::path(".trailmate") / asset_id / "images" ||
        !parse_route_image_name(relative.filename().string(), 64, index) ||
        !native::resolve_child_under_root(root, relative.generic_string(), source)) return false;
    char thumbnail[32]{}, view_name[32]{};
    std::snprintf(thumbnail, sizeof(thumbnail), "thumb-%04u.bmp", static_cast<unsigned>(index + 1));
    std::snprintf(view_name, sizeof(view_name), "view-%04u.bmp", static_cast<unsigned>(index + 1));
    const auto base = relative.parent_path().parent_path();
    const auto expected_preview = base / "thumbs" / thumbnail, expected_view = base / "views" / view_name;
    if (!native::resolve_child_under_root(root, expected_preview.generic_string(), preview) ||
        !native::resolve_child_under_root(root, expected_view.generic_string(), view) ||
        (!item.preview_path.empty() && std::filesystem::path(item.preview_path).lexically_normal() != preview) ||
        (!item.view_path.empty() && std::filesystem::path(item.view_path).lexically_normal() != view)) return false;
    std::error_code error;
    std::filesystem::create_directories(preview.parent_path(), error);
    if (error) return false;
    std::filesystem::create_directories(view.parent_path(), error);
    if (error) return false;
    std::lock_guard<std::mutex> lease(s_image_commit_mutex);
    const native::RouteImageLease file_lease((source.parent_path().parent_path() / ".route-images.lock").c_str());
    if (!file_lease) return false;
    native::RouteSourceIo io;
    const auto binding = source.string() + ".source";
    if (!io.open(binding.c_str(), native::RouteSourceIo::Mode::Read).handle) return false;
    uint64_t size = 0;
    uint8_t record[track_sharing::kImageSourceBindingBytes]{};
    const bool loaded = io.size(&io, size) && size == sizeof(record) && io.read(&io, record, sizeof(record)) == sizeof(record);
    const bool closed = io.close(&io);
    track_sharing::ImageSourceBindingView expected;
    if (!loaded || !closed || !track_sharing::decodeImageSourceBinding(record, sizeof(record), expected) ||
        std::memcmp(expected.source_hash.data, item.source_hash, 32)) return false;
    return native::build_route_image_bitmaps(source, preview, view, expected.file_hash.data, expected.file_bytes, cancelled);
#else
    return false;
#endif
}

template <class Job>
bool start_image_job(const std::string& asset_id, std::size_t total, std::string& error,
                     RouteImageTaskPresentation presentation, RouteImageDownloadPhase phase, Job job)
{
    {
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        if (s_image_batch_running || s_image_batch_status.busy)
        {
            if (s_image_batch_status.asset_id == asset_id) { promote_batch_presentation_locked(presentation); return true; }
            error = "Another route image task is running";
            return false;
        }
        s_image_batch_running = true;
        s_image_batch_presentation = presentation;
        set_batch_status_locked(phase, true, asset_id, total, 0, 0, 0, 0, 0, "Queued image task", nullptr);
    }
    try
    {
        auto& owner = worker_owner();
        if (owner.thread.joinable()) owner.thread.join();
        owner.stopping = false;
        owner.thread = std::thread([job = std::move(job), &owner]() mutable { job(&owner.stopping); });
        return true;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        s_image_batch_running = false;
        set_batch_status_locked(RouteImageDownloadPhase::Failed, false, asset_id, total, 0, 0, total, 0, 0,
                                "Create image task failed", "Create image task failed");
        error = s_image_batch_status.message;
        return false;
    }
}

void route_cache_worker(std::string asset_id, std::vector<RouteImageCacheItem> items, const std::atomic_bool* cancelled,
                        const std::string& package_route_path)
{
    std::size_t processed = 0, saved = 0, failed = 0;
    const auto package_sources = package_route_path.empty() ? uint64_t(0) :
        ::platform::linux_runtime::materialize_route_package_images(asset_id, package_route_path, items, cancelled);
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        if (cancelled->load()) { failed += items.size() - index; break; }
        {
            std::lock_guard<std::mutex> lock(s_image_batch_mutex);
            set_batch_status_locked(RouteImageDownloadPhase::Caching, true, asset_id, items.size(), processed, saved, failed, index, 0, "Building image cache", nullptr);
        }
        if ((package_route_path.empty() || (package_sources & (uint64_t(1) << index))) &&
            cache_one_image(asset_id, items[index], cancelled)) ++saved;
        else ++failed;
        ++processed;
    }
    const bool ok = !failed && saved == items.size();
    std::lock_guard<std::mutex> lock(s_image_batch_mutex);
    set_batch_status_locked(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed, false,
                            asset_id, items.size(), processed, saved, failed, processed ? processed - 1 : 0, 0,
                            ok ? "Image cache ready" : "Image cache failed", ok ? nullptr : "Source binding, image decoding or storage failed");
    s_image_batch_running = false;
}

void route_image_worker(std::string asset_id, std::vector<RouteImageDownloadItem> items, const std::atomic_bool* cancelled)
{
    s_transfer_cancelled = cancelled;
    const std::size_t total = items.size();
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::uint32_t bytes = 0;
    char detail[96]{};

    for (std::size_t index = 0; index < total; ++index)
    {
        if (cancelled->load()) { failed += total - index; break; }
        const auto& item = items[index];
        {
            std::lock_guard<std::mutex> lock(s_image_batch_mutex);
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            set_batch_status_locked(RouteImageDownloadPhase::Downloading,
                                    true,
                                    asset_id,
                                    total,
                                    processed,
                                    saved,
                                    failed,
                                    index,
                                    bytes,
                                    detail,
                                    nullptr);
        }

        bool source_ready = false;
        uint8_t reference[32]{};
        if (route_image_reference_hash(item.url, reference) && route_image_source_matches(item.output_path, reference))
        {
            source_ready = true;
        }
        else
        {
            const auto result = download_route_image(item.url, item.output_path);
            if (result.ok)
            {
                source_ready = true;
                bytes += result.bytes;
            }
        }
        RouteImageCacheItem cache{item.output_path, item.preview_path, item.view_path};
        cache.source_hash_valid = route_image_reference_hash(item.url, cache.source_hash);
        if (source_ready && cache_one_image(asset_id, cache, cancelled)) ++saved;
        else ++failed;
        ++processed;
    }

    const bool ok = failed == 0 && saved >= total;
    if (ok)
    {
        std::snprintf(detail,
                      sizeof(detail),
                      "Saved all %u images",
                      static_cast<unsigned>(total));
    }
    else
    {
        std::snprintf(detail,
                      sizeof(detail),
                      "Saved %u/%u  failed %u",
                      static_cast<unsigned>(saved),
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(failed));
    }
    {
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        set_batch_status_locked(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed,
                                false,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                processed < total ? processed : (total == 0 ? 0 : total - 1),
                                bytes,
                                detail,
                                ok ? nullptr : detail);
        s_image_batch_running = false;
    }
    s_transfer_cancelled = nullptr;
}

} // namespace

bool is_supported()
{
    return ensure_route_dir();
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!ensure_route_dir())
    {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(route_dir_path(), ec)) || ec) return false;
    for (std::filesystem::directory_iterator it(route_dir_path(), ec), end;
         !ec && it != end && out_routes.size() < max_count; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_symlink(ec) || ec || !it->is_regular_file(ec) || ec)
        {
            continue;
        }
        auto extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (extension != ".kml") continue;
        out_routes.push_back(it->path().filename().string());
    }

    std::sort(out_routes.begin(), out_routes.end());
    if (out_routes.size() > max_count)
    {
        out_routes.resize(max_count);
    }
    return !ec;
}

bool remove_route(const std::string& path)
{
    ::hostlink::route_storage_lease::Guard storage_lease;
    if (!storage_lease.acquired()) return false;
    // Shared pages pass an absolute configured path; callers may also pass a
    // filename. Both must resolve to a direct KML child, never metadata or links.
    const std::filesystem::path input(path);
    const auto relative = input.is_absolute() ? input.lexically_relative(route_dir_path()) : input;
    if (relative.empty() || !relative.parent_path().empty()) return false;
    auto extension = relative.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".kml") return false;
    ::platform::linux_runtime::UploadIo filesystem_lease(::platform::linux_runtime::resolve_paths().sd_root);
    if (!filesystem_lease.current() || !filesystem_lease.lock()) return false;
    const auto logical = std::string("/routes/") + relative.generic_string();
    const auto candidate = filesystem_lease.open(logical.c_str(), ::platform::linux_runtime::UploadIo::Mode::Read);
    return candidate.handle && filesystem_lease.close(candidate.handle) && filesystem_lease.remove(logical.c_str());
}

const char* route_dir()
{
    if (s_route_dir_cache.empty())
    {
        (void)ensure_route_dir();
    }
    return s_route_dir_cache.c_str();
}

bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir)
{
    out_dir.clear();
    if (!ensure_route_dir() || !is_safe_asset_id(asset_id))
    {
        return false;
    }

    std::filesystem::path dir;
    if (!::platform::linux_runtime::resolve_child_under_root(
            route_dir_path(), (std::filesystem::path(".trailmate") / asset_id).generic_string(), dir)) return false;
    std::error_code ec;
    for (const char* child : {"images", "thumbs", "views"})
    {
        std::filesystem::path target;
        if (!::platform::linux_runtime::resolve_child_under_root(
                dir, child, target)) return false;
        std::filesystem::create_directories(target, ec);
        if (ec) return false;
    }
    out_dir = dir.string();
    return true;
}

bool count_route_saved_images(const std::string& asset_id,
                              std::size_t max_count,
                              std::size_t& out_count)
{
    out_count = 0;
    if (!ensure_route_dir() || !is_safe_asset_id(asset_id) || max_count == 0)
    {
        return false;
    }

    std::filesystem::path image_dir;
    if (!::platform::linux_runtime::resolve_child_under_root(
            route_dir_path(), (std::filesystem::path(".trailmate") / asset_id / "images").generic_string(), image_dir)) return false;
    std::error_code ec;
    if (!std::filesystem::is_directory(image_dir, ec) || ec)
    {
        return false;
    }

    std::vector<std::uint8_t> seen(max_count, 0);
    for (std::filesystem::directory_iterator it(image_dir, ec), end;
         !ec && it != end && out_count < max_count; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_symlink(ec) || ec || !it->is_regular_file(ec) || ec)
        {
            continue;
        }
        std::size_t index = 0;
        if (parse_route_image_name(it->path().filename().string(), max_count, index) &&
            index < seen.size() &&
            !seen[index])
        {
            seen[index] = 1;
            ++out_count;
        }
    }
    return !ec;
}

bool route_asset_file_exists(const std::string& path)
{
    std::error_code ec;
    return !path.empty() &&
           std::filesystem::is_regular_file(std::filesystem::path(path), ec) &&
           !ec;
}

bool route_image_source_matches(const std::string& path, const uint8_t expected_source_hash[32])
{
    if (path.empty() || !expected_source_hash) return false;
    const auto root = route_dir_path();
    const auto input = std::filesystem::path(path);
    const auto relative = input.is_absolute() ? input.lexically_relative(root) : input;
    std::filesystem::path resolved;
    if (!::platform::linux_runtime::resolve_child_under_root(root, relative.generic_string(), resolved)) return false;
    std::error_code error;
    std::filesystem::path binding;
    const auto binding_relative = relative.generic_string() + ".source";
    if (!::platform::linux_runtime::resolve_child_under_root(root, binding_relative, binding)) return false;
    if (std::filesystem::file_size(binding, error) != track_sharing::kImageSourceBindingBytes || error) return false;
    uint8_t bytes[track_sharing::kImageSourceBindingBytes];
    std::ifstream file(binding, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(bytes), sizeof(bytes))) return false;
    track_sharing::ImageSourceBindingView record;
    if (!track_sharing::decodeImageSourceBinding(bytes, sizeof(bytes), record) || std::memcmp(record.source_hash.data, expected_source_hash, 32)) return false;
    return std::filesystem::file_size(resolved, error) == record.file_bytes && !error;
}

RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes)
{
    RouteImageDownloadResult result{};
#if defined(TRAIL_MATE_HAS_OPENSSL)
    namespace native = ::platform::linux_runtime;
    if (!max_bytes || max_bytes > track_sharing::kImageSourceByteLimit || !ensure_route_dir())
    {
        result.error = "Invalid image size limit or unavailable route storage";
        return result;
    }
    const auto root = route_dir_path();
    const std::filesystem::path input(output_path);
    const auto relative = input.is_absolute() ? input.lexically_relative(root) : input;
    std::filesystem::path image;
    const auto asset = relative.parent_path().parent_path().filename().string();
    std::size_t index = 0;
    if (!native::resolve_child_under_root(root, relative.generic_string(), image) ||
        relative.parent_path().parent_path().parent_path() != ".trailmate" ||
        relative.parent_path().filename() != "images" || !is_safe_asset_id(asset) ||
        !parse_route_image_name(relative.filename().string(), 64, index))
    {
        result.error = "Invalid route image path";
        return result;
    }
    uint8_t reference[32]{}, digest[32]{}, scratch[320]{};
    if (!route_image_reference_hash(url, reference)) { result.error = "Invalid image URL"; return result; }
    std::error_code error;
    std::filesystem::create_directories(image.parent_path(), error);
    if (error) { result.error = "Create image directory failed"; return result; }
    static std::atomic_uint64_t sequence{0};
    const auto candidate = image.string() + ".download-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
    const auto candidate_binding = candidate + ".source";
    struct Cleanup
    {
        std::string image, binding;
        ~Cleanup() { ::unlink(image.c_str()); ::unlink(binding.c_str()); }
    } cleanup{candidate, candidate_binding};
    native::RouteSourceIo io;
    native::RouteSourceHash hash;
    if (!hash.start() || !io.open(candidate.c_str(), native::RouteSourceIo::Mode::CreateTemporary).handle)
    {
        result.error = "Create image staging file failed";
        return result;
    }
    struct Writer { native::RouteSourceIo& io; native::RouteSourceHash& hash; } writer{io, hash};
    http_client::Request request;
    request.url = url.c_str();
    request.max_bytes = max_bytes;
    request.progress = [](uint32_t bytes, uint32_t total, void*)
    {
        if (!s_transfer_cancelled) return;
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        s_image_batch_status.current_bytes = bytes;
        s_image_batch_status.current_total_bytes = total;
    };
    http_client::TransferStats stats;
    const bool downloaded = native::http_download(request, [](const uint8_t* data, size_t bytes, void* context)
                                                  {
                                                      auto& work = *static_cast<Writer*>(context);
                                                      return work.hash.update(data, bytes) && work.io.write(&work.io, data, bytes) == bytes;
                                                  }, &writer, result.error, &stats, s_transfer_cancelled);
    result.http_status = stats.http_status;
    result.bytes = stats.bytes;
    if (!downloaded) return result;
    const bool synced = io.sync(&io), closed = io.close(&io);
    if (!stats.bytes || !synced || !closed || !hash.finish(digest))
    {
        result.error = "Image write or hash failed";
        return result;
    }
    track_sharing::FileIntegrityCheck<native::RouteSourceIo, native::RouteSourceHash> verified(io, hash);
    if (verified.begin(candidate.c_str(), stats.bytes, digest) != track_sharing::FileIntegrityResult::Pending)
    {
        result.error = "Image verification unavailable";
        return result;
    }
    while (verified.active())
    {
        if (s_transfer_cancelled && s_transfer_cancelled->load()) { result.error = "Image download cancelled"; return result; }
        verified.step(scratch, sizeof(scratch));
    }
    char thumbnail[32]{}, view_name[32]{};
    std::snprintf(thumbnail, sizeof(thumbnail), "thumb-%04u.bmp", static_cast<unsigned>(index + 1));
    std::snprintf(view_name, sizeof(view_name), "view-%04u.bmp", static_cast<unsigned>(index + 1));
    const auto directory = image.parent_path().parent_path();
    const auto binding = image.string() + ".source";
    const auto preview = (directory / "thumbs" / thumbnail).string();
    const auto view = (directory / "views" / view_name).string();
    const auto image_name = image.string();
    const track_sharing::ImageBindingPaths paths{candidate.c_str(), image_name.c_str(), candidate_binding.c_str(), binding.c_str(), preview.c_str(), view.c_str()};
    const track_sharing::ImageSourceBindingView expected{{reference, 32}, {digest, 32}, stats.bytes};
    track_sharing::ImageSourceBindingStore<native::RouteSourceIo> bindings(io);
    {
        std::lock_guard<std::mutex> lock(s_image_commit_mutex);
        const native::RouteImageLease lease((directory / ".route-images.lock").c_str());
        if (!lease) { result.error = "Route image storage busy"; return result; }
        result.ok = bindings.commit(paths, expected, verified, scratch, sizeof(scratch)) == track_sharing::ImageBindingResult::Ready;
    }
    if (!result.ok) result.error = "Image verification or source binding failed";
#else
    result.error = "Image verification backend unavailable";
#endif
    return result;
}

bool route_image_reference_hash(std::string_view reference, uint8_t output[32])
{
    if (!output || reference.empty() || reference.size() > 4096) return false;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    unsigned length = 0;
    return EVP_Digest(reference.data(), reference.size(), output, &length, EVP_sha256(), nullptr) == 1 && length == 32;
#else
    return false;
#endif
}

bool start_route_image_download(const std::string& asset_id,
                                std::vector<RouteImageDownloadItem> items,
                                std::string& out_error,
                                RouteImageTaskPresentation presentation)
{
    out_error.clear();
    if (!is_safe_asset_id(asset_id))
    {
        out_error = "Invalid route asset id";
        return false;
    }
    if (items.empty() || items.size() > 64)
    {
        out_error = "No route images";
        return false;
    }
    const std::size_t total = items.size();

    return start_image_job(asset_id, total, out_error, presentation, RouteImageDownloadPhase::Downloading,
                           [asset_id, items = std::move(items)](const std::atomic_bool* cancelled) mutable
                           { route_image_worker(asset_id, std::move(items), cancelled); });
}

bool start_route_image_cache_build(const std::string& asset_id,
                                   std::vector<RouteImageCacheItem> items,
                                   std::string& out_error,
                                   RouteImageTaskPresentation presentation,
                                   const std::string& package_route_path)
{
    out_error.clear();
    if (!is_safe_asset_id(asset_id) || items.empty() || items.size() > 64)
    {
        out_error = "Invalid image cache request";
        return false;
    }
    const auto package_root = route_dir_path().generic_string();
    const bool physical_package = track_sharing::isInstalledPackageRouteAtRoot(package_route_path, package_root);
    const auto package_name = physical_package ? package_route_path.substr(package_root.size() + 1) : std::string{};
    const auto package_identity = physical_package ? "/routes/" + package_name : package_route_path;
    if (!package_route_path.empty() &&
        (!track_sharing::isInstalledPackageRoute(package_identity) || package_identity.substr(8, 66) != asset_id))
    {
        out_error = "Invalid signed package route";
        return false;
    }
    const auto total = items.size();
    return start_image_job(asset_id, total, out_error, presentation, RouteImageDownloadPhase::Caching,
                           [asset_id, package_identity, items = std::move(items)](const std::atomic_bool* cancelled) mutable
                           { route_cache_worker(asset_id, std::move(items), cancelled, package_identity); });
}

RouteImageDownloadStatus route_image_download_status()
{
    std::lock_guard<std::mutex> lock(s_image_batch_mutex);
    return s_image_batch_status;
}

} // namespace platform::ui::route_storage

namespace platform::linux_runtime
{
void stop_route_image_tasks()
{
    platform::ui::route_storage::worker_owner().stop();
}
} // namespace platform::linux_runtime
