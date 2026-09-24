#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"
#include "ui_presentation/agenda/agenda_action_sink.h"
#include "ui_presentation/agenda/agenda_location_source.h"
#include "ui_presentation/agenda/agenda_source.h"

namespace ui::waypoint
{
class ISource;
class IActionSink;
} // namespace ui::waypoint

namespace ui::agenda::page
{
// Supplied by target composition. Only portable presentation contracts cross
// this boundary; the page never mounts storage or constructs an Agenda service.
struct Host
{
    ::ui::page::Host navigation{};
    IAgendaSource* source = nullptr;
    IAgendaActionSink* actions = nullptr;
    IAgendaLocationSource* locations = nullptr;
    void* map_context = nullptr;
    bool (*request_map)(void*, const AgendaEditorModel&, bool return_detail) = nullptr;
    bool (*request_target)(void*, const AgendaEditorModel&) = nullptr;
    ::ui::waypoint::ISource* waypoints = nullptr;
    ::ui::waypoint::IActionSink* waypoint_actions = nullptr;
};

void enter(void* user_data, lv_obj_t* parent);
void exit(void* user_data, lv_obj_t* parent);
bool canPresentReminder();
} // namespace ui::agenda::page
