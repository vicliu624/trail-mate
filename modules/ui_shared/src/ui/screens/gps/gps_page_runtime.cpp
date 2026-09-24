#include "ui/screens/gps/gps_page_runtime.h"

using Host = gps::ui::shell::Host;
using Projection = gps::ui::shell::Projection;

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "gps/domain/gps_diagnostics.h"
#include "gps/gpx/attribute_reader.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/formatters.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui/presentation_sources/runtime_map_workspace_source.h"
#include "ui/presentation_sources/team_map_overlay_source.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/team_presentation/team_member_label.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/route_elevation_profile.h"
#include "ui/widgets/route_image_operation_presenter.h"
#include "ui/widgets/route_image_strip.h"
#include "ui/widgets/top_bar.h"
#include "ui_gps_runtime/gps_page_runtime_pump.h"
#include "ui_map_runtime/map_geo_coordinates.h"
#include "ui_map_runtime/map_overlay_snapshot_source.h"
#include "ui_presentation/gps/gps_status_model.h"
#include "ui_presentation/map/map_location_request.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
#include "ui_presentation/map/map_target_request.h"
#include "ui_presentation/map/map_workspace_model.h"

#include "ui/menu/dashboard/dashboard_style.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

bool isGPSLoadingTiles()
{
    return false;
}

void show_toast(const char* message, uint32_t duration_ms)
{
    ::ui::feedback::show_notice(message ? message : "", duration_ms);
}

void hide_toast()
{
    ::ui::feedback::hide_notice();
}

namespace
{

constexpr int kCardputerZeroMapDefaultZoom = gps_ui::kDefaultZoom;
constexpr lv_coord_t kMapControlBarHeight = 24;
constexpr lv_coord_t kMapControlButtonHeight = 20;
constexpr lv_coord_t kMapControlButtonSmallWidth = 26;
constexpr lv_coord_t kMapControlButtonMediumWidth = 36;
constexpr lv_coord_t kMapControlButtonWideWidth = 44;
constexpr lv_coord_t kMapControlButtonContourWidth = 56;
constexpr lv_coord_t kMapControlButtonTrackerWidth = 42;
constexpr lv_coord_t kMapSideRailWidth = 72;
constexpr lv_coord_t kMapAltitudePanelHeight = 18;
constexpr lv_coord_t kMapAltitudePanelWidth = 82;
constexpr lv_coord_t kRouteElevationPanelHeight = 54;
constexpr lv_coord_t kRouteElevationPanelInset = 4;
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kRouteDeviationThresholdM = 10.0;
constexpr double kRouteDeviationClearThresholdM = 7.0;
constexpr std::uint32_t kLvglFunctionKeyF1 = 0x110001U;
constexpr std::uint32_t kInvalidMemberId = 0xFFFFFFFFU;
constexpr std::size_t kMaxTrackOverlayPoints = 96;
constexpr std::size_t kTrackFileReadBufferBytes = 256;
constexpr std::size_t kMaxTrackFileLineBytes = 2048;
constexpr std::size_t kMaxKmlTagBytes = 80;
constexpr std::size_t kMaxKmlCoordinateTokenBytes = 96;
constexpr std::size_t kMaxRouteImagePoints = 64;
constexpr int kDefaultTrackerZoom = 16;

struct TrackOverlayPoint
{
    double lat = 0.0;
    double lon = 0.0;
    double altitude_m = 0.0;
    bool has_altitude = false;
};

struct TrackElevationMetrics
{
    std::size_t altitude_count = 0;
    double min_altitude_m = 0.0;
    double max_altitude_m = 0.0;
    double ascent_m = 0.0;
    double descent_m = 0.0;
    double previous_altitude_m = 0.0;
    bool has_previous_altitude = false;
};

struct RouteImagePoint
{
    double lat = 0.0;
    double lon = 0.0;
    bool has_position = false;
    bool downloaded = false;
    bool preview_ready = false;
    bool view_ready = false;
    std::string local_path{};
    std::string preview_path{};
    std::string view_path{};
};

enum class TrackOverlayFileKind : uint8_t
{
    Track,
    Route,
};

enum class MapControlAction : uint8_t
{
    ZoomOut = 0,
    ZoomIn,
    Center,
    Layer,
    Contour,
    Tracker,
    Help,
    Route,
    TeamMember,
    PickLocation,
    CancelLocation,
};

const Host* s_host = nullptr;
::ui::map::MapLocationRequest* s_location_request = nullptr;
::ui::map::MapTargetRequest* s_target_request = nullptr;
lv_obj_t* s_map_pick_btn = nullptr;
lv_obj_t* s_map_cancel_btn = nullptr;
lv_obj_t* s_root = nullptr;
lv_timer_t* s_timer = nullptr;
::ui::widgets::TopBar s_top_bar;
::ui::widgets::map::Runtime s_map_runtime;
lv_obj_t* s_map_viewport = nullptr;
int s_map_zoom = kCardputerZeroMapDefaultZoom;
int s_map_pan_x = 0;
int s_map_pan_y = 0;
bool s_map_view_initialized = false;
bool s_map_info_visible = true;
::ui::map::MapOverlaySnapshot* s_overlay_snapshot = nullptr;
const ::gps::ui::runtime::MapTarget* s_map_target = nullptr;
Projection s_projection = Projection::Map;
bool s_gps_power_lease_active = false;
lv_obj_t* s_gps_status_label = nullptr;
lv_obj_t* s_gps_coord_label = nullptr;
lv_obj_t* s_gps_sat_label = nullptr;
lv_obj_t* s_gps_alt_label = nullptr;
lv_obj_t* s_gps_motion_label = nullptr;
lv_obj_t* s_gps_time_label = nullptr;
lv_obj_t* s_gps_diag_label = nullptr;
lv_obj_t* s_map_control_bar = nullptr;
lv_obj_t* s_map_zoom_label = nullptr;
lv_obj_t* s_map_zoom_out_btn = nullptr;
lv_obj_t* s_map_zoom_in_btn = nullptr;
lv_obj_t* s_map_center_btn = nullptr;
lv_obj_t* s_map_layer_btn = nullptr;
lv_obj_t* s_map_contour_btn = nullptr;
lv_obj_t* s_map_help_btn = nullptr;
lv_obj_t* s_map_tracker_btn = nullptr;
lv_obj_t* s_map_altitude_panel = nullptr;
lv_obj_t* s_map_altitude_label = nullptr;
::ui::widgets::route_elevation_profile::Widget s_route_elevation_profile;
::ui::widgets::route_image_strip::Widget s_route_image_strip;
lv_obj_t* s_map_notice_panel = nullptr;
lv_obj_t* s_map_notice_label = nullptr;
lv_obj_t* s_map_context_rail = nullptr;
lv_obj_t* s_map_route_btn = nullptr;
lv_obj_t* s_map_help_modal = nullptr;
lv_obj_t* s_tracker_modal = nullptr;
bool s_map_help_open_pending = false;
bool s_map_refresh_pending = false;
bool s_map_drag_active = false;
bool s_map_tile_loader_paused = false;
uint8_t s_map_context_mask = 0xFF;
char s_map_notice_text[64]{};
uint32_t s_map_notice_until_ms = 0;
int s_map_drag_start_pan_x = 0;
int s_map_drag_start_pan_y = 0;
std::vector<TrackOverlayPoint> s_track_points;
std::vector<RouteImagePoint> s_route_images;
std::vector<::ui::widgets::route_image_strip::Item> s_route_image_strip_items;
std::vector<::ui::widgets::route_elevation_profile::Sample> s_route_elevation_samples;
std::vector<::ui::widgets::route_elevation_profile::Sample> s_route_elevation_work_samples;
TrackElevationMetrics s_track_elevation_metrics;
std::vector<std::string> s_track_modal_names;
std::string s_track_file;
std::string s_route_asset_id;
bool s_track_overlay_active = false;
TrackOverlayFileKind s_track_overlay_kind = TrackOverlayFileKind::Track;
bool s_route_elevation_profile_visible = false;
bool s_route_image_strip_visible = false;
bool s_route_image_saved_state_known = false;
bool s_route_image_cache_state_known = false;
bool s_route_image_cache_build_running = false;
bool s_route_deviation_active = false;
double s_route_deviation_distance_m = 0.0;
std::size_t s_route_selected_image = 0;
uint32_t s_route_image_strip_items_hash = 0;
std::vector<lv_obj_t*> s_member_buttons;
std::vector<uint32_t> s_member_button_ids;
uint32_t s_member_list_hash = 0;
uint32_t s_selected_member_id = kInvalidMemberId;
uint32_t s_member_panel_last_ms = 0;

void refresh_view();
void root_key_event_cb(lv_event_t* e);
void open_map_help_modal();
void open_tracker_modal();
void add_map_controls_to_group(lv_group_t* group);
void request_refresh_view();
void consume_key_event(lv_event_t* e);
void sync_map_tile_loader_pause();
void sync_map_chrome_visibility();
void rebuild_map_control_group();
bool load_map_track_file_impl(const char* path, bool show_fail_toast);

::ui::map::MapOverlaySnapshot* allocate_overlay_snapshot()
{
#if defined(ESP_PLATFORM)
    void* storage = heap_caps_malloc(sizeof(::ui::map::MapOverlaySnapshot),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* storage = std::malloc(sizeof(::ui::map::MapOverlaySnapshot));
#endif
    if (!storage)
    {
        return nullptr;
    }
    return new (storage)::ui::map::MapOverlaySnapshot{};
}

bool ensure_overlay_snapshot()
{
    if (s_overlay_snapshot)
    {
        return true;
    }

    s_overlay_snapshot = allocate_overlay_snapshot();
    if (!s_overlay_snapshot)
    {
        std::printf("[GPS][MAP] enter denied reason=psram_overlay_alloc bytes=%u\n",
                    static_cast<unsigned>(sizeof(::ui::map::MapOverlaySnapshot)));
        return false;
    }

    std::printf("[GPS][MAP] overlay allocated memory=psram bytes=%u\n",
                static_cast<unsigned>(sizeof(::ui::map::MapOverlaySnapshot)));
    return true;
}

void release_overlay_snapshot()
{
    if (!s_overlay_snapshot)
    {
        return;
    }

    s_overlay_snapshot->~MapOverlaySnapshot();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_overlay_snapshot);
#else
    std::free(s_overlay_snapshot);
#endif
    s_overlay_snapshot = nullptr;
}

void downsample_track_points(std::vector<TrackOverlayPoint>& points);
void append_track_point_raw(std::vector<TrackOverlayPoint>& out,
                            double lat,
                            double lon,
                            double altitude_m = 0.0,
                            bool has_altitude = false);
void append_track_point(std::vector<TrackOverlayPoint>& out,
                        double lat,
                        double lon,
                        double altitude_m = 0.0,
                        bool has_altitude = false);
void build_route_elevation_samples(
    const std::vector<TrackOverlayPoint>& points,
    std::vector<::ui::widgets::route_elevation_profile::Sample>& out);
::ui::presentation_sources::TeamMapOverlaySource& team_map_overlay_source();
void apply_map_drag_preview();
lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action);
void sync_map_route_image_strip();
void refresh_route_image_storage_state(bool include_cache_state);

::ui::map::MapWorkspaceModel& map_workspace_model();

void request_exit()
{
    if (s_location_request)
    {
        auto& model = map_workspace_model();
        if (model.locationSelection().state == ::ui::map::MapLocationSelectionState::Selecting &&
            !model.cancelLocationSelection().ok) return;
        s_location_request->result = model.locationSelection();
    }
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

::ui::map::MapWorkspaceModel& map_workspace_model()
{
    static ::ui::presentation_sources::RuntimeMapWorkspaceSource source(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state(),
        &::team::ui::team_ui_snapshot_store());
    static ::ui::presentation_sources::RuntimeMapActionSink sink(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state());
    static ::ui::map::MapWorkspaceModel model(source, sink);
    return model;
}

class RuntimeMapOverlayGpsSource final : public ::ui::map_overlay::IMapOverlayGpsSource
{
  public:
    bool currentFix(double& lat, double& lon, bool& valid) const override
    {
        ::ui::gps::GpsStatusSnapshot gps;
        if (!::ui::presentation_sources::runtime_gps_status_source().buildGpsStatusSnapshot(gps))
        {
            return false;
        }

        lat = gps.latitude;
        lon = gps.longitude;
        valid = gps.header.valid && gps.fix_valid;
        return true;
    }
};

::ui::map::IMapOverlayPresentationSource& map_overlay_source()
{
    static RuntimeMapOverlayGpsSource gps;
    static ::ui::map_overlay::MapOverlaySnapshotSource source(&gps, &team_map_overlay_source());
    return source;
}

::ui::presentation_sources::TeamMapOverlaySource& team_map_overlay_source()
{
    static ::ui::presentation_sources::TeamMapOverlaySource team(
        ::team::ui::team_ui_snapshot_store());
    return team;
}

uint8_t current_map_zoom()
{
    return static_cast<uint8_t>(
        std::max(::ui::widgets::map::kMinZoom,
                 std::min(s_map_zoom, ::ui::widgets::map::kMaxZoom)));
}

bool has_valid_viewport_center(const ::ui::map::MapViewport& viewport)
{
    return std::isfinite(viewport.center_lat) &&
           std::isfinite(viewport.center_lon) &&
           (s_map_target || viewport.center_lat != 0.0 || viewport.center_lon != 0.0);
}

void sync_workspace_layers_from_renderer()
{
    auto& state = ::ui::presentation_sources::runtime_map_workspace_state();
    const auto layers = ::ui::widgets::map::current_layer_state();
    state.layers.osm = layers.map_source == 0;
    state.layers.terrain = layers.map_source == 1;
    state.layers.satellite = layers.map_source == 2;
    state.layers.contour = layers.contour_enabled;
}

void sync_workspace_viewport_from_renderer()
{
    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.zoom = current_map_zoom();
    if (has_valid_viewport_center(viewport))
    {
        (void)model.setViewport(viewport);
    }
    (void)model.setActiveTool(::ui::map::MapToolKind::Pan);
}

bool sync_workspace_center_from_screen()
{
    const auto coordinate_system = app::configFacade().readConfig().map_coord_system;
    if (coordinate_system != 0 && !s_location_request && !s_target_request && !s_map_target)
    {
        return false;
    }

    ::ui::widgets::map::GeoPoint center{};
    if (!::ui::widgets::map::screen_center(s_map_runtime, center) || !center.valid)
    {
        return false;
    }

    if ((s_location_request || s_target_request || s_map_target) && !::ui::map_geo::inverse(center.lat, center.lon, coordinate_system, center.lat, center.lon))
        return false;

    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.center_lat = center.lat;
    viewport.center_lon = center.lon;
    viewport.zoom = current_map_zoom();
    return model.setViewport(viewport).ok;
}

bool commit_pending_map_pan_from_screen()
{
    if (s_map_pan_x == 0 && s_map_pan_y == 0)
    {
        return false;
    }

    if (!sync_workspace_center_from_screen())
    {
        return false;
    }

    s_map_pan_x = 0;
    s_map_pan_y = 0;
    sync_workspace_viewport_from_renderer();
    return true;
}

::ui::widgets::map::Model build_map_model(
    const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    const auto& config = app::configFacade().readConfig();

    ::ui::widgets::map::Model model{};
    const bool has_viewport_center = has_valid_viewport_center(snapshot.viewport) ||
                                     ((s_location_request || s_target_request) && snapshot.viewport.zoom != 0 &&
                                      ::ui::map_geo::valid(snapshot.viewport.center_lat, snapshot.viewport.center_lon));
    model.focus_point.valid = true;
    model.focus_point.lat = has_viewport_center
                                ? snapshot.viewport.center_lat
                                : (snapshot.self.valid ? snapshot.self.lat
                                                       : gps_ui::kDefaultLat);
    model.focus_point.lon = has_viewport_center
                                ? snapshot.viewport.center_lon
                                : (snapshot.self.valid ? snapshot.self.lon
                                                       : gps_ui::kDefaultLng);
    model.zoom = snapshot.viewport.zoom == 0 ? s_map_zoom : snapshot.viewport.zoom;
    model.pan_x = s_map_pan_x;
    model.pan_y = s_map_pan_y;
    model.map_source = config.map_source;
    model.contour_enabled = snapshot.layers.contour;
    model.coord_system = config.map_coord_system;
    return model;
}

const char* diagnostic_label()
{
    const auto diagnostics = ::platform::ui::gps::diagnostics();
    return ::gps::gpsDiagnosticCodeName(diagnostics.code);
}

::ui::gps::GpsStatusModel& gps_status_model()
{
    static ::ui::gps::GpsStatusModel model(
        ::ui::presentation_sources::runtime_gps_status_source());
    return model;
}

void set_compact_label(lv_obj_t* label, const char* text)
{
    if (!label || !lv_obj_is_valid(label))
    {
        return;
    }
    lv_label_set_text(label, text ? text : "");
}

bool format_current_gps_map_title(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';

    const auto gps = ::platform::ui::gps::get_data();
    if (!gps.valid || !std::isfinite(gps.lat) || !std::isfinite(gps.lng))
    {
        return false;
    }

    char coord_buf[64]{};
    ui_format_coords(gps.lat,
                     gps.lng,
                     app::configFacade().readConfig().gps_coord_format,
                     coord_buf,
                     sizeof(coord_buf));
    if (coord_buf[0] == '\0')
    {
        return false;
    }

    std::snprintf(out, out_len, "%s - %.48s", ::ui::i18n::tr("Map"), coord_buf);
    return true;
}

void update_map_top_bar_title()
{
    if (s_location_request)
    {
        ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("Choose on map"));
        return;
    }
    if (s_projection != Projection::Map)
    {
        ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("GPS"));
        return;
    }

    char title[64]{};
    if (format_current_gps_map_title(title, sizeof(title)))
    {
        ::ui::widgets::top_bar_set_title(s_top_bar, title);
        return;
    }

    ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("Map"));
}

