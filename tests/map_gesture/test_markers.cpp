#include "ui/widgets/map/map_viewport.h"
#include "ui_presentation/map/map_marker_cache.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace ui::fonts
{
const lv_font_t* content_font(const char*, const lv_font_t* font) { return font; }
} // namespace ui::fonts
namespace ui::widgets::map
{
#include "map_marker_state.inc"
struct RuntimeImpl
{
    Widgets widgets;
    Model model;
    struct
    {
        bool valid = true;
    } anchor;
    const ui::map::MapMarkerBinding* marker_binding = nullptr;
    MarkerSession* markers = nullptr;
    uint32_t marker_retry_at = 0;
    int marker_drag_dx = 0;
    int marker_drag_dy = 0;
};
RuntimeImpl* fixture = nullptr;
bool is_runtime_alive(const RuntimeImpl& impl) { return lv_obj_is_valid(impl.widgets.root); }
bool project_geo_point(const RuntimeImpl& impl, const GeoPoint& point, lv_point_t& out)
{
    if (!point.valid) return false;
    out.x = static_cast<int>(std::round(point.lon * 1e7)) + impl.model.pan_x + 160;
    out.y = static_cast<int>(std::round(point.lat * 1e7)) + impl.model.pan_y + 120;
    return true;
}
#include "map_marker_actual.inc"
Widgets create(Runtime& runtime, lv_obj_t* parent, uint32_t)
{
    fixture = runtime.impl_ = new RuntimeImpl{};
    auto* surface = lv_obj_create(parent);
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, 320, 240);
    lv_obj_clear_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(surface);
    fixture->widgets.root = fixture->widgets.overlay_layer = surface;
    fixture->model.focus_point = {true, 0, 0};
    return fixture->widgets;
}
void destroy(Runtime& runtime)
{
    if (!runtime.impl_) return;
    set_marker_binding(runtime, nullptr);
    lv_obj_delete(runtime.impl_->widgets.root);
    delete runtime.impl_;
    runtime.impl_ = nullptr;
    fixture = nullptr;
}
Runtime::~Runtime() { destroy(*this); }
} // namespace ui::widgets::map
namespace
{
unsigned allocations = 0;
unsigned releases = 0;
bool allocation_failure = false;
void* allocate(std::size_t bytes)
{
    ++allocations;
    return allocation_failure ? nullptr : std::malloc(bytes);
}
void release(void* pointer)
{
    ++releases;
    std::free(pointer);
}
struct Source : ui::map::IMapMarkerSource
{
    ui::map::MapMarkerStatus state{100, 1, true, true};
    unsigned scans = 0;
    unsigned count = 3;
    bool fail = false;
    ui::map::MapMarkerStatus markerStatus() const override { return state; }
    bool visitMarkers(ui::map::MapMarkerVisitor visit, void* context, void*, std::size_t) override
    {
        ++scans;
        for (unsigned i = 0; i < count; ++i)
        {
            ui::map::MapMarker marker;
            marker.id = i + 1;
            marker.longitude_e7 = static_cast<int>(i * 5);
            marker.active_until = i ? 200 : 50;
            std::strcpy(marker.title, "Event");
            visit(marker, context);
        }
        return !fail;
    }
};
} // namespace
int main()
{
    using namespace ui::widgets::map;
    lv_init();
    auto* display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    Source source;
    const ui::map::MapMarkerBinding binding{&source, allocate, release};
    {
        Runtime runtime;
        create(runtime, lv_screen_active(), 50);
        set_marker_binding(runtime, &binding);
        assert(allocations == 1 && source.scans == 1 && fixture->markers->cache.count == 3);
        for (int i = 0; i < 20; ++i)
        {
            refresh_markers(*fixture);
            lv_obj_invalidate(fixture->widgets.root);
            lv_refr_now(display);
        }
        assert(source.scans == 1 && allocations == 1);
        assert(lv_obj_get_child_count(fixture->widgets.root) == 0); // Direct draw, no per-marker widgets.
        fixture->model.pan_x = 40;
        refresh_markers(*fixture);
        assert(source.scans == 1); // Still inside the query margin.
        fixture->model.pan_x = 65;
        refresh_markers(*fixture);
        assert(source.scans == 2);
        fixture->model.zoom++;
        refresh_markers(*fixture);
        assert(source.scans == 3);
        source.count = 40;
        fixture->model.pan_x = 0;
        ++source.state.revision;
        refresh_markers(*fixture);
        assert(fixture->markers->cache.count == 32 && fixture->markers->cache.truncated);
        const auto before_move = source.scans;
        fixture->model.pan_x = -10;
        refresh_markers(*fixture);
        assert(source.scans == before_move + 1); // Overflow selection follows the viewport.
        source.fail = true;
        ++source.state.revision;
        refresh_markers(*fixture);
        assert(!fixture->markers->cache.valid && fixture->markers->cache.count == 0);
        const auto failed_scan = source.scans;
        refresh_markers(*fixture);
        assert(source.scans == failed_scan); // Read failure retry is bounded.
        source.fail = false;
        lv_tick_inc(3001);
        refresh_markers(*fixture);
        assert(fixture->markers->cache.valid && source.scans == failed_scan + 1);
        source.state.ready = false;
        refresh_markers(*fixture);
        assert(!fixture->markers && releases == 1);
        source.state.ready = true;
        ++source.state.revision;
        allocation_failure = true;
        refresh_markers(*fixture);
        const auto failed_allocation = allocations;
        refresh_markers(*fixture);
        assert(!fixture->markers && allocations == failed_allocation);
        allocation_failure = false;
        lv_tick_inc(3001);
        refresh_markers(*fixture);
        assert(fixture->markers);
        source.state.now = 201;
        const auto before_expiry = source.scans;
        refresh_markers(*fixture);
        assert(source.scans == before_expiry + 1);
    }
    assert(releases == 2 && fixture == nullptr);
    lv_display_delete(display);
    std::printf("Production marker query/draw/binding checks passed; session=%u bytes\n", static_cast<unsigned>(sizeof(MarkerSession)));
}
