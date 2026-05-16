#include "ui_presentation/layout/layout_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* profiles = ui::presentation::allLayoutProfiles(&count);
    assert(profiles != nullptr);
    assert(count == 8);

    const auto* pager = ui::presentation::findLayoutProfile("pager_compact");
    assert(pager != nullptr);
    assert(pager->screen_width == 480);
    assert(pager->screen_height == 222);
    assert(pager->use_compact_rows);
    assert(pager->use_keyboard_shortcuts);

    const auto* watch = ui::presentation::findLayoutProfile("watch_compact");
    assert(watch != nullptr);
    assert(watch->use_watch_cards);

    const auto* desktop = ui::presentation::findLayoutProfile("uconsole_desktop");
    assert(desktop != nullptr);
    assert(desktop->use_side_nav);
    assert(desktop->use_keyboard_shortcuts);

    const auto* headless = ui::presentation::findLayoutProfile("headless_node");
    assert(headless != nullptr);
    assert(headless->max_visible_menu_items == 0);

    assert(ui::presentation::findLayoutProfile("unknown") == nullptr);
    return 0;
}