void set_map_notice(const char* text, uint32_t duration_ms)
{
    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    if (!text || text[0] == '\0')
    {
        return;
    }

    std::snprintf(s_map_notice_text, sizeof(s_map_notice_text), "%s", text);
    s_map_notice_until_ms = sys::millis_now() + duration_ms;
}

const char* compact_map_source_label(uint8_t map_source)
{
    switch (map_source)
    {
    case 1:
        return "Ter";
    case 2:
        return "Sat";
    case 0:
    default:
        return "OSM";
    }
}

void set_button_label(lv_obj_t* btn, const char* text)
{
    if (!btn || !lv_obj_is_valid(btn))
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    set_compact_label(label, text);
}

void clear_map_controls()
{
    s_map_pick_btn = s_map_cancel_btn = nullptr;
    s_map_viewport = nullptr;
    s_map_control_bar = nullptr;
    s_map_zoom_label = nullptr;
    s_map_zoom_out_btn = nullptr;
    s_map_zoom_in_btn = nullptr;
    s_map_center_btn = nullptr;
    s_map_layer_btn = nullptr;
    s_map_contour_btn = nullptr;
    s_map_help_btn = nullptr;
    s_map_tracker_btn = nullptr;
    s_map_altitude_panel = nullptr;
    s_map_altitude_label = nullptr;
    ::ui::widgets::route_elevation_profile::reset(s_route_elevation_profile);
    ::ui::widgets::route_image_strip::destroy(s_route_image_strip);
    ::ui::widgets::route_image_strip::reset(s_route_image_strip);
    s_route_elevation_samples.clear();
    s_route_elevation_work_samples.clear();
    s_route_images.clear();
    s_route_image_strip_items.clear();
    s_route_asset_id.clear();
    s_map_notice_panel = nullptr;
    s_map_notice_label = nullptr;
    s_map_context_rail = nullptr;
    s_map_route_btn = nullptr;
    s_map_help_modal = nullptr;
    s_tracker_modal = nullptr;
    s_map_help_open_pending = false;
    s_map_refresh_pending = false;
    s_map_drag_active = false;
    s_map_context_mask = 0xFF;
    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    s_route_elevation_profile_visible = false;
    s_route_image_strip_visible = false;
    s_route_image_saved_state_known = false;
    s_route_image_cache_state_known = false;
    s_route_image_cache_build_running = false;
    s_route_deviation_active = false;
    s_route_deviation_distance_m = 0.0;
    s_route_selected_image = 0;
    s_route_image_strip_items_hash = 0;
    s_member_buttons.clear();
    s_member_button_ids.clear();
    s_member_list_hash = 0;
    s_member_panel_last_ms = 0;
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }

    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool map_control_visible(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void resize_map_runtime_to_viewport()
{
    if (!s_root || !s_map_viewport || !lv_obj_is_valid(s_map_viewport))
    {
        return;
    }

    lv_obj_update_layout(s_root);
    lv_obj_update_layout(s_map_viewport);
    ::ui::widgets::map::set_size(s_map_runtime,
                                 lv_obj_get_content_width(s_map_viewport),
                                 lv_obj_get_content_height(s_map_viewport));

    const auto& widgets = ::ui::widgets::map::widgets(s_map_runtime);
    if (widgets.root && lv_obj_is_valid(widgets.root))
    {
        lv_obj_align(widgets.root, LV_ALIGN_CENTER, 0, 0);
    }
}

void sync_map_chrome_visibility()
{
    if (s_projection != Projection::Map)
    {
        return;
    }

    const bool show_info = s_map_info_visible;
    set_hidden(s_top_bar.container, !show_info);
    set_hidden(s_map_control_bar, !show_info);

    if (!show_info)
    {
        set_hidden(s_map_altitude_panel, true);
        set_hidden(s_map_context_rail, true);
        set_hidden(s_map_notice_panel, true);
    }

    if (app_g && !(s_map_help_modal && lv_obj_is_valid(s_map_help_modal)) &&
        !(s_tracker_modal && lv_obj_is_valid(s_tracker_modal)))
    {
        if (show_info)
        {
            rebuild_map_control_group();
        }
        else if (s_root && lv_obj_is_valid(s_root))
        {
            lv_group_remove_all_objs(app_g);
            lv_group_add_obj(app_g, s_root);
            lv_group_focus_obj(s_root);
            lv_group_set_editing(app_g, false);
        }
    }

    resize_map_runtime_to_viewport();
}

bool format_map_altitude_label(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }

    const auto snapshot = gps_status_model().snapshot();
    if (snapshot.header.valid &&
        snapshot.fix_valid &&
        snapshot.has_altitude &&
        std::isfinite(snapshot.altitude_m))
    {
        std::snprintf(out, out_len, "Alt %.0f m", static_cast<double>(snapshot.altitude_m));
        return true;
    }

    std::snprintf(out, out_len, "Alt --");
    return false;
}

void sync_map_altitude_overlay()
{
    if (!s_map_altitude_panel || !lv_obj_is_valid(s_map_altitude_panel) ||
        !s_map_altitude_label || !lv_obj_is_valid(s_map_altitude_label))
    {
        return;
    }

    if (!s_map_info_visible)
    {
        set_hidden(s_map_altitude_panel, true);
        return;
    }

    char label[24]{};
    (void)format_map_altitude_label(label, sizeof(label));
    set_compact_label(s_map_altitude_label, label);
    const bool profile_open = s_route_elevation_profile_visible &&
                              s_track_overlay_active &&
                              s_track_overlay_kind == TrackOverlayFileKind::Route;
    const lv_coord_t bottom_offset =
        -(kMapControlBarHeight + 4 + (profile_open ? kRouteElevationPanelHeight + 4 : 0));
    lv_obj_align(s_map_altitude_panel, LV_ALIGN_BOTTOM_LEFT, 4, bottom_offset);
    set_hidden(s_map_altitude_panel, false);
    lv_obj_move_foreground(s_map_altitude_panel);
}

void toggle_map_info_visibility()
{
    s_map_info_visible = !s_map_info_visible;
    sync_map_chrome_visibility();
    refresh_view();
}

bool help_uses_f1()
{
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return true;
#else
    return false;
#endif
}

const char* help_key_label()
{
    return help_uses_f1() ? "F1" : "H";
}

const char* tracker_button_label()
{
    return "Track";
}

lv_coord_t tracker_button_width()
{
    return kMapControlButtonTrackerWidth;
}

bool is_help_key(uint32_t key)
{
    if (help_uses_f1())
    {
        return key == kLvglFunctionKeyF1;
    }
    return key == 'h' || key == 'H';
}

void bind_map_key_handler(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }
    lv_obj_remove_event_cb(obj, root_key_event_cb);
    lv_obj_add_event_cb(obj, root_key_event_cb, LV_EVENT_KEY, nullptr);
}

void rebuild_map_control_group()
{
    if (!app_g || (s_map_help_modal && lv_obj_is_valid(s_map_help_modal)) ||
        (s_tracker_modal && lv_obj_is_valid(s_tracker_modal)))
    {
        return;
    }

    lv_group_remove_all_objs(app_g);
    if (s_top_bar.back_btn)
    {
        lv_group_add_obj(app_g, s_top_bar.back_btn);
    }
    add_map_controls_to_group(app_g);
}

bool route_context_available()
{
    const auto& config = app::configFacade().readConfig();
    return config.route_enabled && config.route_path[0] != '\0';
}

std::string route_file_path_for_name(const std::string& route_name)
{
    return std::string(platform::ui::route_storage::route_dir()) + "/" + route_name;
}

std::string route_path_basename(const std::string& path)
{
    const char* base = std::strrchr(path.c_str(), '/');
    if (base && base[1] != '\0')
    {
        return std::string(base + 1);
    }
    return path;
}

std::string route_asset_id_for_path(const std::string& path)
{
    const std::string name = route_path_basename(path);
    std::uint32_t hash = 2166136261U;
    for (unsigned char ch : name)
    {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619U;
    }
    char text[16];
    std::snprintf(text, sizeof(text), "kml-%08lx", static_cast<unsigned long>(hash));
    return std::string(text);
}

std::string route_asset_root_for_id(const std::string& asset_id)
{
    return std::string(platform::ui::route_storage::route_dir()) + "/.trailmate/" + asset_id;
}

bool route_path_looks_degraded(const std::string& path)
{
    return path.find('?') != std::string::npos;
}

bool resolve_configured_route_path(std::string& out_path, bool show_fail_toast)
{
    auto& config_facade = app::configFacade();
    const auto& config = config_facade.readConfig();
    if (!config.route_enabled || config.route_path[0] == '\0')
    {
        out_path.clear();
        return false;
    }

    out_path = config.route_path;
    if (!route_path_looks_degraded(out_path))
    {
        return true;
    }

    std::vector<std::string> routes;
    if (platform::ui::device::sd_ready() &&
        platform::ui::route_storage::list_routes(routes, 2) &&
        routes.size() == 1)
    {
        out_path = route_file_path_for_name(routes.front());
        auto edit = config_facade.beginConfigEdit();
        if (!edit)
        {
            return false;
        }
        std::strncpy(edit.config().route_path,
                     out_path.c_str(),
                     sizeof(edit.config().route_path) - 1);
        edit.config().route_path[sizeof(edit.config().route_path) - 1] = '\0';
        edit.config().route_enabled = true;
        edit.commit(app::AppConfigChangeSet::route());
        return true;
    }

    if (show_fail_toast)
    {
        show_toast("Route path invalid", 1500);
    }
    return false;
}

bool load_configured_route_overlay(bool show_fail_toast)
{
    std::string route_path;
    if (!resolve_configured_route_path(route_path, show_fail_toast))
    {
        return false;
    }
    if (s_track_overlay_active &&
        s_track_overlay_kind == TrackOverlayFileKind::Route &&
        s_track_file == route_path)
    {
        return true;
    }
    return load_map_track_file_impl(route_path.c_str(), show_fail_toast);
}

double degrees_to_radians(double degrees)
{
    return degrees * 0.017453292519943295;
}

double route_point_distance_m(const TrackOverlayPoint& a, const TrackOverlayPoint& b)
{
    const double lat1 = degrees_to_radians(a.lat);
    const double lat2 = degrees_to_radians(b.lat);
    const double dlat = degrees_to_radians(b.lat - a.lat);
    const double dlon = degrees_to_radians(b.lon - a.lon);
    const double sin_lat = std::sin(dlat / 2.0);
    const double sin_lon = std::sin(dlon / 2.0);
    const double h = (sin_lat * sin_lat) +
                     std::cos(lat1) * std::cos(lat2) * (sin_lon * sin_lon);
    return 2.0 * kEarthRadiusM * std::atan2(std::sqrt(h), std::sqrt(std::max(0.0, 1.0 - h)));
}

bool current_fix_lat_lon(double& lat, double& lon)
{
    const auto snapshot = gps_status_model().snapshot();
    if (!snapshot.header.valid ||
        !snapshot.fix_valid ||
        !std::isfinite(snapshot.latitude) ||
        !std::isfinite(snapshot.longitude))
    {
        return false;
    }

    lat = snapshot.latitude;
    lon = snapshot.longitude;
    return true;
}

bool route_deviation_mode_active()
{
    return s_track_overlay_active &&
           s_track_overlay_kind == TrackOverlayFileKind::Route &&
           !s_track_points.empty();
}

void project_route_point_to_local_m(double ref_lat,
                                    double ref_lon,
                                    const TrackOverlayPoint& point,
                                    double& x_m,
                                    double& y_m)
{
    const double ref_lat_rad = degrees_to_radians(ref_lat);
    x_m = degrees_to_radians(point.lon - ref_lon) *
          kEarthRadiusM *
          std::cos(ref_lat_rad);
    y_m = degrees_to_radians(point.lat - ref_lat) * kEarthRadiusM;
}

double distance_to_segment_m(double ax,
                             double ay,
                             double bx,
                             double by)
{
    const double vx = bx - ax;
    const double vy = by - ay;
    const double segment_len2 = (vx * vx) + (vy * vy);
    if (segment_len2 <= 0.000001)
    {
        return std::sqrt((ax * ax) + (ay * ay));
    }

    const double t = std::max(0.0, std::min(1.0, -((ax * vx) + (ay * vy)) / segment_len2));
    const double nearest_x = ax + (t * vx);
    const double nearest_y = ay + (t * vy);
    return std::sqrt((nearest_x * nearest_x) + (nearest_y * nearest_y));
}

bool nearest_route_distance_m(double self_lat,
                              double self_lon,
                              double& out_distance_m)
{
    if (!route_deviation_mode_active())
    {
        return false;
    }

    double first_x = 0.0;
    double first_y = 0.0;
    project_route_point_to_local_m(self_lat, self_lon, s_track_points.front(), first_x, first_y);
    out_distance_m = std::sqrt((first_x * first_x) + (first_y * first_y));

    if (s_track_points.size() == 1)
    {
        return std::isfinite(out_distance_m);
    }

    double prev_x = first_x;
    double prev_y = first_y;
    for (std::size_t index = 1; index < s_track_points.size(); ++index)
    {
        double next_x = 0.0;
        double next_y = 0.0;
        project_route_point_to_local_m(self_lat, self_lon, s_track_points[index], next_x, next_y);
        const double distance = distance_to_segment_m(prev_x, prev_y, next_x, next_y);
        if (std::isfinite(distance))
        {
            out_distance_m = std::min(out_distance_m, distance);
        }
        prev_x = next_x;
        prev_y = next_y;
    }
    return std::isfinite(out_distance_m);
}

void update_route_deviation_state()
{
    if (!route_deviation_mode_active())
    {
        s_route_deviation_active = false;
        s_route_deviation_distance_m = 0.0;
        return;
    }

    double self_lat = 0.0;
    double self_lon = 0.0;
    double distance_m = 0.0;
    if (!current_fix_lat_lon(self_lat, self_lon) ||
        !nearest_route_distance_m(self_lat, self_lon, distance_m))
    {
        s_route_deviation_active = false;
        s_route_deviation_distance_m = 0.0;
        return;
    }

    s_route_deviation_distance_m = distance_m;
    if (s_route_deviation_active)
    {
        s_route_deviation_active = distance_m > kRouteDeviationClearThresholdM;
    }
    else
    {
        s_route_deviation_active = distance_m > kRouteDeviationThresholdM;
    }
}

void update_track_elevation_metrics(TrackElevationMetrics& metrics,
                                    double altitude_m,
                                    bool has_altitude)
{
    if (!has_altitude || !std::isfinite(altitude_m))
    {
        return;
    }

    if (metrics.altitude_count == 0)
    {
        metrics.min_altitude_m = altitude_m;
        metrics.max_altitude_m = altitude_m;
    }
    else
    {
        metrics.min_altitude_m = std::min(metrics.min_altitude_m, altitude_m);
        metrics.max_altitude_m = std::max(metrics.max_altitude_m, altitude_m);
    }

    if (metrics.has_previous_altitude)
    {
        const double delta = altitude_m - metrics.previous_altitude_m;
        if (delta > 0.0)
        {
            metrics.ascent_m += delta;
        }
        else
        {
            metrics.descent_m -= delta;
        }
    }

    metrics.previous_altitude_m = altitude_m;
    metrics.has_previous_altitude = true;
    ++metrics.altitude_count;
}

