#include "popup_test_support.h"
namespace agenda_test_ui
{
bool overlay = false;
bool interruption = false;
bool transition = false;
unsigned wakes = 0;
} // namespace agenda_test_ui
bool ui_is_overlay_active() { return agenda_test_ui::overlay; }
void ui_set_overlay_active(bool value) { agenda_test_ui::overlay = value; }
bool ui_is_interruption_app_active() { return agenda_test_ui::interruption; }
bool ui_is_transition_pending() { return agenda_test_ui::transition; }
namespace platform::ui::screen
{
void wake_for_modal() { ++agenda_test_ui::wakes; }
} // namespace platform::ui::screen
