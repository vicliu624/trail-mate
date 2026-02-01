/**
 * @file ui_team.h
 * @brief Team page entry point
 */

#pragma once

#include "lvgl.h"

/**
 * @brief Enter Team page
 * @param parent Parent object (usually main screen)
 */
void ui_team_enter(lv_obj_t* parent);

/**
 * @brief Exit Team page
 * @param parent Parent object (can be nullptr)
 */
void ui_team_exit(lv_obj_t* parent);

namespace sys
{
struct Event;
}

/**
 * @brief Handle team-related events (updates UI state, may refresh screen)
 * @param event Event pointer (ownership stays with caller)
 * @return true if handled
 */
bool ui_team_handle_event(sys::Event* event);