bool route_elevation_profile_available()
{
    if (!s_track_overlay_active ||
        s_track_overlay_kind != TrackOverlayFileKind::Route)
    {
        return false;
    }
    return s_track_elevation_metrics.altitude_count >= 2;
}

::ui::widgets::route_elevation_profile::Config route_elevation_profile_config()
{
    ::ui::widgets::route_elevation_profile::Config config{};
    config.height = kRouteElevationPanelHeight;
    config.inset = kRouteElevationPanelInset;
    return config;
}

::ui::widgets::route_elevation_profile::Metrics route_elevation_profile_metrics(double distance_m)
{
    ::ui::widgets::route_elevation_profile::Metrics metrics{};
    metrics.altitude_count = s_track_elevation_metrics.altitude_count;
    metrics.min_altitude_m = s_track_elevation_metrics.min_altitude_m;
    metrics.max_altitude_m = s_track_elevation_metrics.max_altitude_m;
    metrics.ascent_m = s_track_elevation_metrics.ascent_m;
    metrics.descent_m = s_track_elevation_metrics.descent_m;
    metrics.distance_m = distance_m;
    return metrics;
}

void sync_route_elevation_profile()
{
    if (!s_route_elevation_profile.panel ||
        !lv_obj_is_valid(s_route_elevation_profile.panel))
    {
        return;
    }

    if (!s_map_info_visible || !s_route_elevation_profile_visible ||
        !route_elevation_profile_available())
    {
        ::ui::widgets::route_elevation_profile::set_hidden(s_route_elevation_profile, true);
        return;
    }
    if (!s_map_viewport || !lv_obj_is_valid(s_map_viewport))
    {
        ::ui::widgets::route_elevation_profile::set_hidden(s_route_elevation_profile, true);
        return;
    }

    const auto* samples = &s_route_elevation_samples;
    if (samples->empty())
    {
        s_route_elevation_work_samples.clear();
        build_route_elevation_samples(s_track_points, s_route_elevation_work_samples);
        samples = &s_route_elevation_work_samples;
    }
    double distance_m = 0.0;
    if (!samples->empty())
    {
        distance_m = samples->back().distance_m;
        if (!std::isfinite(distance_m))
        {
            distance_m = 0.0;
        }
    }

    const auto metrics = route_elevation_profile_metrics(distance_m);
    (void)::ui::widgets::route_elevation_profile::update(
        s_route_elevation_profile,
        route_elevation_profile_config(),
        samples->data(),
        samples->size(),
        metrics,
        true,
        kMapControlBarHeight + kRouteElevationPanelInset);
}

void toggle_route_elevation_profile()
{
    if (!s_route_elevation_profile_visible)
    {
        if (!s_track_overlay_active ||
            s_track_overlay_kind != TrackOverlayFileKind::Route)
        {
            if (!load_configured_route_overlay(true))
            {
                set_map_notice("No route", 1200);
                request_refresh_view();
                return;
            }
        }
        if (!route_elevation_profile_available())
        {
            set_map_notice("No route altitude", 1500);
            request_refresh_view();
            return;
        }
    }

    s_route_elevation_profile_visible = !s_route_elevation_profile_visible;
    set_map_notice(s_route_elevation_profile_visible ? "Elevation shown" : "Elevation hidden", 900);
    request_refresh_view();
}

bool route_image_context_active()
{
    return s_track_overlay_active &&
           s_track_overlay_kind == TrackOverlayFileKind::Route &&
           !s_route_images.empty();
}

::ui::widgets::route_image_strip::Config map_route_image_strip_config()
{
    ::ui::widgets::route_image_strip::Config config{};
    config.width = 200;
    config.item_height = ::ui::page_profile::current().dense ? 104 : 120;
    return config;
}

uint32_t map_route_image_hash_byte(uint32_t hash, uint8_t value)
{
    hash ^= value;
    hash *= 16777619U;
    return hash;
}

uint32_t map_route_image_hash_string(uint32_t hash, const std::string& value)
{
    for (unsigned char ch : value)
    {
        hash = map_route_image_hash_byte(hash, ch);
    }
    return map_route_image_hash_byte(hash, 0);
}

uint32_t map_route_image_strip_items_hash()
{
    uint32_t hash = 2166136261U;
    hash = map_route_image_hash_string(hash, s_route_asset_id);
    for (const auto& image : s_route_images)
    {
        hash = map_route_image_hash_byte(hash, image.downloaded ? 1U : 0U);
        hash = map_route_image_hash_byte(hash, image.preview_ready ? 1U : 0U);
        hash = map_route_image_hash_byte(hash, image.view_ready ? 1U : 0U);
    }
    return hash;
}

void rebuild_map_route_image_strip_items()
{
    s_route_image_strip_items.clear();
    s_route_image_strip_items.reserve(s_route_images.size());
    for (const auto& image : s_route_images)
    {
        ::ui::widgets::route_image_strip::Item item{};
        item.local_path = image.local_path;
        item.preview_path = image.preview_ready ? image.preview_path : std::string{};
        item.view_path = image.view_ready ? image.view_path : std::string{};
        item.downloaded = image.downloaded;
        s_route_image_strip_items.push_back(std::move(item));
    }
}

void center_map_on_route_image(const RouteImagePoint& image)
{
    if (!image.has_position)
    {
        return;
    }

    auto& model = map_workspace_model();
    auto viewport = model.viewport();
    viewport.center_lat = image.lat;
    viewport.center_lon = image.lon;
    (void)model.setViewport(viewport);
    s_map_pan_x = 0;
    s_map_pan_y = 0;
    sync_workspace_viewport_from_renderer();
}

void on_map_route_image_strip_selected(std::size_t index, void*)
{
    if (index >= s_route_images.size())
    {
        return;
    }
    s_route_selected_image = index;
    center_map_on_route_image(s_route_images[index]);
    request_refresh_view();
}

void maybe_refresh_finished_route_image_cache()
{
    if (!s_route_image_cache_build_running || s_route_asset_id.empty())
    {
        return;
    }
    const auto status = platform::ui::route_storage::route_image_download_status();
    if (status.asset_id != s_route_asset_id || status.busy)
    {
        return;
    }
    if (status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Done ||
        status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Failed)
    {
        refresh_route_image_storage_state(true);
        s_route_image_cache_build_running = false;
        s_route_image_strip_items_hash = 0;
    }
}

void ensure_map_route_image_cache_build()
{
    if (s_route_asset_id.empty() || s_route_images.empty())
    {
        return;
    }
    if (!s_route_image_cache_state_known)
    {
        refresh_route_image_storage_state(true);
    }
    const auto status = platform::ui::route_storage::route_image_download_status();
    if (status.busy)
    {
        return;
    }

    std::vector<platform::ui::route_storage::RouteImageCacheItem> items;
    items.reserve(s_route_images.size());
    for (const auto& image : s_route_images)
    {
        if (!image.downloaded || (image.preview_ready && image.view_ready))
        {
            continue;
        }
        platform::ui::route_storage::RouteImageCacheItem item{};
        item.source_path = image.local_path;
        item.preview_path = image.preview_path;
        item.view_path = image.view_path;
        items.push_back(std::move(item));
    }
    if (items.empty())
    {
        return;
    }

    std::string error;
    if (platform::ui::route_storage::start_route_image_cache_build(
            s_route_asset_id,
            items,
            error,
            platform::ui::route_storage::RouteImageTaskPresentation::PageOnly))
    {
        s_route_image_cache_build_running = true;
    }
}

void sync_map_route_image_strip()
{
    maybe_refresh_finished_route_image_cache();
    if (!s_map_viewport || !lv_obj_is_valid(s_map_viewport) ||
        !route_image_context_active())
    {
        s_route_image_strip_visible = false;
        ::ui::widgets::route_image_strip::destroy(s_route_image_strip);
        s_route_image_strip_items_hash = 0;
        return;
    }

    if (!s_route_image_strip_visible && !s_route_image_strip.root)
    {
        return;
    }

    ::ui::widgets::route_image_strip::create(
        s_map_viewport,
        s_route_image_strip,
        map_route_image_strip_config());
    ::ui::widgets::route_image_strip::set_selection_callback(
        s_route_image_strip,
        on_map_route_image_strip_selected,
        nullptr);
    bind_map_key_handler(s_route_image_strip.root);

    const uint32_t next_items_hash = map_route_image_strip_items_hash();
    if (next_items_hash != s_route_image_strip_items_hash ||
        s_route_image_strip.items.empty())
    {
        rebuild_map_route_image_strip_items();
        ::ui::widgets::route_image_strip::set_items(
            s_route_image_strip,
            s_route_image_strip_items.data(),
            s_route_image_strip_items.size());
        s_route_image_strip_items_hash = next_items_hash;
    }

    const std::size_t selected =
        s_route_images.empty()
            ? 0
            : std::min<std::size_t>(s_route_selected_image, s_route_images.size() - 1);
    ::ui::widgets::route_image_strip::set_selected(s_route_image_strip, selected, false);
    ::ui::widgets::route_image_strip::set_hidden(
        s_route_image_strip,
        !s_route_image_strip_visible);
}

void toggle_map_route_image_strip()
{
    if (!s_track_overlay_active ||
        s_track_overlay_kind != TrackOverlayFileKind::Route)
    {
        if (!load_configured_route_overlay(true))
        {
            set_map_notice("No route", 1200);
            request_refresh_view();
            return;
        }
    }
    if (s_route_images.empty())
    {
        set_map_notice("No route images", 1200);
        s_route_image_strip_visible = false;
        sync_map_route_image_strip();
        request_refresh_view();
        return;
    }

    s_route_image_strip_visible = !s_route_image_strip_visible;
    if (s_route_image_strip_visible)
    {
        refresh_route_image_storage_state(true);
        ensure_map_route_image_cache_build();
    }
    set_map_notice(s_route_image_strip_visible ? "Images shown" : "Images hidden", 900);
    sync_map_route_image_strip();
    request_refresh_view();
}

bool load_team_snapshot(::team::ui::TeamUiSnapshot& out)
{
    return ::team::ui::team_ui_snapshot_store().load(out) &&
           out.in_team &&
           out.has_team_id;
}

uint32_t member_id_for_button(const ::team::ui::TeamMemberUi& member)
{
    if (member.node_id != 0)
    {
        return member.node_id;
    }
    return app::messagingFacade().getSelfNodeId();
}

uint32_t member_color(const ::team::ui::TeamMemberUi& member)
{
    uint8_t color_index = member.color_index;
    if (color_index >= ::team::ui::kTeamMaxMembers)
    {
        color_index = ::team::ui::team_color_index_from_node_id(member_id_for_button(member));
    }
    return ::team::ui::team_color_from_index(color_index);
}

std::string member_label(const ::team::ui::TeamMemberUi& member)
{
    if (member.node_id == 0)
    {
        return member.name.empty() ? std::string("You") : member.name;
    }
    return ::ui::team_presentation::shortTeamMemberLabel(
        member_id_for_button(member));
}

uint32_t hash_member_list(const ::team::ui::TeamUiSnapshot& snapshot)
{
    uint32_t hash = 2166136261U;
    auto mix = [&](uint32_t value)
    {
        hash ^= value;
        hash *= 16777619U;
    };

    mix(static_cast<uint32_t>(snapshot.members.size()));
    for (const auto& member : snapshot.members)
    {
        const uint32_t member_id = member_id_for_button(member);
        mix(member_id);
        mix(member.color_index);
        for (char ch : member.name)
        {
            mix(static_cast<uint8_t>(ch));
        }
    }
    return hash;
}

bool member_exists(const ::team::ui::TeamUiSnapshot& snapshot, uint32_t member_id)
{
    for (const auto& member : snapshot.members)
    {
        if (member_id_for_button(member) == member_id)
        {
            return true;
        }
    }
    return false;
}

void style_member_button_selected(lv_obj_t* btn, bool selected)
{
    if (!btn || !lv_obj_is_valid(btn))
    {
        return;
    }
    lv_obj_set_style_outline_width(btn, selected ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_outline_color(btn, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(btn, 0, LV_PART_MAIN);
}

void update_member_button_states()
{
    for (std::size_t index = 0; index < s_member_buttons.size(); ++index)
    {
        const bool selected = index < s_member_button_ids.size() &&
                              s_member_button_ids[index] == s_selected_member_id;
        style_member_button_selected(s_member_buttons[index], selected);
    }
}

void clear_member_buttons()
{
    for (lv_obj_t* btn : s_member_buttons)
    {
        if (btn && lv_obj_is_valid(btn))
        {
            lv_obj_del(btn);
        }
    }
    s_member_buttons.clear();
    s_member_button_ids.clear();
}

void select_member(uint32_t member_id)
{
    if (member_id == 0 || member_id == kInvalidMemberId)
    {
        return;
    }

    s_selected_member_id = member_id;
    update_member_button_states();

    ::team::ui::TeamUiSnapshot snapshot;
    if (load_team_snapshot(snapshot))
    {
        std::string track_path;
        if (::team::ui::team_ui_get_member_track_path(snapshot.team_id, member_id, track_path))
        {
            (void)load_map_track_file_impl(track_path.c_str(), true);
        }
    }

    char text[40]{};
    std::snprintf(text,
                  sizeof(text),
                  "Member %04lX",
                  static_cast<unsigned long>(member_id & 0xFFFFU));
    set_map_notice(text, 1200);
    request_refresh_view();
}

void member_button_event_cb(lv_event_t* e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(e);
        if (key != LV_KEY_ENTER)
        {
            return;
        }
        consume_key_event(e);
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    const uint32_t member_id = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    select_member(member_id);
}

lv_obj_t* create_member_button(const ::team::ui::TeamMemberUi& member)
{
    if (!s_map_context_rail || !lv_obj_is_valid(s_map_context_rail))
    {
        return nullptr;
    }

    const uint32_t member_id = member_id_for_button(member);
    lv_obj_t* btn = create_map_control_button(s_map_context_rail,
                                              kMapSideRailWidth - 10,
                                              "",
                                              MapControlAction::TeamMember);
    lv_obj_set_height(btn, kMapControlButtonHeight);
    lv_obj_set_style_pad_left(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 3, LV_PART_MAIN);
    lv_obj_add_event_cb(btn,
                        member_button_event_cb,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(member_id)));
    lv_obj_add_event_cb(btn,
                        member_button_event_cb,
                        LV_EVENT_KEY,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(member_id)));

    lv_obj_t* dot = lv_obj_create(btn);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(member_color(member)), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_obj_get_child(btn, 0);
    const std::string text = member_label(member);
    if (label && lv_obj_is_valid(label))
    {
        lv_label_set_text(label, text.c_str());
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_pad_left(label, 12, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }

    return btn;
}

void sync_map_notice_overlay()
{
    if (!s_map_notice_panel || !lv_obj_is_valid(s_map_notice_panel) ||
        !s_map_notice_label || !lv_obj_is_valid(s_map_notice_label))
    {
        return;
    }

    if (!s_map_info_visible)
    {
        set_hidden(s_map_notice_panel, true);
        return;
    }

    const uint32_t now = sys::millis_now();
    if (s_map_notice_text[0] != '\0' && now < s_map_notice_until_ms)
    {
        set_compact_label(s_map_notice_label, s_map_notice_text);
        lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x25170D), 0);
        lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_map_notice_panel);
        return;
    }

    s_map_notice_text[0] = '\0';
    s_map_notice_until_ms = 0;
    if (s_route_deviation_active && std::isfinite(s_route_deviation_distance_m))
    {
        char notice[40]{};
        std::snprintf(notice,
                      sizeof(notice),
                      "Off route %.0fm",
                      s_route_deviation_distance_m);
        set_compact_label(s_map_notice_label, notice);
        lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x8F2E1D), 0);
        lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_map_notice_panel);
        return;
    }
    if (s_map_target)
    {
        char notice[64]{};
        double lat = 0.0, lon = 0.0;
        if (current_fix_lat_lon(lat, lon))
        {
            const double target_lat = s_map_target->latitude_e7 / 10000000.0;
            const double target_lon = s_map_target->longitude_e7 / 10000000.0;
            const double meters = ::ui::menu::dashboard::haversine_m(lat, lon, target_lat, target_lon);
            const float bearing = ::ui::menu::dashboard::bearing_between(lat, lon, target_lat, target_lon);
            char distance[20]{};
            ::ui::menu::dashboard::format_distance(meters, distance, sizeof(distance));
            if (meters < 1.0) std::snprintf(notice, sizeof(notice), "Cache: <1 m (straight line)");
            else std::snprintf(notice, sizeof(notice), "Cache: %s  %s %.0f deg N", distance,
                               ::ui::menu::dashboard::compass_rose(bearing), static_cast<double>(bearing));
        }
        else std::snprintf(notice, sizeof(notice), "Cache: waiting for GPS fix");
        set_compact_label(s_map_notice_label, notice);
        lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x25170D), 0);
        lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_map_notice_panel);
        return;
    }
    lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x25170D), 0);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
}

