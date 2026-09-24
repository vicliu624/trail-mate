#include "ui/screens/agenda/agenda_state.h"
#include <new>

namespace ui::agenda::page
{
namespace
{
State* current = nullptr;
}
State* state() { return current; }
bool canPresentReminder()
{
    return !current || (!current->awaiting_command && current->pending == Action::None &&
                        !current->confirm_delete && !current->confirm_discard);
}
bool createState()
{
    if (current) return false;
    current = new (std::nothrow) State;
    return current != nullptr;
}
void destroyState()
{
    delete current;
    current = nullptr;
}
} // namespace ui::agenda::page
