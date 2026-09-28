#include "screen_app_internal.h"

#include "ui/screens/geocaching/geocaching_page_shell.h"
#include "ui/widgets/map/map_viewport.h"

#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

constexpr lv_coord_t kMapViewportSize = 240;
constexpr lv_coord_t kMapActionTop = 242;
constexpr lv_coord_t kMapStatusTop = 263;
constexpr lv_coord_t kMapStatusHeight = 15;

struct MapPageState
{
    ::ui::widgets::map::Runtime runtime{};
    ::ui::widgets::map::Widgets widgets{};
    ::ui::map::MapWorkspaceSnapshot snapshot{};
    ::ui::map::MapOverlaySnapshot overlay{};
    ::ui::widgets::map::Model model{};
    lv_obj_t* stage = nullptr;
    lv_obj_t* status = nullptr;
    int drag_start_pan_x = 0;
    int drag_start_pan_y = 0;
    int pan_x = 0;
    int pan_y = 0;
    bool drag_active = false;
    bool refresh_scheduled = false;
    bool geocaching_active = false;
    lv_timer_t* overlay_timer = nullptr;
};

MapPageState* s_map_page_storage = nullptr;
bool s_map_page_allocation_failed_logged = false;

MapPageState* allocate_map_page_state()
{
#if defined(ESP_PLATFORM)
    void* const storage = heap_caps_malloc(sizeof(MapPageState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const storage = std::malloc(sizeof(MapPageState));
#endif
    return storage ? new (storage) MapPageState{} : nullptr;
}

bool ensure_map_page_state()
{
    if (s_map_page_storage != nullptr)
    {
        return true;
    }

    s_map_page_storage = allocate_map_page_state();
    if (s_map_page_storage != nullptr)
    {
        return true;
    }

    if (!s_map_page_allocation_failed_logged)
    {
        s_map_page_allocation_failed_logged = true;
        std::printf("[UI][Map] page enter denied reason=psram_state_alloc bytes=%u\n",
                    static_cast<unsigned>(sizeof(MapPageState)));
    }
    return false;
}

void release_map_page_state()
{
    if (s_map_page_storage == nullptr)
    {
        return;
    }

    destroy_map_page();
    s_map_page_storage->~MapPageState();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_map_page_storage);
#else
    std::free(s_map_page_storage);
#endif
    s_map_page_storage = nullptr;
}

// Map snapshots, gesture bookkeeping and widget handles are page-scoped.
// Keeping them live for the entire firmware lifetime wastes internal DRAM.
#define s_map_page (*s_map_page_storage)

::ui::presentation_sources::RuntimeMapWorkspaceSource& map_source()
{
    static ::ui::presentation_sources::RuntimeMapWorkspaceSource source(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state(),
        &::team::ui::team_ui_snapshot_store());
    return source;
}

::ui::presentation_sources::RuntimeMapActionSink& map_sink()
{
    static ::ui::presentation_sources::RuntimeMapActionSink sink(
        ::ui::presentation_sources::runtime_gps_status_source(),
        ::ui::presentation_sources::runtime_map_workspace_state());
    return sink;
}

const char* map_source_short_name(uint8_t source)
{
    switch (source)
    {
    case 1:
        return "TERRAIN";
    case 2:
        return "SAT";
    case 0:
    default:
        return "OSM";
    }
}

void sync_workspace_layers_from_viewport()
{
    const ::ui::widgets::map::LayerState layers = ::ui::widgets::map::current_layer_state();
    auto& workspace = ::ui::presentation_sources::runtime_map_workspace_state();
    workspace.layers.osm = layers.map_source == 0U;
    workspace.layers.terrain = layers.map_source == 1U;
    workspace.layers.satellite = layers.map_source == 2U;
    workspace.layers.contour = layers.contour_enabled;
}

bool has_explicit_map_center()
{
    return ::ui::presentation_sources::runtime_map_workspace_state().has_viewport;
}

::ui::widgets::map::Model build_map_model()
{
    const ::ui::map::MapWorkspaceSnapshot& snapshot = s_map_page.snapshot;
    const auto& workspace = ::ui::presentation_sources::runtime_map_workspace_state();
    const ::ui::widgets::map::LayerState layers = ::ui::widgets::map::current_layer_state();
    const auto& config = ::app::configFacade().readConfig();

    ::ui::widgets::map::Model model{};
    model.focus_point.valid = has_explicit_map_center() || snapshot.self.valid;
    model.focus_point.lat = has_explicit_map_center() ? snapshot.viewport.center_lat : snapshot.self.lat;
    model.focus_point.lon = has_explicit_map_center() ? snapshot.viewport.center_lon : snapshot.self.lon;
    model.zoom = snapshot.viewport.zoom == 0U ? ::ui::widgets::map::kDefaultZoom
                                              : static_cast<int>(snapshot.viewport.zoom);
    model.pan_x = s_map_page.pan_x;
    model.pan_y = s_map_page.pan_y;
    model.map_source = layers.map_source;
    model.contour_enabled = layers.contour_enabled;
    model.coord_system = config.map_coord_system;
    (void)workspace;
    return model;
}

void build_position_overlay()
{
    s_map_page.overlay = ::ui::map::MapOverlaySnapshot{};
    if (!s_map_page.snapshot.self.valid)
    {
        return;
    }

    s_map_page.overlay.header.valid = true;
    s_map_page.overlay.header.version = 1U;
    s_map_page.overlay.header.generated_at_ms = ::sys::millis_now();
    s_map_page.overlay.item_count = 1U;
    ::ui::map::MapOverlayItem& item = s_map_page.overlay.items[0];
    item.kind = ::ui::map::MapOverlayKind::CurrentPosition;
    item.style = ::ui::map::MapOverlayStyle::OwnPosition;
    item.point.valid = true;
    item.point.lat = s_map_page.snapshot.self.lat;
    item.point.lon = s_map_page.snapshot.self.lon;
    item.stable_id = 1U;
    item.visible = true;
}

void ensure_map_viewport()
{
    if (valid(s_map_page.stage))
    {
        return;
    }

    s_map_page.stage = lv_obj_create(s_state.root);
    lv_obj_set_pos(s_map_page.stage, 0, 0);
    lv_obj_set_size(s_map_page.stage, kMapViewportSize, kMapViewportSize);
    style_paper(s_map_page.stage);
#ifdef LV_OBJ_FLAG_CLIP_CHILDREN
    lv_obj_add_flag(s_map_page.stage, LV_OBJ_FLAG_CLIP_CHILDREN);
#endif

    s_map_page.widgets = ::ui::widgets::map::create(s_map_page.runtime, s_map_page.stage, 160U);
    ::ui::widgets::map::set_size(s_map_page.runtime, kMapViewportSize, kMapViewportSize);
    ::ui::widgets::map::set_gesture_callback(s_map_page.runtime, nullptr, nullptr);
    ::ui::widgets::map::set_gesture_enabled(s_map_page.runtime, false);

    s_map_page.status = create_text(s_state.root, kContentWidth, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_map_page.status, kMargin, kMapStatusTop);
    lv_obj_set_height(s_map_page.status, kMapStatusHeight);
}

void schedule_map_refresh(void*)
{
    s_map_page.refresh_scheduled = false;
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == PageKind::Map)
    {
        refresh_page();
    }
}

