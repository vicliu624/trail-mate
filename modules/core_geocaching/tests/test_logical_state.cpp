#include "geocaching/storage/logical_state.h"
int main()
{
    using namespace geocaching::storage;
    uint8_t first[32]{}, second[32]{};
    LogicalState state(first, second, sizeof(first));
    uint8_t key = 1, other_key = 2, value = 9;
    MutationView initial{5, {&key, 1}, {&value, 1}, false};
    auto accept = [](const LogicalState::View&)
    { return true; };
    if (!state.apply(&initial, 1, accept)) return 1;
    MutationView changes[] = {{5, {&key, 1}, {}, true}, {5, {&other_key, 1}, {&value, 1}, false}};
    if (state.apply(changes, 2, [](const LogicalState::View&)
                    { return false; })) return 2;
    geocaching::ByteView found;
    if (!state.view().find(5, {&key, 1}, found) || found.size != 1 || found.data[0] != 9 || state.view().size() != 1) return 3;
    if (!state.apply(changes, 2, accept) || state.view().find(5, {&key, 1}, found) ||
        !state.view().find(5, {&other_key, 1}, found)) return 4;
    uint8_t large[32]{};
    MutationView overflow{5, {&key, 1}, {large, sizeof(large)}, false};
    if (state.apply(&overflow, 1, accept) || state.view().size() != 1 || !state.view().find(5, {&other_key, 1}, found)) return 5;
    return 0;
}