void sync_map_context_buttons(const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    if (s_location_request) return;
    if (!s_map_info_visible)
    {
        set_hidden(s_map_route_btn, true);
        set_hidden(s_map_context_rail, true);
        (void)snapshot;
        return;
    }

    const bool show_route = route_context_available();
    ::team::ui::TeamUiSnapshot team_snapshot;
    const bool has_team_members = load_team_snapshot(team_snapshot) && !team_snapshot.members.empty();
    const uint8_t next_mask = static_cast<uint8_t>((show_route ? 0x01 : 0x00) |
                                                   (has_team_members ? 0x02 : 0x00));
    const uint32_t now_ms = sys::millis_now();
    bool group_dirty = next_mask != s_map_context_mask;

    set_hidden(s_map_route_btn, !show_route);
    set_hidden(s_map_context_rail, next_mask == 0);

    if (has_team_members)
    {
        const uint32_t hash = hash_member_list(team_snapshot);
        const bool refresh_due = (now_ms - s_member_panel_last_ms) >= 2000U;
        if (hash != s_member_list_hash ||
            team_snapshot.members.size() != s_member_buttons.size() ||
            refresh_due)
        {
            s_member_panel_last_ms = now_ms;
            if (hash != s_member_list_hash ||
                team_snapshot.members.size() != s_member_buttons.size())
            {
                clear_member_buttons();
                s_member_buttons.reserve(team_snapshot.members.size());
                s_member_button_ids.reserve(team_snapshot.members.size());
                for (const auto& member : team_snapshot.members)
                {
                    const uint32_t member_id = member_id_for_button(member);
                    lv_obj_t* btn = create_member_button(member);
                    if (!btn)
                    {
                        continue;
                    }
                    s_member_buttons.push_back(btn);
                    s_member_button_ids.push_back(member_id);
                }
                s_member_list_hash = hash;
                group_dirty = true;
            }
        }

        if (!member_exists(team_snapshot, s_selected_member_id))
        {
            s_selected_member_id = kInvalidMemberId;
        }
        update_member_button_states();
    }
    else if (!s_member_buttons.empty() || s_member_list_hash != 0)
    {
        clear_member_buttons();
        s_member_list_hash = 0;
        s_selected_member_id = kInvalidMemberId;
        group_dirty = true;
    }

    if (group_dirty)
    {
        s_map_context_mask = next_mask;
        rebuild_map_control_group();
    }
    (void)snapshot;
}

void sync_map_control_labels(const ::ui::map::MapWorkspaceSnapshot& snapshot)
{
    if (!s_map_control_bar || !lv_obj_is_valid(s_map_control_bar))
    {
        return;
    }

    if (s_location_request)
    {
        char zoom_buf[8]{};
        std::snprintf(zoom_buf, sizeof(zoom_buf), "Z%d", static_cast<int>(current_map_zoom()));
        set_compact_label(s_map_zoom_label, zoom_buf);
        return;
    }

    if (!s_map_info_visible)
    {
        set_hidden(s_map_control_bar, true);
        sync_route_elevation_profile();
        sync_map_altitude_overlay();
        sync_map_context_buttons(snapshot);
        sync_map_notice_overlay();
        return;
    }

    set_hidden(s_map_control_bar, false);
    sync_route_elevation_profile();
    sync_map_altitude_overlay();

    const auto layers = ::ui::widgets::map::current_layer_state();
    set_button_label(s_map_layer_btn, compact_map_source_label(layers.map_source));
    set_button_label(s_map_contour_btn, layers.contour_enabled ? "Contour*" : "Contour");
    sync_map_context_buttons(snapshot);

    char zoom_buf[8]{};
    std::snprintf(zoom_buf, sizeof(zoom_buf), "Z%d", static_cast<int>(current_map_zoom()));
    set_compact_label(s_map_zoom_label, zoom_buf);

    uint8_t missing_source = 0;
    if (::ui::widgets::map::take_missing_tile_notice(s_map_runtime, &missing_source))
    {
        char notice[48]{};
        std::snprintf(notice,
                      sizeof(notice),
                      "%s tile loading",
                      compact_map_source_label(missing_source));
        set_map_notice(notice, 1400);
    }

    sync_map_notice_overlay();
}

lv_obj_t* create_status_row(lv_obj_t* parent, const char* title, lv_obj_t** out_value)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 18);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* key = lv_label_create(row);
    lv_label_set_text(key, title);
    lv_obj_set_width(key, 76);
    lv_obj_set_style_text_font(key, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(key, lv_color_hex(0x6D5B43), 0);
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);

    lv_obj_t* value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_width(value, 0);
    lv_obj_set_flex_grow(value, 1);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x1D160F), 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);

    if (out_value)
    {
        *out_value = value;
    }
    return row;
}

void refresh_gps_status_view()
{
    if (!s_root || s_projection != Projection::GpsStatus)
    {
        return;
    }

    ui_update_top_bar_battery(s_top_bar);

    const auto snapshot = gps_status_model().snapshot();
    if (!snapshot.header.valid)
    {
        set_compact_label(s_gps_status_label, "Unavailable");
        set_compact_label(s_gps_coord_label, "--");
        set_compact_label(s_gps_sat_label, "--");
        set_compact_label(s_gps_alt_label, "--");
        set_compact_label(s_gps_motion_label, "--");
        set_compact_label(s_gps_time_label, "--");
        set_compact_label(s_gps_diag_label, "No status source");
        return;
    }

    set_compact_label(s_gps_status_label, snapshot.fix_label.c_str());
    set_compact_label(s_gps_coord_label, snapshot.coordinate_label.c_str());
    set_compact_label(s_gps_sat_label, snapshot.satellite_label.c_str());

    char line[48]{};
    if (snapshot.fix_valid && snapshot.has_altitude && std::isfinite(snapshot.altitude_m))
    {
        std::snprintf(line, sizeof(line), "%.0f m", static_cast<double>(snapshot.altitude_m));
    }
    else
    {
        std::snprintf(line, sizeof(line), "--");
    }
    set_compact_label(s_gps_alt_label, line);

    if (snapshot.fix_valid)
    {
        std::snprintf(line,
                      sizeof(line),
                      "%.1f m/s %.0f deg",
                      static_cast<double>(snapshot.speed_mps),
                      static_cast<double>(snapshot.course_deg));
    }
    else
    {
        std::snprintf(line, sizeof(line), "--");
    }
    set_compact_label(s_gps_motion_label, line);
    set_compact_label(s_gps_time_label, snapshot.time_label.c_str());
    set_compact_label(s_gps_diag_label, diagnostic_label());
}

std::string trim_copy(std::string value)
{
    const auto is_space = [](unsigned char ch)
    {
        return std::isspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch)
                                            { return !is_space(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch)
                             { return !is_space(static_cast<unsigned char>(ch)); })
                    .base(),
                value.end());
    return value;
}

bool ends_with_ignore_case(const std::string& value, const char* suffix)
{
    if (!suffix)
    {
        return false;
    }
    const std::size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len)
    {
        return false;
    }

    const std::size_t start = value.size() - suffix_len;
    for (std::size_t index = 0; index < suffix_len; ++index)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[start + index]);
        const unsigned char rhs = static_cast<unsigned char>(suffix[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
        {
            return false;
        }
    }
    return true;
}

bool parse_double_token(const std::string& token, double& out)
{
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str();
}

bool parse_attr_double(const std::string& line, const char* key, double& out)
{
    return key && ::gps::gpx::readDoubleAttribute(line, key, out);
}

bool ascii_equal_ignore_case(const std::string& value, const char* expected)
{
    if (!expected)
    {
        return false;
    }
    const std::size_t expected_len = std::strlen(expected);
    if (value.size() != expected_len)
    {
        return false;
    }
    for (std::size_t index = 0; index < expected_len; ++index)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[index]);
        const unsigned char rhs = static_cast<unsigned char>(expected[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
        {
            return false;
        }
    }
    return true;
}

bool kml_tag_name_matches(const std::string& tag, const char* expected, bool& closing)
{
    closing = false;
    std::size_t index = 0;
    while (index < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[index])))
    {
        ++index;
    }
    if (index < tag.size() && tag[index] == '/')
    {
        closing = true;
        ++index;
    }
    while (index < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[index])))
    {
        ++index;
    }

    const std::size_t name_start = index;
    while (index < tag.size())
    {
        const char ch = tag[index];
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == '/' || ch == '>')
        {
            break;
        }
        ++index;
    }
    if (index <= name_start)
    {
        return false;
    }

    std::string name = tag.substr(name_start, index - name_start);
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos && colon + 1 < name.size())
    {
        name.erase(0, colon + 1);
    }
    return ascii_equal_ignore_case(name, expected);
}

bool parse_kml_coordinate_token(const std::string& token,
                                double& lat,
                                double& lon,
                                double& altitude_m,
                                bool& has_altitude)
{
    has_altitude = false;
    const std::size_t comma1 = token.find(',');
    if (comma1 == std::string::npos || comma1 == 0)
    {
        return false;
    }

    const std::size_t comma2 = token.find(',', comma1 + 1);
    const std::string lon_token = token.substr(0, comma1);
    const std::string lat_token = comma2 == std::string::npos
                                      ? token.substr(comma1 + 1)
                                      : token.substr(comma1 + 1, comma2 - comma1 - 1);
    if (lat_token.empty())
    {
        return false;
    }
    if (!parse_double_token(lon_token, lon) ||
        !parse_double_token(lat_token, lat))
    {
        return false;
    }

    if (comma2 != std::string::npos && comma2 + 1 < token.size())
    {
        double parsed_altitude = 0.0;
        if (parse_double_token(token.substr(comma2 + 1), parsed_altitude))
        {
            altitude_m = parsed_altitude;
            has_altitude = true;
        }
    }
    return true;
}

bool parse_kml_gx_coord_token(const std::string& token,
                              double& lat,
                              double& lon,
                              double& altitude_m,
                              bool& has_altitude)
{
    has_altitude = false;
    const std::string value = trim_copy(token);
    std::size_t cursor = 0;
    auto next_token = [&](std::string& out)
    {
        while (cursor < value.size() &&
               std::isspace(static_cast<unsigned char>(value[cursor])))
        {
            ++cursor;
        }
        const std::size_t start = cursor;
        while (cursor < value.size() &&
               !std::isspace(static_cast<unsigned char>(value[cursor])))
        {
            ++cursor;
        }
        if (cursor <= start)
        {
            return false;
        }
        out = value.substr(start, cursor - start);
        return true;
    };

    std::string lon_token;
    std::string lat_token;
    std::string altitude_token;
    if (!next_token(lon_token) || !next_token(lat_token))
    {
        return false;
    }
    if (!parse_double_token(lon_token, lon) ||
        !parse_double_token(lat_token, lat))
    {
        return false;
    }
    if (next_token(altitude_token))
    {
        double parsed_altitude = 0.0;
        if (parse_double_token(altitude_token, parsed_altitude))
        {
            altitude_m = parsed_altitude;
            has_altitude = true;
        }
    }
    return true;
}

