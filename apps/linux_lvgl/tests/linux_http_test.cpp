#include "platform/linux/http_download.h"
#include "platform/linux/route_image_tasks.h"
#include "platform/ui/route_storage.h"
#include "lvgl.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace http = platform::ui::http_client;
namespace routes = platform::ui::route_storage;
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string base = argv[1], bad_tls = argv[2];
    http::Request request;
    request.max_bytes = 1024;
    request.timeout_ms = 2000;
    std::string output, error;
    http::TransferStats stats;
    std::string url = base + "/ok";
    request.url = url.c_str();
    assert(http::get_text(request, output, error, &stats));
    assert(output == "Ab!9" && stats.bytes == 4 && stats.http_status == 200);
    url = base + "/redirect";
    request.url = url.c_str();
    assert(http::get_text(request, output, error, &stats) && output == "Ab!9");
    url = base + "/chunked";
    request.url = url.c_str();
    assert(http::get_text(request, output, error, &stats) && output == std::string("A\0B", 3));
    for (const auto* path : {"/missing", "/partial", "/oversize", "/file-redirect", "/not-modified"})
    {
        url = base + path;
        request.url = url.c_str();
        assert(!http::get_text(request, output, error, &stats) && output.empty() && !error.empty());
        assert(error != "No error");
    }
    url = bad_tls + "/ok";
    request.url = url.c_str();
    assert(!http::get_text(request, output, error, &stats) && output.empty());
    url = base + "/ok";
    request.url = url.c_str();
    assert(!http::download(request, [](const uint8_t*, size_t, void*) { return false; }, nullptr, error, &stats));
    std::atomic_bool cancelled{true};
    assert(!platform::linux_runtime::http_download(request, [](const uint8_t*, size_t, void*) { return true; }, nullptr, error, &stats, &cancelled));
    url = base + "/slow";
    request.url = url.c_str();
    request.timeout_ms = 80;
    const auto started = std::chrono::steady_clock::now();
    assert(!http::get_text(request, output, error, &stats));
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));

    const auto root = std::filesystem::temp_directory_path() / ("trailmate-http-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SD_ROOT", root.c_str(), 1);
    std::string asset;
    std::size_t image_count = 99;
    for (const std::string& invalid : std::vector<std::string>{".", "..", "../escape", "/absolute", std::string(49, 'x'), "p-" + std::string(64, 'G')})
    {
        assert(!routes::ensure_route_asset_dir(invalid, asset) && asset.empty());
        assert(!routes::count_route_saved_images(invalid, 64, image_count) && image_count == 0);
    }
    const auto package_id = "p-" + std::string(64, 'a');
    assert(routes::ensure_route_asset_dir(package_id, asset));
    assert(std::filesystem::path(asset).filename() == package_id);
    assert(routes::count_route_saved_images(package_id, 64, image_count) && image_count == 0);
    const auto outside = root / "outside-assets";
    std::filesystem::create_directories(outside);
    std::filesystem::create_directory_symlink(outside, root / "routes/.trailmate/escaped-asset");
    assert(!routes::ensure_route_asset_dir("escaped-asset", asset));
    assert(!routes::count_route_saved_images("escaped-asset", 64, image_count));
    assert(!std::filesystem::exists(outside / "images"));
    std::filesystem::create_directories(root / "routes/.trailmate/escaped-images");
    std::filesystem::create_directory_symlink(outside, root / "routes/.trailmate/escaped-images/images");
    assert(!routes::ensure_route_asset_dir("escaped-images", asset));
    assert(routes::ensure_route_asset_dir("fixture", asset));
    std::ofstream(outside / "foreign.jpg") << "untrusted";
    std::filesystem::create_symlink(outside / "foreign.jpg", std::filesystem::path(asset) / "images/img-0064.jpg");
    assert(routes::count_route_saved_images("fixture", 64, image_count) && image_count == 0);
    const auto image = std::filesystem::path(asset) / "images/img-0001.jpg";
    url = base + "/image";
    auto downloaded = routes::download_route_image(url, image.string());
    assert(downloaded.ok && downloaded.bytes && downloaded.http_status == 200);
    uint8_t reference[32]{};
    assert(routes::route_image_reference_hash(url, reference));
    assert(routes::route_image_source_matches(image.string(), reference));
    const auto original_size = std::filesystem::file_size(image);
    const int lease = open((std::filesystem::path(asset) / ".route-images.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    assert(lease >= 0 && flock(lease, LOCK_EX | LOCK_NB) == 0);
    assert(!routes::download_route_image(base + "/jpeg", image.string()).ok);
    assert(std::filesystem::file_size(image) == original_size && routes::route_image_source_matches(image.string(), reference));
    close(lease);
    auto wait_image_task = []
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (routes::route_image_download_status().busy && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(!routes::route_image_download_status().busy);
        return routes::route_image_download_status().phase;
    };
    routes::RouteImageCacheItem cache;
    cache.source_path = image.string();
    cache.preview_path = (std::filesystem::path(asset) / "thumbs/thumb-0001.bmp").string();
    cache.view_path = (std::filesystem::path(asset) / "views/view-0001.bmp").string();
    cache.source_hash_valid = true;
    std::copy_n(reference, 32, cache.source_hash);
    assert(routes::start_route_image_cache_build("fixture", {cache}, error));
    assert(wait_image_task() == routes::RouteImageDownloadPhase::Done);
    auto bitmap = [](const std::string& path, uint32_t width, uint32_t height)
    {
        std::ifstream input(path, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(input)), {});
        assert(data.size() >= 54 && data[0] == 'B' && data[1] == 'M');
        auto integer = [&](size_t at)
        { uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= uint32_t(uint8_t(data[at + i])) << (8 * i); return value; };
        assert(integer(18) == width && integer(22) == height && integer(2) == data.size());
        return data;
    };
    auto thumb = bitmap(cache.preview_path, 200, 120);
    (void)bitmap(cache.view_path, 320, 180);
    lv_init();
    std::vector<uint16_t> frame(200 * 120);
    auto* display = lv_display_create(200, 120);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, frame.data(), nullptr, static_cast<uint32_t>(frame.size() * sizeof(uint16_t)), LV_DISPLAY_RENDER_MODE_DIRECT);
    unsigned flushes = 0;
    lv_display_set_user_data(display, &flushes);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t*, uint8_t*)
                            { ++*static_cast<unsigned*>(lv_display_get_user_data(target)); lv_display_flush_ready(target); });
    lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
    auto* rendered = lv_image_create(lv_screen_active());
    const auto image_uri = "A:" + cache.preview_path;
    lv_image_set_src(rendered, image_uri.c_str());
    lv_obj_set_pos(rendered, 0, 0);
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(display);
    assert(flushes && frame[60 * 200 + 50] == 0xF800 && frame[60 * 200 + 150] == 0x07E0);
    lv_display_delete(display);
    lv_deinit();
    const auto left_pixel = 54 + (120 - 1 - 60) * 600 + 50 * 3;
    assert(uint8_t(thumb[left_pixel + 2]) == 255 && uint8_t(thumb[left_pixel]) == 0);
    // Binding length alone cannot detect same-size corruption; cache admission must hash.
    std::string original;
    { std::ifstream input(image, std::ios::binary); original.assign(std::istreambuf_iterator<char>(input), {}); }
    auto corrupt = original;
    corrupt.back() ^= 1;
    std::ofstream(image, std::ios::binary).write(corrupt.data(), corrupt.size());
    assert(routes::start_route_image_cache_build("fixture", {cache}, error));
    assert(wait_image_task() == routes::RouteImageDownloadPhase::Failed);
    std::ofstream(image, std::ios::binary).write(original.data(), original.size());
    auto invalid_cache = cache;
    invalid_cache.preview_path = (root / "outside-cache.bmp").string();
    assert(routes::start_route_image_cache_build("fixture", {invalid_cache}, error));
    assert(wait_image_task() == routes::RouteImageDownloadPhase::Failed && !std::filesystem::exists(invalid_cache.preview_path));
    assert(!routes::start_route_image_cache_build("fixture", {cache}, error, routes::RouteImageTaskPresentation::PageOnly, "signed-package.kml"));
    const auto jpeg_path = (std::filesystem::path(asset) / "images/img-0002.jpg").string();
    assert(routes::download_route_image(base + "/jpeg", jpeg_path).ok);
    routes::RouteImageCacheItem jpeg;
    jpeg.source_path = jpeg_path;
    jpeg.source_hash_valid = routes::route_image_reference_hash(base + "/jpeg", jpeg.source_hash);
    assert(routes::start_route_image_cache_build("fixture", {jpeg}, error));
    assert(wait_image_task() == routes::RouteImageDownloadPhase::Done);
    auto jpeg_bmp = bitmap((std::filesystem::path(asset) / "views/view-0002.bmp").string(), 320, 180);
    const auto middle = 54 + (180 - 1 - 90) * 960 + 160 * 3;
    assert(uint8_t(jpeg_bmp[middle]) > 240 && uint8_t(jpeg_bmp[middle + 2]) < 16);
    for (const auto* path : {"/missing", "/partial", "/oversize"})
    {
        const auto failed = routes::download_route_image(base + path, image.string(), 1024);
        assert(!failed.ok && std::filesystem::file_size(image) == original_size);
        assert(routes::route_image_source_matches(image.string(), reference));
    }
    assert(!routes::download_route_image(url, (root / "escape.jpg").string()).ok);
    std::filesystem::create_directory_symlink(root.parent_path(), root / "routes/.trailmate/escape");
    assert(!routes::download_route_image(url, (root / "routes/.trailmate/escape/images/img-0001.jpg").string()).ok);
    const auto fifo = std::filesystem::path(asset) / "images/img-0003.jpg";
    assert(mkfifo(fifo.c_str(), 0600) == 0);
    std::filesystem::copy_file(image.string() + ".source", fifo.string() + ".source");
    auto special = cache;
    special.source_path = fifo.string();
    special.preview_path.clear();
    special.view_path.clear();
    assert(routes::start_route_image_cache_build("fixture", {special}, error));
    assert(wait_image_task() == routes::RouteImageDownloadPhase::Failed);
    assert(!routes::download_route_image(base + "/image", fifo.string()).ok);
    std::string batch_asset;
    assert(routes::ensure_route_asset_dir("batch", batch_asset));
    const auto batch_image = (std::filesystem::path(batch_asset) / "images/img-0001.jpg").string();
    assert(routes::start_route_image_download("batch", {{base + "/image", batch_image, {}, {}}}, error));
    assert(wait_image_task() == routes::RouteImageDownloadPhase::Done);
    (void)bitmap((std::filesystem::path(batch_asset) / "thumbs/thumb-0001.bmp").string(), 200, 120);
    (void)bitmap((std::filesystem::path(batch_asset) / "views/view-0001.bmp").string(), 320, 180);
    std::string other_asset;
    assert(routes::ensure_route_asset_dir("other", other_asset));
    const auto pending = (std::filesystem::path(other_asset) / "images/img-0001.jpg").string();
    assert(routes::start_route_image_download("other", {{base + "/slow", pending, {}, {}}}, error));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto stopping = std::chrono::steady_clock::now();
    platform::linux_runtime::stop_route_image_tasks();
    assert(std::chrono::steady_clock::now() - stopping < std::chrono::seconds(3));
    assert(!routes::route_image_download_status().busy);
    assert(!std::filesystem::exists(pending));
    for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(other_asset) / "images"))
        assert(item.path().filename().string().find(".download-") == std::string::npos);
    std::filesystem::remove_all(root);
}
