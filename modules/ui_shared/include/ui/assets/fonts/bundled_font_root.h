#pragma once
#include <string>

namespace ui::i18n
{
// Configure before registry access on the LVGL owner. Bundled disk fonts are
// cataloged after user fonts, loaded on demand and never copied into user data.
// Reconfiguration after registry initialization is allowed only for the same root.
bool configure_bundled_font_root(const std::string& root);
} // namespace ui::i18n