std::size_t find_case_insensitive(const std::string& text,
                                  const char* needle,
                                  std::size_t start = 0)
{
    if (!needle || needle[0] == '\0')
    {
        return std::string::npos;
    }
    const std::size_t needle_len = std::strlen(needle);
    if (needle_len > text.size())
    {
        return std::string::npos;
    }
    for (std::size_t pos = start; pos + needle_len <= text.size(); ++pos)
    {
        bool match = true;
        for (std::size_t index = 0; index < needle_len; ++index)
        {
            const unsigned char lhs = static_cast<unsigned char>(text[pos + index]);
            const unsigned char rhs = static_cast<unsigned char>(needle[index]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return pos;
        }
    }
    return std::string::npos;
}

std::string html_decode_attr(std::string value)
{
    auto replace_all = [&value](const char* from, const char* to)
    {
        std::size_t pos = 0;
        const std::size_t from_len = std::strlen(from);
        while ((pos = value.find(from, pos)) != std::string::npos)
        {
            value.replace(pos, from_len, to);
            pos += std::strlen(to);
        }
    };
    replace_all("&amp;", "&");
    replace_all("&quot;", "\"");
    replace_all("&#34;", "\"");
    replace_all("&#38;", "&");
    return value;
}

void extract_img_srcs(const std::string& text, std::vector<std::string>& out_urls)
{
    std::size_t pos = 0;
    while (out_urls.size() < kMaxRouteImagePoints)
    {
        const std::size_t img_pos = find_case_insensitive(text, "<img", pos);
        if (img_pos == std::string::npos)
        {
            break;
        }
        const std::size_t tag_end = text.find('>', img_pos);
        const std::size_t tag_len =
            tag_end == std::string::npos ? text.size() - img_pos : tag_end - img_pos + 1;
        const std::string tag = text.substr(img_pos, tag_len);
        const std::size_t src_pos = find_case_insensitive(tag, "src");
        if (src_pos != std::string::npos)
        {
            std::size_t eq_pos = tag.find('=', src_pos + 3);
            if (eq_pos != std::string::npos)
            {
                ++eq_pos;
                while (eq_pos < tag.size() &&
                       std::isspace(static_cast<unsigned char>(tag[eq_pos])))
                {
                    ++eq_pos;
                }
                if (eq_pos < tag.size())
                {
                    const char quote =
                        tag[eq_pos] == '\'' || tag[eq_pos] == '"' ? tag[eq_pos] : '\0';
                    const std::size_t value_start = quote ? eq_pos + 1 : eq_pos;
                    std::size_t value_end = value_start;
                    while (value_end < tag.size())
                    {
                        const char ch = tag[value_end];
                        if ((quote && ch == quote) ||
                            (!quote &&
                             (std::isspace(static_cast<unsigned char>(ch)) || ch == '>')))
                        {
                            break;
                        }
                        ++value_end;
                    }
                    if (value_end > value_start)
                    {
                        out_urls.push_back(
                            html_decode_attr(tag.substr(value_start, value_end - value_start)));
                    }
                }
            }
        }
        if (tag_end == std::string::npos)
        {
            break;
        }
        pos = tag_end + 1;
    }
}

bool extract_tag_text(const std::string& line, const char* tag, std::string& out_text)
{
    out_text.clear();
    std::string open = "<";
    open += tag;
    const std::size_t open_pos = find_case_insensitive(line, open.c_str());
    if (open_pos == std::string::npos)
    {
        return false;
    }
    const std::size_t body_start = line.find('>', open_pos);
    if (body_start == std::string::npos)
    {
        return false;
    }
    std::string close = "</";
    close += tag;
    close += ">";
    const std::size_t close_pos = find_case_insensitive(line, close.c_str(), body_start + 1);
    if (close_pos == std::string::npos || close_pos <= body_start)
    {
        return false;
    }
    out_text = line.substr(body_start + 1, close_pos - body_start - 1);
    return true;
}

void append_route_images(const std::vector<std::string>& urls,
                         double lat,
                         double lon,
                         bool has_position)
{
    for (const auto& url : urls)
    {
        if (s_route_images.size() >= kMaxRouteImagePoints)
        {
            return;
        }
        if (url.empty())
        {
            continue;
        }
        RouteImagePoint image{};
        image.lat = lat;
        image.lon = lon;
        image.has_position = has_position && std::isfinite(lat) && std::isfinite(lon);
        s_route_images.push_back(std::move(image));
    }
}

void assign_route_image_paths()
{
    if (s_route_asset_id.empty())
    {
        return;
    }
    const std::string asset_root = route_asset_root_for_id(s_route_asset_id);
    for (std::size_t index = 0; index < s_route_images.size(); ++index)
    {
        char local_name[32];
        char preview_name[36];
        char view_name[34];
        const unsigned display_index = static_cast<unsigned>(index + 1);
        std::snprintf(local_name, sizeof(local_name), "/images/img-%04u.jpg", display_index);
        std::snprintf(preview_name, sizeof(preview_name), "/thumbs/thumb-%04u.bmp", display_index);
        std::snprintf(view_name, sizeof(view_name), "/views/view-%04u.bmp", display_index);
        s_route_images[index].local_path = asset_root + local_name;
        s_route_images[index].preview_path = asset_root + preview_name;
        s_route_images[index].view_path = asset_root + view_name;
    }
}

void refresh_route_image_storage_state(bool include_cache_state)
{
    assign_route_image_paths();
    for (RouteImagePoint& image : s_route_images)
    {
        image.downloaded =
            platform::ui::route_storage::route_asset_file_exists(image.local_path);
        image.preview_ready =
            include_cache_state &&
            image.downloaded &&
            platform::ui::route_storage::route_asset_file_exists(image.preview_path);
        image.view_ready =
            include_cache_state &&
            image.downloaded &&
            platform::ui::route_storage::route_asset_file_exists(image.view_path);
    }
    s_route_image_saved_state_known = true;
    s_route_image_cache_state_known = include_cache_state;
}

template <typename ChunkHandler>
bool read_track_file_chunks(const char* path, ChunkHandler on_chunk)
{
    const std::string normalized = ::ui::fs::normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    char buffer[kTrackFileReadBufferBytes];
    bool ok = true;
    while (true)
    {
        uint32_t bytes_read = 0;
        if (lv_fs_read(&file, buffer, sizeof(buffer), &bytes_read) != LV_FS_RES_OK)
        {
            ok = false;
            break;
        }
        if (bytes_read == 0)
        {
            break;
        }

        on_chunk(buffer, bytes_read);
    }

    lv_fs_close(&file);
    return ok;
}

template <typename LineHandler>
bool read_track_file_lines(const char* path, LineHandler on_line)
{
    std::string line;
    bool discard_line = false;
    const bool ok = read_track_file_chunks(path, [&](const char* buffer, uint32_t bytes_read)
                                           {
        for (uint32_t index = 0; index < bytes_read; ++index)
        {
            const char ch = buffer[index];
            if (ch == '\n')
            {
                if (!discard_line)
                {
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    on_line(line);
                }
                line.clear();
                discard_line = false;
                continue;
            }

            if (discard_line)
            {
                continue;
            }
            if (line.size() >= kMaxTrackFileLineBytes)
            {
                line.clear();
                discard_line = true;
                continue;
            }
            line.push_back(ch);
        } });

    if (ok && !discard_line && !line.empty())
    {
        if (line.back() == '\r')
        {
            line.pop_back();
        }
        on_line(line);
    }
    return ok;
}

bool load_kml_route_images(const char* path)
{
    s_route_images.clear();
    s_route_image_strip_items.clear();
    s_route_image_strip_items_hash = 0;
    s_route_selected_image = 0;
    s_route_image_saved_state_known = false;
    s_route_image_cache_state_known = false;
    s_route_image_cache_build_running = false;
    if (!path || path[0] == '\0' || !platform::ui::device::sd_ready())
    {
        return false;
    }

    std::vector<std::string> pending_image_urls;
    const bool read_ok = read_track_file_lines(
        path,
        [&](const std::string& line)
        {
            if (find_case_insensitive(line, "<Placemark") != std::string::npos)
            {
                pending_image_urls.clear();
            }

            extract_img_srcs(line, pending_image_urls);

            std::string text;
            if (!pending_image_urls.empty() && extract_tag_text(line, "coordinates", text))
            {
                std::size_t token_start = 0;
                while (token_start < text.size())
                {
                    while (token_start < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[token_start])))
                    {
                        ++token_start;
                    }
                    if (token_start >= text.size())
                    {
                        break;
                    }
                    std::size_t token_end = token_start;
                    while (token_end < text.size() &&
                           !std::isspace(static_cast<unsigned char>(text[token_end])))
                    {
                        ++token_end;
                    }

                    double lat = 0.0;
                    double lon = 0.0;
                    double altitude_m = 0.0;
                    bool has_altitude = false;
                    if (parse_kml_coordinate_token(
                            text.substr(token_start, token_end - token_start),
                            lat,
                            lon,
                            altitude_m,
                            has_altitude))
                    {
                        append_route_images(pending_image_urls, lat, lon, true);
                        pending_image_urls.clear();
                        break;
                    }
                    token_start = token_end;
                }
            }
            if (!pending_image_urls.empty() && extract_tag_text(line, "gx:coord", text))
            {
                double lat = 0.0;
                double lon = 0.0;
                double altitude_m = 0.0;
                bool has_altitude = false;
                if (parse_kml_gx_coord_token(text, lat, lon, altitude_m, has_altitude))
                {
                    append_route_images(pending_image_urls, lat, lon, true);
                    pending_image_urls.clear();
                }
            }

            if (find_case_insensitive(line, "</Placemark") != std::string::npos)
            {
                if (!pending_image_urls.empty())
                {
                    append_route_images(pending_image_urls, 0.0, 0.0, false);
                    pending_image_urls.clear();
                }
            }
        });

    assign_route_image_paths();
    return read_ok;
}

void build_route_elevation_samples(
    const std::vector<TrackOverlayPoint>& points,
    std::vector<::ui::widgets::route_elevation_profile::Sample>& out)
{
    out.clear();
    out.reserve(points.size());
    double distance_m = 0.0;
    const TrackOverlayPoint* previous = nullptr;
    for (const auto& point : points)
    {
        if (previous)
        {
            const double step = route_point_distance_m(*previous, point);
            if (std::isfinite(step))
            {
                distance_m += step;
            }
        }
        previous = &point;
        ::ui::widgets::route_elevation_profile::Sample sample{};
        sample.altitude_m = point.altitude_m;
        sample.distance_m = distance_m;
        sample.has_altitude = point.has_altitude;
        out.push_back(sample);
    }
}

enum class KmlCoordinateMode : uint8_t
{
    None,
    Coordinates,
    GxCoord,
};

struct KmlCoordinateStreamParser
{
    std::vector<TrackOverlayPoint> coordinate_points;
    std::vector<TrackOverlayPoint> gx_points;
    TrackElevationMetrics coordinate_metrics;
    TrackElevationMetrics gx_metrics;
    bool in_tag = false;
    bool tag_truncated = false;
    KmlCoordinateMode mode = KmlCoordinateMode::None;
    bool discard_token = false;
    std::string tag;
    std::string token;

    KmlCoordinateStreamParser()
    {
        coordinate_points.reserve(kMaxTrackOverlayPoints);
        gx_points.reserve(kMaxTrackOverlayPoints);
        tag.reserve(kMaxKmlTagBytes);
        token.reserve(kMaxKmlCoordinateTokenBytes);
    }

    std::vector<TrackOverlayPoint>& active_points()
    {
        return mode == KmlCoordinateMode::GxCoord ? gx_points : coordinate_points;
    }

    TrackElevationMetrics& active_metrics()
    {
        return mode == KmlCoordinateMode::GxCoord ? gx_metrics : coordinate_metrics;
    }

    void flush_token()
    {
        if (discard_token)
        {
            token.clear();
            discard_token = false;
            return;
        }
        if (token.empty())
        {
            return;
        }

        double lat = 0.0;
        double lon = 0.0;
        double altitude_m = 0.0;
        bool has_altitude = false;
        const bool parsed = mode == KmlCoordinateMode::GxCoord
                                ? parse_kml_gx_coord_token(token, lat, lon, altitude_m, has_altitude)
                                : parse_kml_coordinate_token(token, lat, lon, altitude_m, has_altitude);
        if (parsed)
        {
            update_track_elevation_metrics(active_metrics(), altitude_m, has_altitude);
            append_track_point_raw(active_points(), lat, lon, altitude_m, has_altitude);
        }
        token.clear();
    }

    void begin_mode(KmlCoordinateMode next_mode)
    {
        mode = next_mode;
        token.clear();
        discard_token = false;
    }

    void end_mode(KmlCoordinateMode closing_mode)
    {
        if (mode == closing_mode)
        {
            flush_token();
            mode = KmlCoordinateMode::None;
        }
    }

    void handle_tag()
    {
        if (tag_truncated)
        {
            tag.clear();
            tag_truncated = false;
            return;
        }

        bool closing = false;
        if (kml_tag_name_matches(tag, "coordinates", closing))
        {
            if (closing)
            {
                end_mode(KmlCoordinateMode::Coordinates);
            }
            else
            {
                begin_mode(KmlCoordinateMode::Coordinates);
            }
        }
        else if (kml_tag_name_matches(tag, "coord", closing))
        {
            if (closing)
            {
                end_mode(KmlCoordinateMode::GxCoord);
            }
            else
            {
                begin_mode(KmlCoordinateMode::GxCoord);
            }
        }
        tag.clear();
    }

    void consume_char(char ch)
    {
        if (in_tag)
        {
            if (ch == '>')
            {
                in_tag = false;
                handle_tag();
                return;
            }
            if (tag.size() < kMaxKmlTagBytes)
            {
                tag.push_back(ch);
            }
            else
            {
                tag_truncated = true;
            }
            return;
        }

        if (ch == '<')
        {
            if (mode != KmlCoordinateMode::None)
            {
                flush_token();
            }
            in_tag = true;
            tag.clear();
            tag_truncated = false;
            return;
        }

        if (mode == KmlCoordinateMode::None)
        {
            return;
        }
        if (mode == KmlCoordinateMode::Coordinates &&
            std::isspace(static_cast<unsigned char>(ch)))
        {
            flush_token();
            return;
        }
        if (discard_token)
        {
            return;
        }
        if (token.size() >= kMaxKmlCoordinateTokenBytes)
        {
            token.clear();
            discard_token = true;
            return;
        }
        token.push_back(ch);
    }

    void consume(const char* data, uint32_t size)
    {
        if (!data)
        {
            return;
        }
        for (uint32_t index = 0; index < size; ++index)
        {
            consume_char(data[index]);
        }
    }

    void finish()
    {
        if (mode != KmlCoordinateMode::None)
        {
            flush_token();
        }
    }

    void take_points(
        std::vector<TrackOverlayPoint>& out,
        TrackElevationMetrics& metrics,
        std::vector<::ui::widgets::route_elevation_profile::Sample>& profile_samples)
    {
        if (!gx_points.empty())
        {
            build_route_elevation_samples(gx_points, profile_samples);
            metrics = gx_metrics;
            out = std::move(gx_points);
            return;
        }
        build_route_elevation_samples(coordinate_points, profile_samples);
        metrics = coordinate_metrics;
        out = std::move(coordinate_points);
    }
};

void downsample_track_points(std::vector<TrackOverlayPoint>& points)
{
    if (points.size() <= kMaxTrackOverlayPoints)
    {
        return;
    }

    const std::size_t total = points.size();
    for (std::size_t index = 0; index < kMaxTrackOverlayPoints; ++index)
    {
        const std::size_t src = (index * (total - 1)) / (kMaxTrackOverlayPoints - 1);
        points[index] = points[src];
    }
    points.resize(kMaxTrackOverlayPoints);
}

void append_track_point_raw(std::vector<TrackOverlayPoint>& out,
                            double lat,
                            double lon,
                            double altitude_m,
                            bool has_altitude)
{
    TrackOverlayPoint point{};
    point.lat = lat;
    point.lon = lon;
    point.altitude_m = altitude_m;
    point.has_altitude = has_altitude;
    out.push_back(point);
}

void append_track_point(std::vector<TrackOverlayPoint>& out,
                        double lat,
                        double lon,
                        double altitude_m,
                        bool has_altitude)
{
    append_track_point_raw(out, lat, lon, altitude_m, has_altitude);
    downsample_track_points(out);
}

bool load_gpx_track_points(const char* path, std::vector<TrackOverlayPoint>& out)
{
    out.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    out.reserve(kMaxTrackOverlayPoints);
    const bool read_ok = read_track_file_lines(path, [&out](const std::string& line)
                                               {
        if (line.find("<trkpt") != std::string::npos)
        {
            double lat = 0.0;
            double lon = 0.0;
            if (parse_attr_double(line, "lat", lat) && parse_attr_double(line, "lon", lon))
            {
                append_track_point(out, lat, lon);
            }
        } });
    return read_ok && !out.empty();
}

bool load_csv_track_points(const char* path, std::vector<TrackOverlayPoint>& out)
{
    out.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    out.reserve(kMaxTrackOverlayPoints);
    const bool read_ok = read_track_file_lines(path, [&out](std::string line)
                                               {
        line = trim_copy(std::move(line));
        if (!line.empty())
        {
            const char first = line.front();
            if ((first >= '0' && first <= '9') || first == '-')
            {
                const std::size_t comma1 = line.find(',');
                if (comma1 != std::string::npos && comma1 > 0)
                {
                    const std::size_t comma2 = line.find(',', comma1 + 1);
                    const std::string lat_str = line.substr(0, comma1);
                    const std::string lon_str = comma2 != std::string::npos
                                                    ? line.substr(comma1 + 1, comma2 - comma1 - 1)
                                                    : line.substr(comma1 + 1);

                    double lat = 0.0;
                    double lon = 0.0;
                    if (parse_double_token(lat_str, lat) && parse_double_token(lon_str, lon))
                    {
                        append_track_point(out, lat, lon);
                    }
                }
            }
        } });
    return read_ok && !out.empty();
}

bool load_kml_track_points(const char* path,
                           std::vector<TrackOverlayPoint>& out,
                           TrackElevationMetrics& metrics,
                           std::vector<::ui::widgets::route_elevation_profile::Sample>& profile_samples)
{
    out.clear();
    metrics = TrackElevationMetrics{};
    profile_samples.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    out.reserve(kMaxTrackOverlayPoints);
    KmlCoordinateStreamParser parser;
    const bool read_ok = read_track_file_chunks(path, [&](const char* data, uint32_t size)
                                                { parser.consume(data, size); });
    parser.finish();
    parser.take_points(out, metrics, profile_samples);
    downsample_track_points(out);
    return read_ok && !out.empty();
}

void append_route_image_overlay(::ui::map::MapOverlaySnapshot& snapshot)
{
    if (!s_route_image_strip_visible || !route_image_context_active())
    {
        return;
    }

    for (std::size_t index = 0; index < s_route_images.size(); ++index)
    {
        if (snapshot.item_count >= ::ui::map::MapOverlaySnapshot::kMaxItems)
        {
            snapshot.truncated = true;
            return;
        }

        const auto& image = s_route_images[index];
        if (!image.has_position)
        {
            continue;
        }
        auto& item = snapshot.items[snapshot.item_count++];
        item = ::ui::map::MapOverlayItem{};
        const bool selected = index == s_route_selected_image;
        item.kind = selected ? ::ui::map::MapOverlayKind::SelectedTarget
                             : ::ui::map::MapOverlayKind::RoutePoint;
        item.style = selected ? ::ui::map::MapOverlayStyle::Warning
                              : ::ui::map::MapOverlayStyle::Route;
        item.point.valid = true;
        item.point.lat = image.lat;
        item.point.lon = image.lon;
        item.selected = selected;
        item.stable_id = static_cast<uint32_t>(0x494D0000U + index);
        if (selected)
        {
            char label[24]{};
            std::snprintf(label,
                          sizeof(label),
                          "%u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(s_route_images.size()));
            ::ui::copyText(item.label, label);
        }
        item.visible = true;
    }
}

void append_track_overlay(::ui::map::MapOverlaySnapshot& snapshot)
{
    if (!s_track_overlay_active || s_track_points.empty())
    {
        return;
    }

    const std::size_t available =
        snapshot.item_count < ::ui::map::MapOverlaySnapshot::kMaxItems
            ? ::ui::map::MapOverlaySnapshot::kMaxItems - snapshot.item_count
            : 0;
    if (available == 0)
    {
        snapshot.truncated = true;
        return;
    }

    const std::size_t total = s_track_points.size();
    const std::size_t count = std::min<std::size_t>(available, total);
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t src = count == total
                                    ? index
                                    : (index * (total - 1)) / (count - 1 == 0 ? 1 : count - 1);
        const auto& point = s_track_points[src];
        auto& item = snapshot.items[snapshot.item_count++];
        const bool is_route = s_track_overlay_kind == TrackOverlayFileKind::Route;
        item.kind = is_route ? ::ui::map::MapOverlayKind::RoutePoint
                             : ::ui::map::MapOverlayKind::TrackPoint;
        item.style = is_route ? ::ui::map::MapOverlayStyle::Route
                              : ::ui::map::MapOverlayStyle::Track;
        item.point.valid = true;
        item.point.lat = point.lat;
        item.point.lon = point.lon;
        item.stable_id = static_cast<uint32_t>((is_route ? 0x52540000U : 0x54520000U) + index);
        item.visible = true;
    }

    if (count < total)
    {
        snapshot.truncated = true;
    }
}

