#include "ui_presentation/target_ui_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* profiles = ui::presentation::allTargetUiProfiles(&count);
    assert(profiles != nullptr);
    assert(count == 8);

    const auto* tab5 = ui::presentation::findTargetUiProfile("tab5_touch_ui");
    assert(tab5 != nullptr);
    assert(tab5->display_class == ui::presentation::DisplayClass::LargeTouch);
    assert(tab5->input_class == ui::presentation::InputClass::TouchOnly);
    assert(std::strcmp(tab5->page_manifest_id, "tab5_touch_manifest") == 0);

    const auto* deck = ui::presentation::findTargetUiProfile("deck_wide_ui");
    assert(deck != nullptr);
    assert(deck->has_physical_keyboard);
    assert(deck->has_trackball);

    const auto* uconsole = ui::presentation::findTargetUiProfile("uconsole_desktop_ui");
    assert(uconsole != nullptr);
    assert(uconsole->has_pointer);
    assert(uconsole->has_side_nav);

    const auto* node = ui::presentation::findTargetUiProfile("node_headless_ui");
    assert(node != nullptr);
    assert(node->display_class == ui::presentation::DisplayClass::Headless);
    assert(node->layout_class == ui::presentation::LayoutClass::HeadlessNode);

    assert(ui::presentation::findTargetUiProfile("unknown") == nullptr);
    return 0;
}
