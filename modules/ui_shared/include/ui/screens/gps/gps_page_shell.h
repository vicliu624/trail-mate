/**
 * @file gps_page_shell.h
 * @brief Shared GPS page shell entrypoints
 */

#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

#include <stdint.h>

namespace ui::map
{
struct MapLocationRequest;
struct MapTargetRequest;
struct MapMarkerBinding;
} // namespace ui::map

namespace gps::ui::shell
{

using Host = ::ui::page::Host;

enum class Projection : uint8_t
{
    Map = 0,
    GpsStatus,
};

struct RouteSpec
{
    const Host* host = nullptr;
    Projection projection = Projection::Map;
    ::ui::map::MapLocationRequest* location = nullptr;
    ::ui::map::MapTargetRequest* target = nullptr;
    const ::ui::map::MapMarkerBinding* markers = nullptr;
};

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);
void enter_route(const RouteSpec* spec, lv_obj_t* parent);
void exit_route(const RouteSpec* spec, lv_obj_t* parent);

} // namespace gps::ui::shell