void keep_only_current_position_overlay(::ui::map::MapOverlaySnapshot& snapshot)
{
    const bool keep_route_points =
        s_track_overlay_active &&
        s_track_overlay_kind == TrackOverlayFileKind::Route;
    const bool keep_selected_route_image =
        keep_route_points && s_route_image_strip_visible;
    std::size_t write = 0;
    for (std::size_t read = 0; read < snapshot.item_count; ++read)
    {
        const auto& item = snapshot.items[read];
        const bool keep_item =
            item.kind == ::ui::map::MapOverlayKind::CurrentPosition ||
            (keep_route_points && item.kind == ::ui::map::MapOverlayKind::RoutePoint) ||
            (keep_selected_route_image &&
             item.kind == ::ui::map::MapOverlayKind::SelectedTarget);
        if (!keep_item)
        {
            continue;
        }
        if (write != read)
        {
            snapshot.items[write] = item;
        }
        ++write;
    }
    snapshot.item_count = write;
    snapshot.truncated = false;
}

bool load_map_track_file_impl(const char* path, bool show_fail_toast)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }

    std::vector<TrackOverlayPoint> points;
    TrackElevationMetrics elevation_metrics;
    std::vector<::ui::widgets::route_elevation_profile::Sample> elevation_samples;
    const std::string normalized = ::ui::fs::normalize_path(path);
    TrackOverlayFileKind file_kind = TrackOverlayFileKind::Track;
    bool loaded = false;
    if (ends_with_ignore_case(normalized, ".csv"))
    {
        loaded = load_csv_track_points(path, points);
    }
    else if (ends_with_ignore_case(normalized, ".kml"))
    {
        loaded = load_kml_track_points(path, points, elevation_metrics, elevation_samples);
        file_kind = TrackOverlayFileKind::Route;
    }
    else
    {
        loaded = load_gpx_track_points(path, points);
    }
    if (!loaded)
    {
        if (show_fail_toast)
        {
            set_map_notice("No track yet", 1500);
            request_refresh_view();
        }
        s_track_overlay_active = false;
        s_track_points.clear();
        s_track_file.clear();
        s_track_overlay_kind = TrackOverlayFileKind::Track;
        s_track_elevation_metrics = TrackElevationMetrics{};
        s_route_elevation_samples.clear();
        s_route_elevation_work_samples.clear();
        s_route_images.clear();
        s_route_image_strip_items.clear();
        s_route_asset_id.clear();
        s_route_elevation_profile_visible = false;
        s_route_image_strip_visible = false;
        s_route_image_saved_state_known = false;
        s_route_image_cache_state_known = false;
        s_route_image_cache_build_running = false;
        s_route_deviation_active = false;
        s_route_deviation_distance_m = 0.0;
        s_route_selected_image = 0;
        s_route_image_strip_items_hash = 0;
        ::ui::widgets::route_image_strip::destroy(s_route_image_strip);
        return false;
    }

    s_track_file = path;
    s_track_points = std::move(points);
    s_track_overlay_active = true;
    s_track_overlay_kind = file_kind;
    s_track_elevation_metrics =
        file_kind == TrackOverlayFileKind::Route ? elevation_metrics : TrackElevationMetrics{};
    if (file_kind == TrackOverlayFileKind::Route)
    {
        s_route_elevation_samples = std::move(elevation_samples);
        s_route_asset_id = route_asset_id_for_path(path);
        (void)load_kml_route_images(path);
        if (s_route_images.empty())
        {
            s_route_image_strip_visible = false;
            s_route_image_strip_items_hash = 0;
            ::ui::widgets::route_image_strip::destroy(s_route_image_strip);
        }
    }
    else
    {
        s_route_elevation_samples.clear();
        s_route_images.clear();
        s_route_image_strip_items.clear();
        s_route_asset_id.clear();
        s_route_image_strip_visible = false;
        s_route_image_saved_state_known = false;
        s_route_image_cache_state_known = false;
        s_route_image_cache_build_running = false;
        s_route_selected_image = 0;
        s_route_image_strip_items_hash = 0;
        ::ui::widgets::route_image_strip::destroy(s_route_image_strip);
    }
    s_route_elevation_work_samples.clear();
    if (file_kind != TrackOverlayFileKind::Route || !route_elevation_profile_available())
    {
        s_route_elevation_profile_visible = false;
    }
    s_route_deviation_active = false;
    s_route_deviation_distance_m = 0.0;
    if (!s_track_points.empty())
    {
        const auto& last = s_track_points.back();
        auto& model = map_workspace_model();
        auto viewport = model.viewport();
        viewport.center_lat = last.lat;
        viewport.center_lon = last.lon;
        viewport.zoom = kDefaultTrackerZoom;
        (void)model.setViewport(viewport);
        s_map_zoom = kDefaultTrackerZoom;
        s_map_pan_x = 0;
        s_map_pan_y = 0;
    }

    set_map_notice(file_kind == TrackOverlayFileKind::Route ? "Route loaded" : "Track loaded", 1200);
    request_refresh_view();
    return true;
}

bool select_cache_at(lv_point_t point)
{
    if (!s_map_target || !s_map_target->select_overlay || !s_overlay_snapshot) return false;
    const ::ui::map::MapOverlayItem* closest = nullptr;
    int64_t best = 24 * 24 + 1;
    for (size_t index = 0; index < s_overlay_snapshot->item_count; ++index)
    {
        const auto& item = s_overlay_snapshot->items[index];
        if (item.kind != ::ui::map::MapOverlayKind::Geocache || !item.visible || !item.point.valid) continue;
        lv_point_t projected;
        if (!::ui::widgets::map::project_point(s_map_runtime, {true, item.point.lat, item.point.lon}, projected)) continue;
        const int64_t dx = int64_t(point.x) - projected.x, dy = int64_t(point.y) - projected.y;
        const auto distance = dx * dx + dy * dy;
        if (distance < best)
        {
            best = distance;
            closest = &item;
        }
    }
    if (!closest) return false;
    s_map_target->select_overlay(s_map_target->overlay_context, closest->stable_id);
    return true;
}

void map_gesture_callback(const ::ui::widgets::map::GestureEvent& event, void*)
{
    switch (event.phase)
    {
    case ::ui::widgets::map::GesturePhase::Tapped:
    {
        const auto* root = ::ui::widgets::map::widgets(s_map_runtime).root;
        if (!root) break;
        lv_area_t bounds;
        lv_obj_get_coords(root, &bounds);
        (void)select_cache_at({event.point.x - bounds.x1, event.point.y - bounds.y1});
        break;
    }
    case ::ui::widgets::map::GesturePhase::Pressed:
        s_map_drag_start_pan_x = s_map_pan_x;
        s_map_drag_start_pan_y = s_map_pan_y;
        s_map_drag_active = false;
        break;
    case ::ui::widgets::map::GesturePhase::DragBegin:
        s_map_drag_active = true;
        apply_map_drag_preview();
        break;
    case ::ui::widgets::map::GesturePhase::DragUpdate:
        s_map_drag_active = true;
        s_map_pan_x = s_map_drag_start_pan_x + event.total_dx;
        s_map_pan_y = s_map_drag_start_pan_y + event.total_dy;
        apply_map_drag_preview();
        break;
    case ::ui::widgets::map::GesturePhase::DragEnd:
    case ::ui::widgets::map::GesturePhase::Cancel:
        if (s_map_drag_active)
        {
            (void)sync_workspace_center_from_screen();
            s_map_pan_x = 0;
            s_map_pan_y = 0;
            sync_workspace_viewport_from_renderer();
            request_refresh_view();
        }
        s_map_drag_active = false;
        break;
    }
}

void apply_map_drag_preview()
{
    if (!s_root)
    {
        return;
    }

    const auto snapshot = map_workspace_model().snapshot();
    if (!snapshot.header.valid)
    {
        return;
    }

    ::ui::widgets::map::apply_model_lightweight(
        s_map_runtime,
        build_map_model(snapshot));
}

void sync_map_tile_loader_pause()
{
    const auto status = platform::ui::route_storage::route_image_download_status();
    ::ui::widgets::route_image_operation::sync(status);
    const bool should_pause = status.busy;
    s_map_tile_loader_paused = should_pause;
    ::ui::widgets::map::set_loader_paused(s_map_runtime, should_pause);
}

void refresh_view()
{
    if (!s_root || s_projection != Projection::Map || !s_overlay_snapshot)
    {
        return;
    }
    sync_map_tile_loader_pause();

    ui_update_top_bar_battery(s_top_bar);
    update_map_top_bar_title();
    if (!s_location_request) update_route_deviation_state();

    sync_workspace_layers_from_renderer();
    auto snapshot = map_workspace_model().snapshot();
    (void)map_overlay_source().buildMapOverlaySnapshot(*s_overlay_snapshot);
    if (s_map_target && s_overlay_snapshot->item_count < ::ui::map::MapOverlaySnapshot::kMaxItems)
    {
        auto& target = s_overlay_snapshot->items[s_overlay_snapshot->item_count++];
        target = ::ui::map::MapOverlayItem{};
        target.kind = ::ui::map::MapOverlayKind::Geocache;
        target.style = ::ui::map::MapOverlayStyle::Warning;
        target.point.valid = true;
        target.point.lat = s_map_target->latitude_e7 / 10000000.0;
        target.point.lon = s_map_target->longitude_e7 / 10000000.0;
        target.selected = target.visible = true;
        ::ui::copyText(target.label, s_map_target->name);
    }
    if (s_map_target && s_map_target->append_overlays)
        s_map_target->append_overlays(s_map_target->overlay_context, *s_overlay_snapshot);
    if (!s_location_request)
    {
        append_route_image_overlay(*s_overlay_snapshot);
        append_track_overlay(*s_overlay_snapshot);
    }
    if (!s_map_info_visible)
    {
        keep_only_current_position_overlay(*s_overlay_snapshot);
    }
    if (s_target_request) s_target_request->appendOverlay(*s_overlay_snapshot);

    if (snapshot.header.valid)
    {
        if (s_map_drag_active)
        {
            ::ui::widgets::map::apply_model_lightweight(
                s_map_runtime,
                build_map_model(snapshot));
        }
        else
        {
            ::ui::widgets::map::apply_model(s_map_runtime, build_map_model(snapshot));
        }
        if (!s_map_drag_active && commit_pending_map_pan_from_screen())
        {
            snapshot = map_workspace_model().snapshot();
            ::ui::widgets::map::apply_model(s_map_runtime, build_map_model(snapshot));
        }
        if (!s_map_drag_active)
        {
            ::ui::widgets::map::apply_overlay(s_map_runtime, *s_overlay_snapshot);
        }
    }
    else
    {
        ::ui::widgets::map::clear(s_map_runtime);
    }
    sync_map_control_labels(snapshot);
    sync_map_route_image_strip();
}

void refresh_view_async(void*)
{
    s_map_refresh_pending = false;
    refresh_view();
}

void request_refresh_view()
{
    if (s_map_refresh_pending)
    {
        return;
    }

    s_map_refresh_pending = true;
    lv_async_call(refresh_view_async, nullptr);
}

class SharedGpsRuntimeRefreshModel final : public ::ui::screens::gps::IGpsStatusRefreshModel
{
  public:
    void refresh() override {}
};

class SharedGpsUiRefreshSink final : public ::ui::screens::gps::IGpsUiRefreshSink
{
  public:
    void onGpsRuntimeUpdated() override
    {
        if (s_projection == Projection::GpsStatus)
        {
            refresh_gps_status_view();
            return;
        }
        if (s_map_drag_active)
        {
            return;
        }
        refresh_view();
    }
};

SharedGpsRuntimeRefreshModel& gps_runtime_refresh_model()
{
    static SharedGpsRuntimeRefreshModel model;
    return model;
}

SharedGpsUiRefreshSink& gps_runtime_refresh_sink()
{
    static SharedGpsUiRefreshSink sink;
    return sink;
}

::ui::screens::gps::GpsPageRuntimePump& gps_runtime_pump()
{
    static ::ui::screens::gps::GpsPageRuntimePump pump(
        gps_runtime_refresh_model(),
        &gps_runtime_refresh_sink(),
        750);
    return pump;
}

void refresh_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    sync_map_tile_loader_pause();
    gps_runtime_pump().update(sys::millis_now());
}

void consume_key_event(lv_event_t* e)
{
    if (!e)
    {
        return;
    }

    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

void open_map_help_modal_async(void*)
{
    s_map_help_open_pending = false;
    open_map_help_modal();
}

void request_open_map_help_modal()
{
    if (s_map_help_open_pending)
    {
        return;
    }

    s_map_help_open_pending = true;
    lv_async_call(open_map_help_modal_async, nullptr);
}

lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action);
void add_map_controls_to_group(lv_group_t* group);

void adjust_map_zoom(int delta)
{
    const int next_zoom = std::max(::ui::widgets::map::kMinZoom,
                                   std::min(s_map_zoom + delta,
                                            ::ui::widgets::map::kMaxZoom));
    if (next_zoom == s_map_zoom)
    {
        return;
    }

    (void)sync_workspace_center_from_screen();
    s_map_pan_x = 0;
    s_map_pan_y = 0;
    s_map_zoom = next_zoom;
    sync_workspace_viewport_from_renderer();
    request_refresh_view();
}

void center_map_on_self()
{
    const auto result = map_workspace_model().centerOnSelf();
    const auto snapshot = map_workspace_model().snapshot();
    const auto& config = app::configFacade().readConfig();
    if (result.ok)
    {
        s_map_pan_x = 0;
        s_map_pan_y = 0;
        set_map_notice("Centered", 900);
        std::printf("[MAP][POS] center_on_self ok self_valid=%d self_lat=%.6f self_lon=%.6f viewport_lat=%.6f viewport_lon=%.6f zoom=%u map_coord=%u\n",
                    snapshot.self.valid ? 1 : 0,
                    snapshot.self.lat,
                    snapshot.self.lon,
                    snapshot.viewport.center_lat,
                    snapshot.viewport.center_lon,
                    static_cast<unsigned>(snapshot.viewport.zoom),
                    static_cast<unsigned>(config.map_coord_system));
    }
    else
    {
        set_map_notice("No GPS fix", 1200);
        std::printf("[MAP][POS] center_on_self failed self_valid=%d self_lat=%.6f self_lon=%.6f status=%s map_coord=%u\n",
                    snapshot.self.valid ? 1 : 0,
                    snapshot.self.lat,
                    snapshot.self.lon,
                    snapshot.status_line.c_str(),
                    static_cast<unsigned>(config.map_coord_system));
    }
    sync_workspace_viewport_from_renderer();
    request_refresh_view();
}

void cycle_map_layer()
{
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    const uint8_t next_source = static_cast<uint8_t>((layer_state.map_source + 1U) % 3U);
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::set_layer_map_source(next_source, &notice);
    if (notice.has_message)
    {
        set_map_notice(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1400);
    }
    else
    {
        char text[40]{};
        std::snprintf(text,
                      sizeof(text),
                      "Base %s",
                      compact_map_source_label(next_source));
        set_map_notice(text, 900);
    }
    sync_workspace_layers_from_renderer();
    request_refresh_view();
}

void toggle_map_contour()
{
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::toggle_layer_contour(&notice);
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    if (notice.has_message)
    {
        set_map_notice(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1400);
    }
    else
    {
        set_map_notice(layer_state.contour_enabled ? "Contour on" : "Contour off", 900);
    }
    sync_workspace_layers_from_renderer();
    request_refresh_view();
}

void close_map_help_modal()
{
    if (!s_map_help_modal || !lv_obj_is_valid(s_map_help_modal))
    {
        s_map_help_modal = nullptr;
        return;
    }

    lv_obj_del(s_map_help_modal);
    s_map_help_modal = nullptr;
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        if (s_top_bar.back_btn)
        {
            lv_group_add_obj(app_g, s_top_bar.back_btn);
        }
        add_map_controls_to_group(app_g);
        if (s_map_help_btn)
        {
            lv_group_focus_obj(s_map_help_btn);
        }
    }
}

void on_map_help_modal_key(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC || key == LV_KEY_ENTER ||
        is_help_key(key))
    {
        consume_key_event(e);
        close_map_help_modal();
        return;
    }

    if (key == LV_KEY_UP || key == 'w' || key == 'W')
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_by(target, 0, 18, LV_ANIM_OFF);
        }
        consume_key_event(e);
        return;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S')
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_by(target, 0, -18, LV_ANIM_OFF);
        }
        consume_key_event(e);
        return;
    }

    consume_key_event(e);
}

