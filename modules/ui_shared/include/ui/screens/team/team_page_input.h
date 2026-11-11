/**
 * @file team_page_input.h
 * @brief Team page input handling
 */

#pragma once

#include "lvgl.h"
#include "ui/widgets/top_bar.h"

#include <vector>

namespace team
{
namespace ui
{

struct TeamPageInputContext
{
    lv_obj_t* root = nullptr;
    lv_obj_t* body = nullptr;
    ::ui::widgets::TopBar* top_bar = nullptr;
    std::vector<lv_obj_t*>* focusables = nullptr;
    lv_obj_t** default_focus = nullptr;
};

void init_team_input(const TeamPageInputContext& context);
void refresh_team_input(const TeamPageInputContext& context);
void cleanup_team_input();

} // namespace ui
} // namespace team
