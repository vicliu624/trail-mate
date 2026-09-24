#pragma once
#include "ui/screens/agenda/agenda_state.h"

namespace ui::agenda::page::runtime
{
void enter(const Host* host, lv_obj_t* parent);
void exit();
void request(Action action, uint8_t row = 0);
void refresh();
} // namespace ui::agenda::page::runtime