void open_map_help_modal()
{
    if (s_map_help_modal && lv_obj_is_valid(s_map_help_modal))
    {
        close_map_help_modal();
        return;
    }
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }

    s_map_help_modal = lv_obj_create(s_root);
    lv_obj_set_size(s_map_help_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_map_help_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_map_help_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_map_help_modal, lv_color_hex(0x1C1812), 0);
    lv_obj_set_style_bg_opa(s_map_help_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_help_modal, 0, 0);
    lv_obj_set_style_pad_all(s_map_help_modal, 4, 0);
    lv_obj_clear_flag(s_map_help_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_map_help_modal, on_map_help_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* panel = lv_obj_create(s_map_help_modal);
    lv_obj_set_size(panel, 304, 176);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_left(panel, 7, 0);
    lv_obj_set_style_pad_right(panel, 7, 0);
    lv_obj_set_style_pad_top(panel, 5, 0);
    lv_obj_set_style_pad_bottom(panel, 5, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, on_map_help_modal_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Map Help");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25170D), 0);

    auto add_keycap = [](lv_obj_t* parent, const char* text, lv_coord_t width)
    {
        lv_obj_t* keycap = lv_label_create(parent);
        lv_obj_set_size(keycap, width, 14);
        lv_obj_set_style_bg_color(keycap, lv_color_hex(0xF8E6C3), 0);
        lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(keycap, 1, 0);
        lv_obj_set_style_border_color(keycap, lv_color_hex(0x8A6E43), 0);
        lv_obj_set_style_radius(keycap, 3, 0);
        lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(keycap, lv_color_hex(0x25170D), 0);
        lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
        lv_label_set_text(keycap, text ? text : "");
        return keycap;
    };

    auto add_help_row = [&](const char* primary,
                            const char* secondary,
                            const char* description)
    {
        lv_obj_t* row = lv_obj_create(panel);
        lv_obj_set_size(row, LV_PCT(100), 15);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 3, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* keys = lv_obj_create(row);
        lv_obj_set_size(keys, 76, 15);
        lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(keys,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(keys, 0, 0);
        lv_obj_set_style_pad_all(keys, 0, 0);
        lv_obj_set_style_pad_column(keys, 2, 0);
        lv_obj_clear_flag(keys, LV_OBJ_FLAG_SCROLLABLE);

        if (secondary && secondary[0] != '\0')
        {
            const lv_coord_t secondary_width =
                std::strlen(secondary) > 4 ? 48 : (std::strlen(secondary) > 2 ? 34 : 22);
            add_keycap(keys, primary, std::strlen(primary) > 2 ? 34 : 22);
            add_keycap(keys, secondary, secondary_width);
        }
        else
        {
            add_keycap(keys, primary, 72);
        }

        lv_obj_t* text = lv_label_create(row);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_style_text_font(text, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(text, lv_color_hex(0x3E2B18), 0);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_label_set_text(text, description ? description : "");
    };

    add_help_row("WASD", nullptr, "Move map");
    add_help_row("Q", "E", "Zoom map");
    add_help_row("C", "Pos", "Center current position");
    add_help_row("P", nullptr, "Show/hide route photos");
    add_help_row("L", nullptr, "Change base layer");
    add_help_row("O", "Contour", "Toggle contour overlay");
    add_help_row("T", "Track", "Select track file");
    add_help_row("V", nullptr, "Show/hide elevation profile");
    add_help_row("I", nullptr, "Hide info, keep route");
    add_help_row("Route", nullptr, "Shown when route active");
    add_help_row("Members", nullptr, "Shown when team active");
    add_help_row(help_key_label(), "Back", "Close help");

    lv_obj_move_foreground(s_map_help_modal);
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, panel);
        lv_group_focus_obj(panel);
    }
}

void close_tracker_modal()
{
    if (!s_tracker_modal || !lv_obj_is_valid(s_tracker_modal))
    {
        s_tracker_modal = nullptr;
        return;
    }

    lv_obj_del(s_tracker_modal);
    s_tracker_modal = nullptr;
    rebuild_map_control_group();
    if (app_g && s_map_tracker_btn && lv_obj_is_valid(s_map_tracker_btn))
    {
        lv_group_focus_obj(s_map_tracker_btn);
    }
}

void tracker_modal_bg_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    if (lv_event_get_target_obj(e) == s_tracker_modal)
    {
        close_tracker_modal();
    }
}

void tracker_modal_close_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        close_tracker_modal();
    }
}

void tracker_modal_key_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC)
    {
        consume_key_event(e);
        close_tracker_modal();
    }
}

void tracker_track_button_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const std::uintptr_t index = reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(e));
    if (index >= s_track_modal_names.size())
    {
        return;
    }

    const std::string path =
        std::string(::platform::ui::tracker::track_dir()) + "/" + s_track_modal_names[index];
    (void)load_map_track_file_impl(path.c_str(), true);
    close_tracker_modal();
}

lv_obj_t* create_tracker_modal_button(lv_obj_t* parent,
                                      const char* text,
                                      lv_event_cb_t cb,
                                      void* user_data)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, ::ui::page_profile::current().large_touch_hitbox ? 52 : 24);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 8, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_add_event_cb(btn, tracker_modal_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

void open_tracker_modal()
{
    if (s_tracker_modal && lv_obj_is_valid(s_tracker_modal))
    {
        close_tracker_modal();
        return;
    }
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }
    if (!platform::ui::device::sd_ready())
    {
        set_map_notice("No SD Card", 1200);
        request_refresh_view();
        return;
    }

    s_tracker_modal = lv_obj_create(s_root);
    lv_obj_set_size(s_tracker_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_tracker_modal, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_tracker_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_tracker_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_tracker_modal, lv_color_hex(0x1C1812), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tracker_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tracker_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tracker_modal, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_tracker_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_tracker_modal, tracker_modal_bg_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_tracker_modal, tracker_modal_key_event_cb, LV_EVENT_KEY, nullptr);

    const bool touch_layout = ::ui::page_profile::current().large_touch_hitbox;
    const auto size = ::ui::page_profile::resolve_modal_size(touch_layout ? 520 : 300,
                                                             touch_layout ? 520 : 178);
    lv_obj_t* panel = lv_obj_create(s_tracker_modal);
    lv_obj_set_size(panel, size.width, size.height);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFF3DF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, touch_layout ? 12 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, touch_layout ? 8 : 3, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, tracker_modal_key_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "Select Track");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25170D), LV_PART_MAIN);

    lv_obj_t* list = lv_obj_create(panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, touch_layout ? 6 : 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    s_track_modal_names.clear();
    ::platform::ui::tracker::list_tracks(s_track_modal_names, 64);
    std::sort(s_track_modal_names.begin(), s_track_modal_names.end());

    lv_obj_t* first_focus = nullptr;
    std::vector<lv_obj_t*> focusables;
    if (s_track_modal_names.empty())
    {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, "No track files");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6D5B43), LV_PART_MAIN);
    }
    else
    {
        for (std::size_t index = 0; index < s_track_modal_names.size(); ++index)
        {
            lv_obj_t* btn = create_tracker_modal_button(
                list,
                s_track_modal_names[index].c_str(),
                tracker_track_button_event_cb,
                reinterpret_cast<void*>(index));
            if (!first_focus)
            {
                first_focus = btn;
            }
            focusables.push_back(btn);
        }
    }

    lv_obj_t* close_btn =
        create_tracker_modal_button(panel, "Close", tracker_modal_close_event_cb, nullptr);
    if (!first_focus)
    {
        first_focus = close_btn;
    }
    focusables.push_back(close_btn);

    lv_obj_move_foreground(s_tracker_modal);
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
        for (lv_obj_t* obj : focusables)
        {
            if (obj)
            {
                lv_group_add_obj(app_g, obj);
            }
        }
        if (first_focus)
        {
            lv_group_focus_obj(first_focus);
        }
    }
}

void show_route_context_notice()
{
    if (!route_context_available())
    {
        set_map_notice("No route", 1200);
        request_refresh_view();
        return;
    }

    if (!load_configured_route_overlay(true))
    {
        set_map_notice("Route load failed", 1500);
        request_refresh_view();
        return;
    }
    set_map_notice("Route active", 1200);
    request_refresh_view();
}

void show_team_overlay_notice()
{
    const auto snapshot = map_workspace_model().snapshot();
    if (!snapshot.team.available)
    {
        set_map_notice("No team", 1200);
        request_refresh_view();
        return;
    }

    char message[48]{};
    std::snprintf(message,
                  sizeof(message),
                  "Team %u/%u",
                  static_cast<unsigned>(snapshot.team.visible_members),
                  static_cast<unsigned>(snapshot.team.stale_members));
    set_map_notice(message, 1400);
    request_refresh_view();
}

void on_map_control_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const auto action = static_cast<MapControlAction>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    switch (action)
    {
    case MapControlAction::ZoomOut:
        adjust_map_zoom(-1);
        break;
    case MapControlAction::ZoomIn:
        adjust_map_zoom(1);
        break;
    case MapControlAction::Center:
        center_map_on_self();
        break;
    case MapControlAction::Layer:
        cycle_map_layer();
        break;
    case MapControlAction::Contour:
        toggle_map_contour();
        break;
    case MapControlAction::Tracker:
        open_tracker_modal();
        break;
    case MapControlAction::Help:
        request_open_map_help_modal();
        break;
    case MapControlAction::Route:
        show_route_context_notice();
        break;
    case MapControlAction::TeamMember:
        break;
    case MapControlAction::PickLocation:
        consume_key_event(e);
        // Commit the rendered crosshair centre (including pending pan) before
        // asking the shared workspace model to produce a result.
        if (s_location_request && sync_workspace_center_from_screen() && map_workspace_model().pickLocation().ok)
            request_exit();
        else show_toast(::ui::i18n::tr("Invalid location"), 2000);
        break;
    case MapControlAction::CancelLocation:
        consume_key_event(e);
        request_exit();
        break;
    }
}

bool handle_map_key(uint32_t key, lv_event_t* e)
{
    if (s_map_target && (key == 'g' || key == 'G'))
    {
        auto* root = ::ui::widgets::map::widgets(s_map_runtime).root;
        if (root && !select_cache_at({lv_obj_get_width(root) / 2, lv_obj_get_height(root) / 2}))
            set_map_notice("No cache near map center", 1200);
        consume_key_event(e);
        return true;
    }
    // Selection exposes only pan/zoom/current-position, not unrelated route,
    // track or layer dialogs which could replace the temporary selection.
    if (s_location_request)
    {
        switch (key)
        {
        case 'a':
        case 'A':
        case LV_KEY_LEFT:
        case 'd':
        case 'D':
        case LV_KEY_RIGHT:
        case 'w':
        case 'W':
        case LV_KEY_UP:
        case 's':
        case 'S':
        case LV_KEY_DOWN:
        case 'q':
        case 'Q':
        case 'e':
        case 'E':
        case 'c':
        case 'C':
            break;
        default:
            return false;
        }
    }
    if (::ui::widgets::route_image_strip::handle_key(s_route_image_strip, key))
    {
        consume_key_event(e);
        request_refresh_view();
        return true;
    }
    if (s_route_image_strip_visible &&
        (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC))
    {
        s_route_image_strip_visible = false;
        sync_map_route_image_strip();
        set_map_notice("Images hidden", 900);
        consume_key_event(e);
        request_refresh_view();
        return true;
    }

    switch (key)
    {
    case kLvglFunctionKeyF1:
        if (!is_help_key(key))
        {
            return false;
        }
        consume_key_event(e);
        request_open_map_help_modal();
        return true;
    case 'a':
    case 'A':
    case LV_KEY_LEFT:
        s_map_pan_x += gps_ui::kMapPanStep;
        break;
    case 'd':
    case 'D':
    case LV_KEY_RIGHT:
        s_map_pan_x -= gps_ui::kMapPanStep;
        break;
    case 'w':
    case 'W':
    case LV_KEY_UP:
        s_map_pan_y += gps_ui::kMapPanStep;
        break;
    case 's':
    case 'S':
    case LV_KEY_DOWN:
        s_map_pan_y -= gps_ui::kMapPanStep;
        break;
    case 'q':
    case 'Q':
        adjust_map_zoom(-1);
        consume_key_event(e);
        return true;
    case 'e':
    case 'E':
        adjust_map_zoom(1);
        consume_key_event(e);
        return true;
    case 'i':
    case 'I':
        toggle_map_info_visibility();
        consume_key_event(e);
        return true;
    case 'c':
    case 'C':
        center_map_on_self();
        consume_key_event(e);
        return true;
    case 'p':
    case 'P':
        if (route_context_available() || route_image_context_active())
        {
            toggle_map_route_image_strip();
        }
        else
        {
            center_map_on_self();
        }
        consume_key_event(e);
        return true;
    case 'l':
    case 'L':
        cycle_map_layer();
        consume_key_event(e);
        return true;
    case 'o':
    case 'O':
        toggle_map_contour();
        consume_key_event(e);
        return true;
    case 't':
    case 'T':
        open_tracker_modal();
        consume_key_event(e);
        return true;
    case 'v':
    case 'V':
        toggle_route_elevation_profile();
        consume_key_event(e);
        return true;
    case 'h':
    case 'H':
        if (!is_help_key(key))
        {
            return false;
        }
        request_open_map_help_modal();
        consume_key_event(e);
        return true;
    case 'r':
    case 'R':
        show_route_context_notice();
        consume_key_event(e);
        return true;
    default:
        return false;
    }

    sync_workspace_viewport_from_renderer();
    request_refresh_view();
    consume_key_event(e);
    return true;
}

lv_obj_t* create_map_control_button(lv_obj_t* parent,
                                    lv_coord_t width,
                                    const char* text,
                                    MapControlAction action)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, kMapControlButtonHeight);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF8E6C3), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A6E43), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x25170D), 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn,
                        on_map_control_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
    bind_map_key_handler(btn);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_center(label);
    return btn;
}

void add_map_controls_to_group(lv_group_t* group)
{
    if (!group)
    {
        return;
    }
    if (s_map_pick_btn) lv_group_add_obj(group, s_map_pick_btn);
    if (s_map_cancel_btn) lv_group_add_obj(group, s_map_cancel_btn);
    if (s_map_zoom_out_btn) lv_group_add_obj(group, s_map_zoom_out_btn);
    if (s_map_zoom_in_btn) lv_group_add_obj(group, s_map_zoom_in_btn);
    if (s_map_center_btn) lv_group_add_obj(group, s_map_center_btn);
    if (s_map_layer_btn) lv_group_add_obj(group, s_map_layer_btn);
    if (s_map_contour_btn) lv_group_add_obj(group, s_map_contour_btn);
    if (s_map_tracker_btn) lv_group_add_obj(group, s_map_tracker_btn);
    if (s_map_help_btn) lv_group_add_obj(group, s_map_help_btn);
    if (map_control_visible(s_map_route_btn)) lv_group_add_obj(group, s_map_route_btn);
    for (lv_obj_t* member_btn : s_member_buttons)
    {
        if (map_control_visible(member_btn))
        {
            lv_group_add_obj(group, member_btn);
        }
    }
}