void request_map_refresh()
{
    if (s_map_page.refresh_scheduled)
    {
        return;
    }
    s_map_page.refresh_scheduled = true;
    if (lv_async_call(schedule_map_refresh, nullptr) != LV_RESULT_OK)
    {
        s_map_page.refresh_scheduled = false;
    }
}

void map_gesture_callback(const ::ui::widgets::map::GestureEvent& event, void*)
{
    switch (event.phase)
    {
    case ::ui::widgets::map::GesturePhase::Pressed:
        s_map_page.drag_start_pan_x = s_map_page.pan_x;
        s_map_page.drag_start_pan_y = s_map_page.pan_y;
        s_map_page.drag_active = false;
        break;
    case ::ui::widgets::map::GesturePhase::DragBegin:
        s_map_page.drag_active = true;
        break;
    case ::ui::widgets::map::GesturePhase::DragUpdate:
        s_map_page.drag_active = true;
        s_map_page.pan_x = s_map_page.drag_start_pan_x + event.total_dx;
        s_map_page.pan_y = s_map_page.drag_start_pan_y + event.total_dy;
        s_map_page.model = build_map_model();
        ::ui::widgets::map::apply_model_lightweight(s_map_page.runtime, s_map_page.model);
        break;
    case ::ui::widgets::map::GesturePhase::DragEnd:
    case ::ui::widgets::map::GesturePhase::Cancel:
        if (s_map_page.drag_active)
        {
            ::ui::widgets::map::GeoPoint center{};
            if (::ui::widgets::map::screen_center(s_map_page.runtime, center) && center.valid)
            {
                ::ui::map::MapViewport viewport = s_map_page.snapshot.viewport;
                viewport.center_lat = center.lat;
                viewport.center_lon = center.lon;
                viewport.zoom = static_cast<uint8_t>(s_map_page.model.zoom);
                (void)map_sink().setViewport(viewport);
                set_notice("MAP MOVED");
            }
        }
        s_map_page.pan_x = 0;
        s_map_page.pan_y = 0;
        s_map_page.drag_active = false;
        request_map_refresh();
        break;
    }
}

