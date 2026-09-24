#pragma once
#include "ui/screens/agenda/agenda_state.h"

namespace ui::agenda::page::input
{
void begin();
void bind(lv_obj_t* object);
void end();
void activate(lv_event_t* event);
} // namespace ui::agenda::page::input
