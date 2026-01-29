/**
 * @file team_page_components.h
 * @brief Team page components (public interface)
 */

#pragma once

#include "lvgl.h"
#include "team_ui_store.h"

namespace sys
{
struct Event;
}

void team_page_create(lv_obj_t* parent);
void team_page_destroy();
void team_page_refresh();
bool team_page_handle_event(sys::Event* event);
