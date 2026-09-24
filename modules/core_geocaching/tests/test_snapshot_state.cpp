#include "geocaching/storage/logical_state.h"
int main()
{
    using namespace geocaching::storage;
    uint8_t a[64]{}, b[64]{}, key = 1, next_key = 2, value = 9;
    LogicalState state(a, b, sizeof(a));
    MutationView first{5, {&key, 1}, {&value, 1}, false}, second{5, {&next_key, 1}, {}, false};
    auto accept = [](const auto&) { return true; };
    if (!state.apply(&first, 1, accept) || !state.beginSnapshot() || !state.appendSnapshot(&second, 1)) return 1;
    geocaching::ByteView found;
    if (!state.view().find(5, {&key, 1}, found) || state.apply(&second, 1, accept) ||
        state.commitSnapshot([](const auto&) { return false; })) return 2;
    if (!state.commitSnapshot(accept) || state.view().find(5, {&key, 1}, found) || !state.view().find(5, {&next_key, 1}, found)) return 3;
    if (!state.beginSnapshot() || !state.appendSnapshot(&second, 1) || state.appendSnapshot(&first, 1) || state.commitSnapshot(accept)) return 4;
    state.discardSnapshot();
    if (!state.view().find(5, {&next_key, 1}, found)) return 5;
    return 0;
}
