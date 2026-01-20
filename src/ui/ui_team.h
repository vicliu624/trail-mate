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
