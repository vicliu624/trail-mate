#pragma once
#include "ui/screens/agenda/agenda_state.h"

namespace ui::agenda::page::components
{
void create(lv_obj_t* parent);
void render();
void updateCountdowns();
void destroy();
lv_obj_t* addLabel(lv_obj_t* parent, const char* text, bool translated = true, bool caption = false);
lv_obj_t* addButton(lv_obj_t* parent, const char* text, Action action, uint8_t value = 0);
} // namespace ui::agenda::page::components
