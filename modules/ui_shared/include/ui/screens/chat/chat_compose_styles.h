#pragma once
#include "chat_compose_layout.h"
#include "lvgl.h"

namespace chat::ui::compose::styles
{

void init_once();
void apply_all(const layout::Widgets& w);

} // namespace chat::ui::compose::styles
