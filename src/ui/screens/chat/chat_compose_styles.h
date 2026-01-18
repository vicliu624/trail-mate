#pragma once
#include "lvgl.h"
#include "chat_compose_layout.h"

namespace chat::ui::compose::styles {

void init_once();
void apply_all(const layout::Widgets& w);

} // namespace chat::ui::compose::styles
