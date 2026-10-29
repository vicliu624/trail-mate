#include "platform/esp/arduino_common/route_image_source_runtime.h"
#include "platform/ui/route_storage.h"
#include "track_sharing/installed_package_path.h"
#include "ui/support/disconnected_route_line.h"
#include "ui/support/kml_route_projection.h"
#include "ui/support/route_image_kml_reader.h"
#include "ui/support/route_image_memory.h"
#include "ui/support/route_image_source_state.h"
#include "ui/support/route_segment_sampling.h"
#include "ui/widgets/route_elevation_profile.h"
#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
namespace storage_fake
{
std::set<std::string> files;
std::map<std::string, std::array<uint8_t, 32>> bindings;
unsigned source_reads = 0, hashes = 0;
platform::ui::route_storage::RouteImageDownloadStatus status;
std::string queued_route;
std::vector<platform::ui::route_storage::RouteImageCacheItem> queued;
std::string route_root = "/routes";
} // namespace storage_fake
namespace platform::ui::route_storage
{
const char* route_dir() { return storage_fake::route_root.c_str(); }
#include "image_reference_hash_actual.inc"
bool route_asset_file_exists(const std::string& path)
{
    return storage_fake::files.count(path) != 0;
}
bool route_image_source_matches(const std::string& path, const uint8_t* source)
{
    ++storage_fake::source_reads;
    const auto it = storage_fake::bindings.find(path);
    return it != storage_fake::bindings.end() && route_asset_file_exists(path) && !std::memcmp(it->second.data(), source, 32);
}
RouteImageDownloadStatus route_image_download_status() { return storage_fake::status; }
bool start_route_image_cache_build(const std::string&, std::vector<RouteImageCacheItem> items, std::string&, RouteImageTaskPresentation, const std::string& route)
{
    storage_fake::queued_route = route;
    storage_fake::queued = std::move(items);
    return true;
}
} // namespace platform::ui::route_storage
namespace platform::ui::device
{
bool sd_ready() { return true; }
} // namespace platform::ui::device
namespace ui::widgets::map
{
struct Position
{
    bool valid;
    double lat, lon;
};
inline double fail_longitude = 999;
bool project_point(int, Position position, lv_point_t& output)
{
    if (position.lon == fail_longitude) return false;
    output = {static_cast<int32_t>(position.lon * 1000), static_cast<int32_t>(position.lat * 1000)};
    return true;
}
} // namespace ui::widgets::map
// This image-source contract exercises the production loader and real KML
// parser. Native-route projection and the map widget are separate adapters;
// their archive/viewport integration is covered by the navigation test suite.
namespace tmap
{
enum class Result
{
    Ok,
    Busy,
    BudgetExceeded,
    Invalid
};
}
namespace native_preview_fake
{
tmap::Result result = tmap::Result::Invalid;
bool fail_owner = false, map_ready = true;
unsigned projections = 0, owners = 0;
} // namespace native_preview_fake
namespace ui::i18n
{
const char* tr(const char* text) { return text; }
} // namespace ui::i18n
namespace ui::support
{
struct TmRouteMap
{
    TmRouteMap() { ++native_preview_fake::owners; }
    ~TmRouteMap() { --native_preview_fake::owners; }
    bool begin(const char*) { return native_preview_fake::map_ready; }
};
struct RouteUiMemory
{
    template <class T>
    static T* create() { return native_preview_fake::fail_owner ? nullptr : new (std::nothrow) T; }
    template <class T>
    static void destroy(T*& value)
    {
        delete value;
        value = nullptr;
    }
};
template <class Points>
tmap::Result projectTmRoutePreview(const char*, Points& points, double& distance)
{
    ++native_preview_fake::projections;
    if (native_preview_fake::result != tmap::Result::Ok) return native_preview_fake::result;
    using Point = std::remove_reference_t<decltype(points[0])>;
    Point first{}, last{};
    first.lat = 30;
    first.lon = 120;
    last.lat = 30.001;
    last.lon = 120.001;
    if (!points.push_back(first) || !points.push_back(last)) return tmap::Result::BudgetExceeded;
    distance = 100;
    return tmap::Result::Ok;
}
} // namespace ui::support
namespace navigation
{
#include "image_ui_navigation_struct_actual.inc"
constexpr size_t kMaxRouteImagePoints = 64;
ui::support::RouteImageVector<RouteImagePoint> s_route_images;
std::string s_route_asset_id = "test";
std::string s_track_file;
bool s_route_image_saved_state_known = false, s_route_image_cache_state_known = false;
bool s_route_image_cache_build_running = false;
unsigned s_route_image_strip_items_hash = 0, s_route_selected_image = 0;
std::vector<int> s_route_image_strip_items;
#include "image_ui_navigation_asset_id_actual.inc"
std::string route_asset_root_for_id(const std::string& id)
{
    return "/routes/.trailmate/" + id;
}
#include "image_ui_navigation_append_actual.inc"
#include "image_ui_navigation_paths_actual.inc"
#include "image_ui_navigation_refresh_actual.inc"

#include "image_ui_navigation_package_cache_actual.inc"

#include "image_ui_navigation_load_actual.inc"
} // namespace navigation
namespace preview
{
#include "image_ui_preview_metrics_struct_actual.inc"
#include "image_ui_preview_point_struct_actual.inc"
#include "image_ui_preview_struct_actual.inc"
constexpr size_t kPreviewMaxImages = 64;
ui::support::RouteImageVector<RoutePreviewImage> s_preview_images;
std::string s_preview_asset_id = "test";
bool s_preview_image_saved_state_known = false, s_preview_image_cache_state_known = false;
size_t s_preview_saved_image_count_hint = 0;
unsigned s_preview_selected_image = 0;
ui::support::RouteImageVector<RoutePreviewPoint, 96> s_preview_route_points;
RoutePreviewMetrics s_preview_metrics;
ui::support::RouteImageVector<ui::widgets::route_elevation_profile::Sample, 96> s_preview_elevation_samples;
ui::support::RouteImageVector<lv_point_precise_t, 192> s_preview_map_line_points;
constexpr size_t kPreviewMaxDrawPoints = 96;
int s_preview_map_runtime = 0;
std::string s_preview_route_name;
std::string s_preview_status_text;
ui::support::TmRouteMap* s_preview_native_route = nullptr;
#include "image_ui_preview_asset_id_actual.inc"
std::string route_path_for_name(const std::string& name)
{
    return "/routes/" + name;
}
void clear_preview_model()
{
    ui::support::RouteUiMemory::destroy(s_preview_native_route);
    s_preview_route_points.clear();
    s_preview_images.clear();
    s_preview_route_name.clear();
    s_preview_asset_id.clear();
    s_preview_map_line_points.clear();
    s_preview_elevation_samples.clear();
    s_preview_elevation_samples.clear();
}
std::string route_asset_root_for_id(const std::string& id) { return "/routes/.trailmate/" + id; }
#include "image_ui_preview_append_actual.inc"
#include "image_ui_preview_package_cache_items_actual.inc"
#include "image_ui_preview_paths_actual.inc"

#include "image_ui_preview_hint_actual.inc"
#include "image_ui_preview_point_append_actual.inc"
#include "image_ui_preview_refresh_actual.inc"
#include "image_ui_preview_status_actual.inc"

#include "image_ui_preview_metrics_actual.inc"

#include "image_ui_preview_draw_points_actual.inc"
#include "image_ui_preview_samples_actual.inc"

#include "image_ui_preview_load_actual.inc"
} // namespace preview
int main()
{
    image_fake::psram = true;
    image_fake::allocation_fail = false;
    // The dispatcher must not send native plans through the KML image reader.
    assert(!preview::load_route_preview_model("busy.tmroute"));
    native_preview_fake::result = tmap::Result::Busy;
    assert(!preview::load_route_preview_model("busy.tmroute"));
    assert(preview::s_preview_status_text == "SD busy; retry preview");
    native_preview_fake::result = tmap::Result::Ok;
    native_preview_fake::fail_owner = true;
    assert(!preview::load_route_preview_model("owner.tmroute"));
    assert(preview::s_preview_status_text == "Route preview budget exceeded");
    assert(preview::s_preview_route_points.empty() && native_preview_fake::owners == 0);
    native_preview_fake::fail_owner = false;
    native_preview_fake::map_ready = false;
    assert(!preview::load_route_preview_model("open.tmroute"));
    assert(native_preview_fake::owners == 0);
    native_preview_fake::map_ready = true;
    assert(preview::load_route_preview_model("saved.tmroute"));
    assert(preview::s_preview_images.empty() && preview::s_preview_route_points.size() == 2);
    assert(native_preview_fake::owners == 1);
    preview::clear_preview_model();
    assert(native_preview_fake::owners == 0 && image_fake::live == 0);
    preview::s_preview_asset_id = "test";
    const std::vector<std::string> urls = {"https://example.invalid/a", "https://example.invalid/b"};
    for (const auto& url : urls)
    {
        assert(navigation::append_route_images(url, 30, 120, true));
        assert(preview::append_pending_images(url, 30, 120, true));
    }
    navigation::assign_route_image_paths();
    // Actual UI queue producers must enqueue unsigned/relative KML references
    // from an installed package without requiring a prior HTTP download.
    navigation::ensure_map_route_image_cache_build();
    assert(storage_fake::queued.empty()); // legacy unbound images keep old rule
    const auto package_name = "p-" + std::string(64, 'a') + ".kml";
    assert(navigation::route_asset_id_for_path("/routes/" + package_name) == package_name.substr(0, 66));
    storage_fake::route_root = "/configured/storage/routes";
    assert(navigation::route_asset_id_for_path(storage_fake::route_root + "/" + package_name) == package_name.substr(0, 66));
    assert(navigation::route_asset_id_for_path("/routes/" + package_name) != package_name.substr(0, 66));
    storage_fake::route_root = "/routes";
    assert(preview::route_asset_id_for_name(package_name) == package_name.substr(0, 66));
    auto second_package = package_name;
    second_package[65] = 'b';
    assert(navigation::route_asset_id_for_path("/routes/" + second_package) != navigation::route_asset_id_for_path("/routes/" + package_name));
    assert(navigation::route_asset_id_for_path("/routes/legacy.kml") == preview::route_asset_id_for_name("legacy.kml"));
    navigation::s_track_file = "/routes/" + package_name;
    navigation::ensure_map_route_image_cache_build();
    assert(storage_fake::queued_route == navigation::s_track_file && storage_fake::queued.size() == 2);
    for (size_t i = 0; i < 2; ++i)
        assert(storage_fake::queued[i].source_hash_valid && !std::memcmp(storage_fake::queued[i].source_hash, navigation::s_route_images[i].source_hash, 32));
    preview::s_preview_route_name = package_name;
    assert(preview::route_preview_cache_items().size() == 2);
    storage_fake::queued.clear();
    storage_fake::status.busy = true;
    navigation::ensure_map_route_image_cache_build();
    assert(storage_fake::queued.empty());
    storage_fake::status.busy = false;
    storage_fake::status.asset_id = navigation::s_route_asset_id;
    storage_fake::status.phase = platform::ui::route_storage::RouteImageDownloadPhase::Failed;
    navigation::ensure_map_route_image_cache_build();
    assert(storage_fake::queued.empty()); // no per-frame retry storm
    storage_fake::status = {};
    navigation::s_track_file.clear();
    preview::s_preview_route_name.clear();
    storage_fake::queued_route.clear();
    preview::assign_preview_image_paths();
    assert(navigation::s_route_images.size() == 2 && preview::s_preview_images.size() == 2);
    assert(image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    for (size_t i = 0; i < 2; ++i)
    {
        const auto& a = navigation::s_route_images[i];
        const auto& b = preview::s_preview_images[i];
        assert(a.source_hash_valid && b.source_hash_valid && !std::memcmp(a.source_hash, b.source_hash, 32));
        storage_fake::files.insert(a.local_path);
        storage_fake::files.insert(a.preview_path);
        storage_fake::files.insert(a.view_path);
    }
    // Legacy files exist, but do not enter the ready count or image cache flags.
    navigation::refresh_route_image_storage_state(true);
    preview::refresh_preview_saved_image_count_hint();
    preview::refresh_preview_image_storage_state(true);
    assert(preview::s_preview_saved_image_count_hint == 0 && ui::support::routeImagesNeedSourceReview(navigation::s_route_images));
    for (const auto& image : navigation::s_route_images) assert(!image.downloaded && !image.preview_ready && !image.view_ready);
    auto& first = navigation::s_route_images[0];
    std::array<uint8_t, 32> binding;
    std::memcpy(binding.data(), first.source_hash, 32);
    storage_fake::bindings[first.local_path] = binding;
    navigation::refresh_route_image_storage_state(true);
    preview::refresh_preview_saved_image_count_hint();
    preview::refresh_preview_image_storage_state(true);
    assert(first.downloaded && first.preview_ready && first.view_ready);
    assert(preview::s_preview_saved_image_count_hint == 1);
    assert(preview::s_preview_images[0].downloaded && preview::s_preview_images[0].preview_ready && !preview::s_preview_images[1].downloaded);
    platform::ui::route_storage::RouteImageDownloadStatus status;
    status.asset_id = "test";
    status.busy = true;
    status.saved = 2;
    status.phase = platform::ui::route_storage::RouteImageDownloadPhase::Downloading;
    assert(!preview::apply_route_preview_download_status_to_images(status));
    assert(!preview::s_preview_images[1].downloaded);
    status.phase = platform::ui::route_storage::RouteImageDownloadPhase::Caching;
    assert(!preview::apply_route_preview_download_status_to_images(status));
    assert(!preview::s_preview_images[1].preview_ready);
    binding[0] ^= 1;
    storage_fake::bindings[first.local_path] = binding;
    status.busy = false;
    status.phase = platform::ui::route_storage::RouteImageDownloadPhase::Done;
    assert(preview::apply_route_preview_download_status_to_images(status) && preview::s_preview_saved_image_count_hint == 0);
    navigation::refresh_route_image_storage_state(false);
    assert(!first.downloaded && !first.preview_ready && !first.view_ready);
    // Hash allocation failure does not authorize an image or fall back internally.
    image_fake::allocation_fail = true;
    uint8_t digest[32];
    assert(!platform::ui::route_storage::route_image_reference_hash(urls[0], digest));
    assert(image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    image_fake::allocation_fail = false;
    ui::support::RouteImageVector<uint8_t> fresh;
    image_fake::allocation_fail = true;
    assert(!fresh.push_back(1) && fresh.failed());
    assert(fresh.empty() && image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    image_fake::allocation_fail = false;
    navigation::s_route_images.clear();
    preview::s_preview_images.clear();
    image_fake::allocation_fail = true;
    assert(!navigation::append_route_images(urls[0], 0, 0, false));
    assert(!preview::append_pending_images(urls[0], 0, 0, false));
    assert(navigation::s_route_images.empty() && navigation::s_route_images.failed());
    assert(preview::s_preview_images.empty() && preview::s_preview_images.failed());
    image_fake::allocation_fail = false;
    navigation::s_route_images.clear();
    preview::s_preview_images.clear();
    for (unsigned i = 0; i < 100; ++i) assert(navigation::append_route_images("same", 0, 0, false) == (i < 64));
    assert(navigation::s_route_images.size() == 64);
    ui::support::RouteImageVector<navigation::RouteImagePoint>{}.swap(navigation::s_route_images);
    ui::support::RouteImageVector<preview::RoutePreviewImage>{}.swap(preview::s_preview_images);
    assert(image_fake::live == 0);
    // Actual production loaders + actual streaming reader/parser. Filesystem,
    // hashing platform admission and ready-record lookup remain mocked above.
    const std::string document = "<k:kml xmlns:k='http://www.opengis.net/kml/2.2' xmlns:g='http://www.google.com/kml/ext/2.2'><k:Document><k:Placemark><k:LineString><k:coordinates>1,2,3 4,5,6</k:coordinates></k:LineString><k:description><![CDATA[" +
                                 std::string(20000, 'x') + "<img\nsrc='a&amp;b.jpg'>]]></k:description></k:Placemark><k:Placemark><k:description>&lt;img src='second.jpg'&gt;</k:description><g:Track><g:coord/><g:coord>120 30 42</g:coord><g:coord>121 31 44</g:coord></g:Track></k:Placemark><k:Placemark><k:description><![CDATA[<img src=unlocated>]]></k:description></k:Placemark></k:Document></k:kml>";
    kml_reader_fake::files["A:/routes/test.kml"] = document;
    assert(navigation::load_kml_route_images("/routes/test.kml"));
    assert(preview::load_route_preview_model("test.kml"));
    assert(navigation::s_route_images.size() == 3 && preview::s_preview_images.size() == 3);
    assert(preview::s_preview_images[0].url == "a&b.jpg" && preview::s_preview_images[1].url == "second.jpg");
    assert(navigation::s_route_images[0].lat == 2 && navigation::s_route_images[0].lon == 1);
    assert(preview::s_preview_images[1].lat == 30 && preview::s_preview_images[1].lon == 120);
    assert(!preview::s_preview_images[2].has_position && !navigation::s_route_images[2].has_position);
    assert(preview::s_preview_route_points.size() == 2 && preview::s_preview_route_points[0].lon == 120 && preview::s_preview_route_points[1].altitude_m == 44);
    for (size_t i = 0; i < 3; ++i) assert(!std::memcmp(navigation::s_route_images[i].source_hash, preview::s_preview_images[i].source_hash, 32));
    assert(kml_reader_fake::open_handles == 0);
    kml_reader_fake::files["A:/routes/line.kml"] = "<kml><Placemark><LineString><altitudeMode>absolute</altitudeMode><coordinates>1,2 3,4,5</coordinates></LineString></Placemark></kml>";
    assert(preview::load_route_preview_model("line.kml"));
    assert(preview::s_preview_route_points.size() == 2 && !preview::s_preview_route_points[0].has_altitude && preview::s_preview_route_points[1].has_altitude);
    kml_reader_fake::files["A:/routes/bad.kml"] = document.substr(0, document.size() - 8);
    assert(!navigation::load_kml_route_images("/routes/bad.kml") && navigation::s_route_images.empty());
    assert(!preview::load_route_preview_model("bad.kml") && preview::s_preview_images.empty() && preview::s_preview_route_points.empty());
    image_fake::allocation_fail = true;
    const unsigned admissions = image_fake::allocations;
    assert(!navigation::load_kml_route_images("/routes/test.kml"));
    assert(image_fake::allocations == admissions + 1 && image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    image_fake::allocation_fail = false;
    const auto file_failure = [&]()
    {
        assert(!navigation::load_kml_route_images("/routes/test.kml") && navigation::s_route_images.empty());
        assert(!preview::load_route_preview_model("test.kml") && preview::s_preview_route_points.empty() && preview::s_preview_images.empty());
        assert(!kml_reader_fake::open_handles && image_fake::live == 0);
    };
    kml_reader_fake::close_fail = true;
    file_failure();
    kml_reader_fake::close_fail = false;
    kml_reader_fake::fail_read = static_cast<int>(kml_reader_fake::reads + 2);
    assert(!navigation::load_kml_route_images("/routes/test.kml") && navigation::s_route_images.empty());
    assert(image_fake::live == 0 && !kml_reader_fake::open_handles);
    kml_reader_fake::fail_read = -1;
    assert(!ui::support::readRouteImageKml("/routes/missing.kml", {}));
    image_fake::psram = false;
    assert(ui::support::readRouteImageKml("/routes/line.kml", {}));
    assert(image_fake::last_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) && image_fake::live == 0);
    image_fake::psram = true;
    std::string many = "<kml><Placemark><description><![CDATA[";
    for (unsigned i = 0; i < 100; ++i) many += "<img src='" + std::to_string(i) + ".jpg'>";
    kml_reader_fake::files["A:/routes/many.kml"] = many + "]]></description></Placemark></kml>";
    assert(navigation::load_kml_route_images("/routes/many.kml") && navigation::s_route_images.size() == 64);
    assert(preview::load_route_preview_model("many.kml") && preview::s_preview_images.size() == 64);
    assert(preview::s_preview_images[63].url == "63.jpg");
    kml_reader_fake::files["A:/routes/segments.kml"] = "<kml xmlns:g='http://www.google.com/kml/ext/2.2'><Placemark><g:MultiTrack><g:Track><altitudeMode>absolute</altitudeMode><g:coord>0 0 0</g:coord><g:coord>0.001 0 10</g:coord><g:coord/><g:coord>100 50 100</g:coord><g:coord>100.001 50 110</g:coord></g:Track><g:Track><g:coord/></g:Track></g:MultiTrack></Placemark></kml>";
    assert(preview::load_route_preview_model("segments.kml"));
    assert(preview::s_preview_route_points.size() == 4);
    assert(preview::s_preview_route_points[0].break_before && !preview::s_preview_route_points[1].break_before);
    assert(preview::s_preview_route_points[2].break_before && !preview::s_preview_route_points[3].break_before);
    assert(preview::s_preview_metrics.ascent_m == 20 && preview::s_preview_metrics.descent_m == 0);
    assert(preview::s_preview_metrics.distance_m > 100 && preview::s_preview_metrics.distance_m < 300);
    preview::rebuild_preview_elevation_samples();
    assert(preview::s_preview_elevation_samples[2].break_before);
    assert(preview::s_preview_elevation_samples[2].distance_m == preview::s_preview_elevation_samples[1].distance_m);
    assert(preview::build_preview_map_line_points());
    auto* map_line = lv_line_create(nullptr);
    lv_event_t map_event{map_line, &draw_fake::layer, nullptr};
    draw_fake::lines.clear();
    ui::support::drawDisconnectedRouteLine(&map_event, preview::s_preview_map_line_points.data(), preview::s_preview_map_line_points.size());
    assert(draw_fake::lines.size() == 2);
    assert(image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ui::widgets::map::fail_longitude = 0.001;
    assert(preview::build_preview_map_line_points());
    draw_fake::lines.clear();
    ui::support::drawDisconnectedRouteLine(&map_event, preview::s_preview_map_line_points.data(), preview::s_preview_map_line_points.size());
    assert(draw_fake::lines.size() == 1);
    ui::widgets::map::fail_longitude = 999;
    image_fake::allocation_fail = true;
    assert(!preview::build_preview_map_line_points() && preview::s_preview_map_line_points.empty());
    assert(image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    image_fake::allocation_fail = false;
    preview::s_preview_route_points.clear();
    assert(preview::s_preview_route_points.push_back({0, 0, 0, true, true}));
    assert(preview::s_preview_route_points.push_back({0, 0.001, 0, false, false}));
    assert(preview::s_preview_route_points.push_back({0, 0.002, 100, true, false}));
    preview::refresh_preview_metrics();
    assert(preview::s_preview_metrics.ascent_m == 0);
    navigation::s_route_images.clear();
    preview::s_preview_images.clear();
    preview::s_preview_map_line_points.clear();
    preview::s_preview_route_points.clear();
    preview::s_preview_elevation_samples.clear();
    assert(image_fake::live == 0 && !kml_reader_fake::open_handles);
}
