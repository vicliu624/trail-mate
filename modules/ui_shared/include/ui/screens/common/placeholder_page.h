#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace ui::placeholder_page
{

struct State
{
    const ui::page::Host* host = nullptr;
    const char* title = nullptr;
    const char* body = nullptr;
    lv_obj_t* root = nullptr;
};

void show(State& state, lv_obj_t* parent);
void hide(State& state);

void enter_callback(void* user_data, lv_obj_t* parent);
void exit_callback(void* user_data, lv_obj_t* parent);

} // namespace ui::placeholder_page
