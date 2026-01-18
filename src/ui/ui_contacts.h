/**
 * @file ui_contacts.h
 * @brief Contacts page entry point
 */

#pragma once

#include "lvgl.h"

/**
 * @brief Enter contacts page
 * @param parent Parent object (usually main screen)
 */
void ui_contacts_enter(lv_obj_t* parent);

/**
 * @brief Exit contacts page
 * @param parent Parent object (can be nullptr)
 */
void ui_contacts_exit(lv_obj_t* parent);
