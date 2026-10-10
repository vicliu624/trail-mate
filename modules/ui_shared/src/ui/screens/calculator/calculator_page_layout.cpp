#include "ui/screens/calculator/calculator_page_layout.h"

#include "ui/top_bar_metrics.h"
#include <algorithm>

namespace calculator::ui::layout
{
namespace
{

constexpr lv_coord_t kPagerFallbackWidth = 480;
constexpr lv_coord_t kPagerFallbackHeight = 222;
constexpr lv_coord_t kLargeTouchMaximumContentWidth = 860;

lv_coord_t dimensionOrFallback(lv_obj_t* parent, bool width)
{
    const lv_coord_t dimension = parent ? (width ? lv_obj_get_width(parent) : lv_obj_get_height(parent)) : 0;
    if (dimension > 0)
    {
        return dimension;
    }
    return width ? kPagerFallbackWidth : kPagerFallbackHeight;
}

} // namespace

Geometry resolve(lv_obj_t* parent)
{
    Geometry geometry{};
    geometry.width = dimensionOrFallback(parent, true);
    geometry.height = dimensionOrFallback(parent, false);

    const bool deck_compact = geometry.width <= 360;
    geometry.large_touch = !deck_compact && geometry.height >= 300;
    geometry.outer_margin = deck_compact ? 6 : (geometry.large_touch ? 18 : 7);
    geometry.content_width = geometry.large_touch
                                 ? std::min<lv_coord_t>(geometry.width - geometry.outer_margin * 2,
                                                        kLargeTouchMaximumContentWidth)
                                 : geometry.width;
    geometry.content_x = (geometry.width - geometry.content_width) / 2;
    geometry.top_bar_height = ::ui::kUnifiedTopBarHeight;
    geometry.display_y = geometry.top_bar_height + 4;
    geometry.display_height = deck_compact ? 48 : (geometry.large_touch ? 74 : 52);
    geometry.function_y = geometry.display_y + geometry.display_height + 4;
    geometry.function_height = deck_compact ? 20 : (geometry.large_touch ? 30 : 23);
    geometry.function_gap = geometry.large_touch ? 5 : 3;
    geometry.keypad_y = geometry.function_y + geometry.function_height + 4;
    geometry.keypad_gap = geometry.large_touch ? 5 : 2;
    geometry.footer_height = geometry.large_touch ? 18 : 12;

    const lv_coord_t usable_height = std::max<lv_coord_t>(
        5,
        geometry.height - geometry.keypad_y - geometry.footer_height - 4 - geometry.keypad_gap * 4);
    geometry.keypad_button_height = std::max<lv_coord_t>(
        deck_compact ? 17 : (geometry.large_touch ? 24 : 17), usable_height / 5);
    geometry.footer_y = geometry.keypad_y + geometry.keypad_button_height * 5 + geometry.keypad_gap * 4 + 3;
    return geometry;
}

} // namespace calculator::ui::layout
