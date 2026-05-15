#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/map/map_workspace_model.h"

#include <cstdio>

namespace
{

const char* onOff(bool value)
{
    return value ? "on" : "off";
}

const char* toolName(ui::map::MapToolKind tool)
{
    switch (tool)
    {
    case ui::map::MapToolKind::None:
        return "None";
    case ui::map::MapToolKind::Pan:
        return "Pan";
    case ui::map::MapToolKind::CenterOnSelf:
        return "CenterOnSelf";
    case ui::map::MapToolKind::MeasureDistance:
        return "MeasureDistance";
    default:
        return "Unknown";
    }
}

} // namespace

int main()
{
    ui::tests::FakeMapPresentationSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.self.valid = true;
    source.snapshot_value.self.lat = 52.4068;
    source.snapshot_value.self.lon = -1.5197;
    source.snapshot_value.self.accuracy_m = 4.0f;
    source.snapshot_value.layers.osm = true;
    source.snapshot_value.layers.terrain = false;
    source.snapshot_value.layers.satellite = false;
    source.snapshot_value.layers.contour = true;
    source.snapshot_value.team.available = true;
    source.snapshot_value.team.visible_members = 3;
    source.snapshot_value.team.stale_members = 1;
    source.snapshot_value.can_center_on_self = true;
    ui::copyText(source.snapshot_value.status_line, "GPS fix");

    ui::tests::FakeMapActionSink sink;
    ui::map::MapWorkspaceModel model(source, sink);

    ui::map::MapViewport viewport;
    viewport.center_lat = 52.4068;
    viewport.center_lon = -1.5197;
    viewport.zoom = 12;
    (void)model.setViewport(viewport);
    (void)model.setActiveTool(ui::map::MapToolKind::Pan);

    const auto snapshot = model.snapshot();
    if (!snapshot.header.valid)
    {
        std::printf("MAP: unavailable\n");
        return 1;
    }

    std::printf("MAP CENTER: %.4f,%.4f z=%u\n",
                snapshot.viewport.center_lat,
                snapshot.viewport.center_lon,
                static_cast<unsigned>(snapshot.viewport.zoom));
    std::printf("SELF: %s %.4f,%.4f accuracy=%.1fm\n",
                snapshot.self.valid ? "FIX" : "NO FIX",
                snapshot.self.lat,
                snapshot.self.lon,
                static_cast<double>(snapshot.self.accuracy_m));
    std::printf("LAYERS: OSM %s / Terrain %s / Satellite %s / Contour %s\n",
                onOff(snapshot.layers.osm),
                onOff(snapshot.layers.terrain),
                onOff(snapshot.layers.satellite),
                onOff(snapshot.layers.contour));
    std::printf("TOOL: %s\n", toolName(snapshot.active_tool));
    std::printf("TEAM: %u visible / %u stale\n",
                static_cast<unsigned>(snapshot.team.visible_members),
                static_cast<unsigned>(snapshot.team.stale_members));
    std::printf("STATUS: %s\n", snapshot.status_line.c_str());
    return 0;
}