void configure_map_gesture()
{
    // The physical keyboard is always available; touch panning is additive on
    // targets whose display exposes a pointer input device.
    ::ui::widgets::map::set_gesture_callback(s_map_page.runtime, map_gesture_callback, nullptr);
    ::ui::widgets::map::set_gesture_enabled(s_map_page.runtime, true);
}

void update_map_status()
{
    const ::ui::widgets::map::Status viewport = ::ui::widgets::map::status(s_map_page.runtime);
    const ::ui::widgets::map::LayerState layers = ::ui::widgets::map::current_layer_state();
    const char* status = s_state.notice[0] != '\0' ? s_state.notice : nullptr;
    if (status == nullptr)
    {
        if (!s_map_page.model.focus_point.valid)
        {
            status = "NO GPS FIX: CENTER UNAVAILABLE";
        }
        else if (!viewport.has_visible_map_data)
        {
            status = "TILES LOADING / NOT CACHED";
        }
        else
        {
            std::snprintf(s_state.scratch,
                          sizeof(s_state.scratch),
                          "%s  Z%d  %s%s",
                          map_source_short_name(layers.map_source),
                          s_map_page.model.zoom,
                          s_map_page.snapshot.self.valid ? "GPS FIX" : "SAVED CENTER",
                          s_map_page.snapshot.team.available ? "  TEAM" : "");
            status = s_state.scratch;
        }
    }
    set_text(s_map_page.status, status);
    set_text(s_state.footer, "W/S FOCUS ENTER ACT BKSP BACK");
}

void cycle_map_base_layer()
{
    const ::ui::widgets::map::LayerState current = ::ui::widgets::map::current_layer_state();
    const uint8_t next_source = static_cast<uint8_t>((current.map_source + 1U) % 3U);
    ::ui::widgets::map::LayerNotice notice{};
    const bool changed = ::ui::widgets::map::set_layer_map_source(next_source, &notice);
    sync_workspace_layers_from_viewport();
    if (notice.has_message)
    {
        set_notice(notice.message);
    }
    else if (changed)
    {
        std::snprintf(s_state.scratch,
                      sizeof(s_state.scratch),
                      "BASE %s",
                      map_source_short_name(next_source));
        set_notice(s_state.scratch);
    }
    else
    {
        set_notice("MAP LAYER UNAVAILABLE");
    }
}

} // namespace

void refresh_local_cache_overlays(lv_timer_t*)
{
    if (!s_map_page_storage || s_map_page.drag_active || !s_map_page.model.focus_point.valid) return;
    build_position_overlay();
    ::geocaching::ui::shell::appendMapOverlays(s_map_page.overlay,
                                               s_map_page.model.focus_point.lat, s_map_page.model.focus_point.lon,
                                               static_cast<uint8_t>(s_map_page.model.zoom));
    ::ui::widgets::map::apply_overlay(s_map_page.runtime, s_map_page.overlay);
}

void reset_map_page_state()
{
    release_map_page_state();
}

