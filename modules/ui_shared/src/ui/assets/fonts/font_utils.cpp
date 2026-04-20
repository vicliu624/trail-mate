#include "ui/assets/fonts/font_utils.h"

namespace ui::fonts
{
namespace
{

constexpr std::size_t kMaxLocalizedFontBindings = 32;
LocalizedFontBinding s_localized_font_bindings[kMaxLocalizedFontBindings]{};

} // namespace

LocalizedFontBinding* localized_font_binding_storage()
{
    return s_localized_font_bindings;
}

std::size_t localized_font_binding_storage_size()
{
    return kMaxLocalizedFontBindings;
}

} // namespace ui::fonts
