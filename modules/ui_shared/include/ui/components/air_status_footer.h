#pragma once

#include "lvgl.h"

namespace ui::components::air_status_footer
{

struct Footer
{
    lv_obj_t* container = nullptr;
    lv_obj_t* summary_label = nullptr;
    lv_obj_t* detail_label = nullptr;
};

Footer create(lv_obj_t* parent);
void refresh(Footer& footer);

} // namespace ui::components::air_status_footer