void destroy_map_page()
{
    if (s_map_page_storage == nullptr)
    {
        return;
    }
    s_map_page.refresh_scheduled = false;
    if (s_map_page.overlay_timer) lv_timer_delete(s_map_page.overlay_timer);
    s_map_page.overlay_timer = nullptr;
    if (s_map_page.geocaching_active) ::geocaching::ui::shell::endMapOverlays();
    s_map_page.geocaching_active = false;
    ::ui::widgets::map::destroy(s_map_page.runtime);
    s_map_page.widgets = ::ui::widgets::map::Widgets{};
    s_map_page.stage = nullptr;
    s_map_page.status = nullptr;
    s_map_page.drag_active = false;
}

void render_map()
{
    if (!ensure_map_page_state())
    {
        set_text(s_state.title, "MAP");
        set_text(s_state.subtitle, "MEMORY");
        set_line(0, "PSRAM REQUIRED FOR MAP");
        set_line(1, "RETURN AFTER MEMORY RECOVERS");
        clear_lines_from(2);
        return;
    }
    ensure_map_viewport();
    if (!s_map_page.geocaching_active)
    {
        ::geocaching::ui::shell::beginMapOverlays();
        s_map_page.geocaching_active = true;
        s_map_page.overlay_timer = lv_timer_create(refresh_local_cache_overlays, 750, nullptr);
    }
    sync_workspace_layers_from_viewport();

    ::ui::map::MapWorkspaceRequest request{};
    const auto& workspace = ::ui::presentation_sources::runtime_map_workspace_state();
    request.requested_viewport = workspace.last_viewport;
    request.active_tool = workspace.active_tool;
    if (!map_source().buildMapWorkspaceSnapshot(request, s_map_page.snapshot))
    {
        set_notice("MAP RUNTIME UNAVAILABLE");
        ::ui::widgets::map::clear(s_map_page.runtime);
        update_map_status();
        return;
    }

    s_map_page.model = build_map_model();
    if (!s_map_page.model.focus_point.valid)
    {
        ::ui::widgets::map::clear(s_map_page.runtime);
        update_map_status();
        return;
    }

    if (s_map_page.drag_active)
    {
        ::ui::widgets::map::apply_model_lightweight(s_map_page.runtime, s_map_page.model);
    }
    else
    {
        ::ui::widgets::map::apply_model(s_map_page.runtime, s_map_page.model);
        build_position_overlay();
        ::geocaching::ui::shell::appendMapOverlays(s_map_page.overlay,
                                                   s_map_page.model.focus_point.lat, s_map_page.model.focus_point.lon,
                                                   static_cast<uint8_t>(s_map_page.model.zoom));
        ::ui::widgets::map::apply_overlay(s_map_page.runtime, s_map_page.overlay);
    }
    configure_map_gesture();
    update_map_status();
}

void add_map_actions()
{
    add_action("CENTER", Action::CenterOnSelf, 4, kMapActionTop, 56);
    add_action("ZOOM+", Action::ZoomIn, 62, kMapActionTop, 54);
    add_action("ZOOM-", Action::ZoomOut, 118, kMapActionTop, 54);
    add_action("LAYER", Action::ToggleTerrain, 174, kMapActionTop, 62);
}

bool handle_map_action(Action action)
{
    if (!ensure_map_page_state())
    {
        set_notice("MAP MEMORY UNAVAILABLE");
        return action != Action::Back;
    }
    switch (action)
    {
    case Action::CenterOnSelf:
        set_notice(map_sink().centerOnSelf().ok ? "CENTERED ON SELF" : "NO GPS FIX");
        return true;
    case Action::ZoomIn:
    case Action::ZoomOut:
    {
        ::ui::map::MapViewport viewport = s_map_page.snapshot.viewport;
        if (viewport.zoom == 0U)
        {
            viewport.zoom = static_cast<uint8_t>(::ui::widgets::map::kDefaultZoom);
        }
        if (action == Action::ZoomIn && viewport.zoom < ::ui::widgets::map::kMaxZoom)
        {
            ++viewport.zoom;
        }
        else if (action == Action::ZoomOut && viewport.zoom > ::ui::widgets::map::kMinZoom)
        {
            --viewport.zoom;
        }
        set_notice(map_sink().setViewport(viewport).ok ? "MAP ZOOM UPDATED" : "ZOOM UNAVAILABLE");
        return true;
    }
    case Action::ToggleTerrain:
        cycle_map_base_layer();
        return true;
    default:
        return false;
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
