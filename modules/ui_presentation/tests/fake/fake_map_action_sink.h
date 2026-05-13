#pragma once

#include "ui_presentation/map/map_action_sink.h"

namespace ui::tests
{

class FakeMapActionSink final : public ui::map::IMapActionSink
{
  public:
    ui::UiActionResult centerOnSelf() override
    {
        ++center_on_self_count;
        return center_on_self_result;
    }

    ui::UiActionResult setViewport(
        const ui::map::MapViewport& viewport) override
    {
        ++set_viewport_count;
        last_viewport = viewport;
        return set_viewport_result;
    }

    ui::UiActionResult setLayer(ui::map::MapLayerKind layer,
                                bool enabled) override
    {
        ++set_layer_count;
        last_layer = layer;
        last_layer_enabled = enabled;
        return set_layer_result;
    }

    ui::UiActionResult setActiveTool(ui::map::MapToolKind tool) override
    {
        ++set_active_tool_count;
        last_tool = tool;
        return set_active_tool_result;
    }

    ui::UiActionResult clearMeasurement() override
    {
        ++clear_measurement_count;
        return clear_measurement_result;
    }

    int center_on_self_count = 0;
    int set_viewport_count = 0;
    int set_layer_count = 0;
    int set_active_tool_count = 0;
    int clear_measurement_count = 0;

    ui::map::MapViewport last_viewport{};
    ui::map::MapLayerKind last_layer = ui::map::MapLayerKind::Osm;
    bool last_layer_enabled = false;
    ui::map::MapToolKind last_tool = ui::map::MapToolKind::Pan;

    ui::UiActionResult center_on_self_result = ui::UiActionResult::success();
    ui::UiActionResult set_viewport_result = ui::UiActionResult::success();
    ui::UiActionResult set_layer_result = ui::UiActionResult::success();
    ui::UiActionResult set_active_tool_result = ui::UiActionResult::success();
    ui::UiActionResult clear_measurement_result = ui::UiActionResult::success();
};

} // namespace ui::tests