void create_map_control_bar(lv_obj_t* viewport)
{
    s_map_control_bar = lv_obj_create(viewport);
    lv_obj_set_size(s_map_control_bar, LV_PCT(100), kMapControlBarHeight);
    lv_obj_align(s_map_control_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(s_map_control_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_map_control_bar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_map_control_bar, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_map_control_bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_map_control_bar, 1, 0);
    lv_obj_set_style_border_color(s_map_control_bar, lv_color_hex(0xB3915D), 0);
    lv_obj_set_style_radius(s_map_control_bar, 0, 0);
    lv_obj_set_style_pad_left(s_map_control_bar, 3, 0);
    lv_obj_set_style_pad_right(s_map_control_bar, 3, 0);
    lv_obj_set_style_pad_top(s_map_control_bar, 2, 0);
    lv_obj_set_style_pad_bottom(s_map_control_bar, 2, 0);
    lv_obj_set_style_pad_column(s_map_control_bar, 3, 0);
    lv_obj_clear_flag(s_map_control_bar, LV_OBJ_FLAG_SCROLLABLE);
    bind_map_key_handler(s_map_control_bar);

    s_map_zoom_out_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "-",
        MapControlAction::ZoomOut);
    s_map_zoom_label = lv_label_create(s_map_control_bar);
    lv_label_set_text(s_map_zoom_label, "Z7");
    lv_obj_set_width(s_map_zoom_label, 24);
    lv_obj_set_style_text_font(s_map_zoom_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_map_zoom_label, lv_color_hex(0x3E2B18), 0);
    lv_obj_set_style_text_align(s_map_zoom_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_map_zoom_label, LV_LABEL_LONG_CLIP);

    s_map_zoom_in_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        "+",
        MapControlAction::ZoomIn);
    s_map_center_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonMediumWidth,
        "Pos",
        MapControlAction::Center);
    if (s_location_request)
    {
        s_map_pick_btn = create_map_control_button(s_map_control_bar, 66, ::ui::i18n::tr("Pick"), MapControlAction::PickLocation);
        s_map_cancel_btn = create_map_control_button(s_map_control_bar, 66, ::ui::i18n::tr("Cancel"), MapControlAction::CancelLocation);
        lv_obj_move_foreground(s_map_control_bar);
        return;
    }
    s_map_layer_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonMediumWidth,
        "OSM",
        MapControlAction::Layer);
    s_map_contour_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonContourWidth,
        "Contour",
        MapControlAction::Contour);
    s_map_tracker_btn = create_map_control_button(
        s_map_control_bar,
        tracker_button_width(),
        tracker_button_label(),
        MapControlAction::Tracker);
    s_map_help_btn = create_map_control_button(
        s_map_control_bar,
        kMapControlButtonSmallWidth,
        help_key_label(),
        MapControlAction::Help);
    lv_obj_move_foreground(s_map_control_bar);
}

void create_map_altitude_overlay(lv_obj_t* viewport)
{
    s_map_altitude_panel = lv_obj_create(viewport);
    lv_obj_set_size(s_map_altitude_panel, kMapAltitudePanelWidth, kMapAltitudePanelHeight);
    lv_obj_align(s_map_altitude_panel,
                 LV_ALIGN_BOTTOM_LEFT,
                 4,
                 -(kMapControlBarHeight + 4));
    lv_obj_add_flag(s_map_altitude_panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_map_altitude_panel, lv_color_hex(0x25170D), 0);
    lv_obj_set_style_bg_opa(s_map_altitude_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_altitude_panel, 0, 0);
    lv_obj_set_style_radius(s_map_altitude_panel, 4, 0);
    lv_obj_set_style_pad_left(s_map_altitude_panel, 5, 0);
    lv_obj_set_style_pad_right(s_map_altitude_panel, 5, 0);
    lv_obj_set_style_pad_top(s_map_altitude_panel, 1, 0);
    lv_obj_set_style_pad_bottom(s_map_altitude_panel, 1, 0);
    lv_obj_clear_flag(s_map_altitude_panel, LV_OBJ_FLAG_SCROLLABLE);
    bind_map_key_handler(s_map_altitude_panel);

    s_map_altitude_label = lv_label_create(s_map_altitude_panel);
    lv_label_set_text(s_map_altitude_label, "Alt --");
    lv_obj_set_width(s_map_altitude_label, kMapAltitudePanelWidth - 10);
    lv_obj_set_style_text_font(s_map_altitude_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_map_altitude_label, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_text_align(s_map_altitude_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_map_altitude_label, LV_LABEL_LONG_CLIP);
    lv_obj_center(s_map_altitude_label);
    lv_obj_move_foreground(s_map_altitude_panel);
}

void create_route_elevation_profile_overlay(lv_obj_t* viewport)
{
    ::ui::widgets::route_elevation_profile::create(
        viewport,
        s_route_elevation_profile,
        route_elevation_profile_config());
    if (s_route_elevation_profile.panel && lv_obj_is_valid(s_route_elevation_profile.panel))
    {
        lv_obj_align(s_route_elevation_profile.panel,
                     LV_ALIGN_BOTTOM_MID,
                     0,
                     -(kMapControlBarHeight + kRouteElevationPanelInset));
        bind_map_key_handler(s_route_elevation_profile.panel);
    }
}

void create_map_notice_overlay(lv_obj_t* viewport)
{
    s_map_notice_panel = lv_obj_create(viewport);
    lv_obj_set_size(s_map_notice_panel, LV_SIZE_CONTENT, 18);
    lv_obj_align(s_map_notice_panel, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_map_notice_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_map_notice_panel, lv_color_hex(0x25170D), 0);
    lv_obj_set_style_bg_opa(s_map_notice_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_notice_panel, 0, 0);
    lv_obj_set_style_radius(s_map_notice_panel, 4, 0);
    lv_obj_set_style_pad_left(s_map_notice_panel, 6, 0);
    lv_obj_set_style_pad_right(s_map_notice_panel, 6, 0);
    lv_obj_set_style_pad_top(s_map_notice_panel, 2, 0);
    lv_obj_set_style_pad_bottom(s_map_notice_panel, 2, 0);
    lv_obj_clear_flag(s_map_notice_panel, LV_OBJ_FLAG_SCROLLABLE);
    bind_map_key_handler(s_map_notice_panel);

    s_map_notice_label = lv_label_create(s_map_notice_panel);
    lv_label_set_text(s_map_notice_label, "");
    lv_obj_set_width(s_map_notice_label, 154);
    lv_obj_set_style_text_font(s_map_notice_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_map_notice_label, lv_color_hex(0xFFF3DF), 0);
    lv_label_set_long_mode(s_map_notice_label, LV_LABEL_LONG_DOT);
    lv_obj_center(s_map_notice_label);
}

void create_map_context_rail(lv_obj_t* viewport)
{
    s_map_context_rail = lv_obj_create(viewport);
    lv_obj_set_size(s_map_context_rail, kMapSideRailWidth + 4, LV_SIZE_CONTENT);
    lv_obj_align(s_map_context_rail, LV_ALIGN_TOP_RIGHT, -3, 4);
    lv_obj_set_flex_flow(s_map_context_rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_map_context_rail,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_map_context_rail, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_map_context_rail, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_map_context_rail, 1, 0);
    lv_obj_set_style_border_color(s_map_context_rail, lv_color_hex(0xB3915D), 0);
    lv_obj_set_style_radius(s_map_context_rail, 4, 0);
    lv_obj_set_style_pad_all(s_map_context_rail, 3, 0);
    lv_obj_set_style_pad_row(s_map_context_rail, 3, 0);
    lv_obj_clear_flag(s_map_context_rail, LV_OBJ_FLAG_SCROLLABLE);
    bind_map_key_handler(s_map_context_rail);

    s_map_route_btn = create_map_control_button(
        s_map_context_rail,
        kMapControlButtonWideWidth,
        "Route",
        MapControlAction::Route);
    set_hidden(s_map_route_btn, true);
    set_hidden(s_map_context_rail, true);
    lv_obj_move_foreground(s_map_context_rail);
}

void on_back(void*)
{
    request_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    const uint32_t key = lv_event_get_key(e);
    if (s_projection == Projection::Map && handle_map_key(key, e))
    {
        return;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC)
    {
        consume_key_event(e);
        request_exit();
    }
}

void clear_gps_status_labels()
{
    s_gps_status_label = nullptr;
    s_gps_coord_label = nullptr;
    s_gps_sat_label = nullptr;
    s_gps_alt_label = nullptr;
    s_gps_motion_label = nullptr;
    s_gps_time_label = nullptr;
    s_gps_diag_label = nullptr;
}

void create_gps_status_content(lv_obj_t* content)
{
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 10, 0);
    lv_obj_set_style_pad_right(panel, 10, 0);
    lv_obj_set_style_pad_top(panel, 5, 0);
    lv_obj_set_style_pad_bottom(panel, 4, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    create_status_row(panel, "Fix", &s_gps_status_label);
    create_status_row(panel, "Coord", &s_gps_coord_label);
    create_status_row(panel, "Sat", &s_gps_sat_label);
    create_status_row(panel, "Alt", &s_gps_alt_label);
    create_status_row(panel, "Motion", &s_gps_motion_label);
    create_status_row(panel, "Time", &s_gps_time_label);
    create_status_row(panel, "Diag", &s_gps_diag_label);

    refresh_gps_status_view();
}

void create_map_content(lv_obj_t* content)
{
    lv_obj_t* viewport = lv_obj_create(content);
    s_map_viewport = viewport;
    lv_obj_set_size(viewport, LV_PCT(100), 0);
    lv_obj_set_flex_grow(viewport, 1);
    lv_obj_set_style_bg_color(viewport, lv_color_hex(0xEAD9B2), 0);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_radius(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    bind_map_key_handler(viewport);

    const auto map_widgets = ::ui::widgets::map::create(s_map_runtime, viewport, 180);
    ::ui::widgets::map::set_gesture_callback(s_map_runtime, map_gesture_callback, nullptr);
    ::ui::widgets::map::set_gesture_enabled(s_map_runtime, true);
    sync_map_tile_loader_pause();
    lv_obj_update_layout(content);
    lv_obj_update_layout(viewport);
    ::ui::widgets::map::set_size(s_map_runtime,
                                 lv_obj_get_content_width(viewport),
                                 lv_obj_get_content_height(viewport));
    if (map_widgets.root)
    {
        lv_obj_align(map_widgets.root, LV_ALIGN_CENTER, 0, 0);
        bind_map_key_handler(map_widgets.root);
    }
    create_map_control_bar(viewport);
    if (s_location_request)
    {
        // The crosshair is a viewport sibling, not part of the panned tile or
        // annotation layer. It never owns or requests map tiles.
        auto* crosshair = lv_obj_create(viewport);
        lv_obj_remove_style_all(crosshair);
        lv_obj_set_size(crosshair, 22, 22);
        lv_obj_center(crosshair);
        lv_obj_remove_flag(crosshair, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(crosshair, LV_OBJ_FLAG_SCROLLABLE);
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            auto* line = lv_obj_create(crosshair);
            lv_obj_remove_style_all(line);
            lv_obj_set_size(line, axis ? 2 : 22, axis ? 22 : 2);
            lv_obj_set_style_bg_color(line, lv_color_hex(0x6B4A1E), 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_center(line);
            lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
        }
        refresh_view();
        return;
    }
    create_map_altitude_overlay(viewport);
    create_route_elevation_profile_overlay(viewport);
    create_map_notice_overlay(viewport);
    create_map_context_rail(viewport);

    if (!s_target_request && !s_map_target && route_context_available())
    {
        (void)load_configured_route_overlay(false);
    }
    refresh_view();
}

} // namespace

namespace gps::ui::runtime
{

void set_marker_binding(const ::ui::map::MapMarkerBinding* binding)
{
    ::ui::widgets::map::set_marker_binding(s_map_runtime, binding);
}

bool is_available()
{
    return platform::ui::device::gps_supported();
}

void remember_gps_view_state()
{
}

bool restore_gps_view_state()
{
    return s_map_view_initialized;
}

uint32_t selected_map_member_id()
{
    return s_selected_member_id;
}

bool load_map_track_file(const char* path, bool show_fail_toast)
{
    return load_map_track_file_impl(path, show_fail_toast);
}

void enter(const shell::Host* host, lv_obj_t* parent, shell::Projection projection,
           ::ui::map::MapLocationRequest* location, ::ui::map::MapTargetRequest* target)
{
    if (location) location->result = {0.0, 0.0, ::ui::map::MapLocationSelectionState::Cancelled};
    if (target) target->entered = false;
    if (target && (location || projection != Projection::Map || !target->valid())) return;
    if (projection == Projection::Map && !ensure_overlay_snapshot())
    {
        return;
    }
    if (target && !target->focus(map_workspace_model(), gps_ui::kDefaultZoom).ok)
    {
        release_overlay_snapshot();
        return;
    }

    if (!s_gps_power_lease_active)
    {
        platform::ui::gps::acquire_power_lease(projection == Projection::GpsStatus ? "lvgl-gps" : "lvgl-map");
        s_gps_power_lease_active = true;
    }
    s_host = host;
    s_location_request = projection == Projection::Map ? location : nullptr;
    s_target_request = target;
    s_projection = projection;
    clear_gps_status_labels();
    clear_map_controls();
    if (s_projection == Projection::Map)
    {
        s_map_info_visible = true;
    }
    if (s_projection == Projection::Map && !s_map_view_initialized)
    {
        s_map_zoom = kCardputerZeroMapDefaultZoom;
        s_map_pan_x = 0;
        s_map_pan_y = 0;
        s_map_view_initialized = true;
    }
    if (s_projection == Projection::Map)
    {
        if (s_target_request)
        {
            s_map_zoom = map_workspace_model().viewport().zoom;
            s_map_pan_x = s_map_pan_y = 0;
        }
        else
        {
            sync_workspace_viewport_from_renderer();
        }
        if (s_location_request)
        {
            auto& model = map_workspace_model();
            if (location->has_initial_viewport)
            {
                auto initial = location->initial_viewport;
                if (::ui::map_geo::valid(initial.center_lat, initial.center_lon))
                {
                    if (initial.zoom == 0) initial.zoom = gps_ui::kDefaultZoom;
                    (void)model.setViewport(initial);
                    s_map_zoom = initial.zoom;
                    s_map_pan_x = s_map_pan_y = 0;
                }
            }
            if (model.beginLocationSelection().ok) location->result = model.locationSelection();
        }
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    ::ui::widgets::TopBarConfig top_bar_config{};
    top_bar_config.height = ::ui::page_profile::current().top_bar_height;
    ::ui::widgets::top_bar_init(s_top_bar, s_root, top_bar_config);
    ::ui::widgets::top_bar_set_title(
        s_top_bar,
        ::ui::i18n::tr(s_location_request ? "Choose on map" : (s_projection == Projection::GpsStatus ? "GPS" : "Map")));
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);
    if (s_top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_top_bar);

    if (app_g && s_top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_top_bar.back_btn);
        lv_group_focus_obj(s_top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    if (s_projection == Projection::GpsStatus)
    {
        create_gps_status_content(content);
    }
    else
    {
        create_map_content(content);
        if (app_g)
        {
            add_map_controls_to_group(app_g);
            if (s_map_pick_btn) lv_group_focus_obj(s_map_pick_btn);
        }
    }
    gps_runtime_pump().setActive(true);
    if (s_target_request) s_target_request->entered = true;
    if (!s_timer)
    {
        s_timer = lv_timer_create(refresh_timer_cb, 750, nullptr);
    }
}

bool enter_target(const shell::Host* host, lv_obj_t* parent, const MapTarget& target)
{
    if (!parent || s_root || target.latitude_e7 < -900000000 || target.latitude_e7 > 900000000 ||
        target.longitude_e7 < -1800000000 || target.longitude_e7 >= 1800000000 || !is_available()) return false;
    s_map_target = &target;
    enter(host, parent, shell::Projection::Map);
    if (!s_root)
    {
        s_map_target = nullptr;
        return false;
    }
    auto viewport = map_workspace_model().viewport();
    viewport.center_lat = target.latitude_e7 / 10000000.0;
    viewport.center_lon = target.longitude_e7 / 10000000.0;
    viewport.zoom = current_map_zoom();
    (void)map_workspace_model().setViewport(viewport);
    s_map_pan_x = s_map_pan_y = 0;
    set_map_notice("Tap cache or G near map center", 3500);
    refresh_view();
    return true;
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    s_map_target = nullptr;
    if (s_target_request) s_target_request->entered = false;
    s_target_request = nullptr;

    if (s_location_request)
    {
        auto& model = map_workspace_model();
        if (model.locationSelection().state == ::ui::map::MapLocationSelectionState::Selecting)
            (void)model.cancelLocationSelection();
        s_location_request->result = model.locationSelection();
        s_location_request = nullptr;
    }

    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    gps_runtime_pump().setActive(false);
    if (s_projection == Projection::Map)
    {
        ::ui::widgets::map::destroy(s_map_runtime);
        release_overlay_snapshot();
    }
    s_map_tile_loader_paused = false;
    clear_gps_status_labels();
    clear_map_controls();
    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    // The LVGL object tree owns every label in the top bar. Clear this
    // non-owning view immediately after deleting the tree so a stale callback
    // cannot bind or render through a freed label.
    s_top_bar = {};
    const bool was_gps_status = s_projection == Projection::GpsStatus;
    s_host = nullptr;
    s_projection = Projection::Map;
    if (s_gps_power_lease_active)
    {
        platform::ui::gps::release_power_lease(was_gps_status ? "lvgl-gps" : "lvgl-map");
        s_gps_power_lease_active = false;
    }
}

} // namespace gps::ui::runtime
