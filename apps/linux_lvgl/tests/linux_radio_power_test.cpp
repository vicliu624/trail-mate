#include "platform/linux/radio_power.h"
#include <cassert>
#include <memory>

using namespace platform::linux_runtime;
int main()
{
    auto owner = std::make_shared<RadioPowerRequests>();
    assert(!owner->request(false));
    owner->activate(true, true, true);
    bind_radio_power(owner);
    assert(request_radio_enabled(false));
    auto state = radio_power_snapshot();
    assert(state.changing && state.enabled); // Accepted is not applied.
    const auto first_generation = state.generation;
    assert(request_radio_enabled(false));
    assert(owner->snapshot().generation == first_generation);
    RadioPowerWork first, next;
    assert(owner->take(first) && !first.enabled);
    assert(!owner->take(next));
    assert(request_radio_enabled(true));
    assert(!owner->current(first.generation));
    owner->complete(first, true, true, false, "foreground owns radio");
    state = owner->snapshot();
    assert(state.changing && !state.failed && state.enabled);
    assert(owner->take(next) && next.enabled);
    owner->complete(next, true, true, true, "");
    assert(!owner->snapshot().changing);
    assert(request_radio_enabled(false));
    assert(owner->take(next));
    owner->complete(next, true, true, false, "foreground owns radio");
    state = radio_power_snapshot();
    assert(state.failed && state.enabled && !state.changing);
    assert(state.error == "foreground owns radio");
    assert(request_radio_enabled(false));
    assert(owner->take(next));
    owner->complete(next, false, false, false, "setting could not be saved");
    state = owner->snapshot();
    assert(state.failed && !state.enabled); // Report actual state on persistence failure.
    auto replacement = std::make_shared<RadioPowerRequests>();
    replacement->activate(true, true, false);
    bind_radio_power(replacement);
    unbind_radio_power(owner); // Old owner cannot remove a newer binding.
    assert(radio_power_snapshot().enabled);
    replacement->deactivate();
    assert(!request_radio_enabled(false));
    assert(!radio_power_snapshot().available && !radio_power_snapshot().ready);
    unbind_radio_power(replacement);
    assert(!request_radio_enabled(true));
    assert(!radio_power_snapshot().available);
}
