#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_budget.h"

#include <cassert>
#include <cstring>

namespace
{

using chat::lxmf::runtime::makeRuntimeBudget;
using chat::lxmf::runtime::RuntimeBudgetInput;

void assertPhase(const char* actual, const char* expected)
{
    assert(actual != nullptr);
    assert(std::strcmp(actual, expected) == 0);
}

} // namespace

int main()
{
    RuntimeBudgetInput input{};
    input.max_ingress_packets_per_poll = 4;
    input.call_ingress_packets_per_poll = 8;

    input.call_realtime_active = true;
    auto budget = makeRuntimeBudget(input);
    assertPhase(budget.phase, "call");
    assert(budget.live_packet_limit == 8);
    assert(!budget.allow_announce_tx);
    assert(!budget.allow_propagation_client);
    assert(budget.drop_public_discovery);

    input = RuntimeBudgetInput{};
    input.max_ingress_packets_per_poll = 4;
    input.call_ingress_packets_per_poll = 8;
    input.nomad_request_active = true;
    budget = makeRuntimeBudget(input);
    assertPhase(budget.phase, "nomad");
    assert(budget.live_packet_limit == 4);
    assert(!budget.allow_peer_projection);
    assert(!budget.allow_announce_tx);

    input = RuntimeBudgetInput{};
    input.screen_sleeping = true;
    budget = makeRuntimeBudget(input);
    assertPhase(budget.phase, "sleep");
    assert(budget.live_packet_limit == 1);
    assert(budget.deferred_discovery_limit == 1);
    assert(budget.allow_public_discovery);
    assert(!budget.allow_persistence);
    assert(budget.allow_propagation_client);

    input = RuntimeBudgetInput{};
    input.screen_saver_active = true;
    budget = makeRuntimeBudget(input);
    assertPhase(budget.phase, "saver");
    assert(budget.live_packet_limit == 1);
    assert(budget.deferred_discovery_limit == 0);
    assert(budget.drop_public_discovery);
    assert(budget.allow_propagation_client);
    assert(!budget.allow_announce_tx);
    assert(!budget.allow_persistence);

    input.screen_sleeping = true;
    budget = makeRuntimeBudget(input);
    assert(budget.allow_propagation_client);
    input.call_realtime_active = true;
    budget = makeRuntimeBudget(input);
    assertPhase(budget.phase, "call");
    assert(!budget.allow_propagation_client);
    input.call_realtime_active = false;
    input.nomad_request_active = true;
    budget = makeRuntimeBudget(input);
    assertPhase(budget.phase, "nomad");
    assert(!budget.allow_propagation_client);

    input = RuntimeBudgetInput{};
    input.max_ingress_packets_per_poll = 4;
    budget = makeRuntimeBudget(input);
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    assertPhase(budget.phase, "p4_screen");
    assert(budget.deferred_discovery_limit == 4);
    assert(budget.allow_public_discovery);
    assert(budget.allow_persistence);
#else
    assertPhase(budget.phase, "screen");
    assert(budget.deferred_discovery_limit == 0);
    assert(!budget.allow_public_discovery);
    assert(!budget.allow_persistence);
#endif
    assert(budget.allow_peer_projection);
    assert(budget.allow_announce_tx);
    assert(budget.allow_propagation_client);

    return 0;
}
