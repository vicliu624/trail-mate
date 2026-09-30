/**
 * @file lxmf_runtime_budget.cpp
 * @brief Runtime scheduling policy for the embedded Reticulum/LXMF adapter.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_budget.h"

namespace chat::lxmf::runtime
{

RuntimeBudget makeRuntimeBudget(const RuntimeBudgetInput& input)
{
    RuntimeBudget budget{};
    if (input.call_realtime_active)
    {
        budget.live_packet_limit = input.call_ingress_packets_per_poll;
        budget.deferred_discovery_limit = 0;
        budget.allow_public_discovery = false;
        budget.allow_persistence = false;
        budget.allow_peer_projection = false;
        budget.allow_announce_tx = false;
        budget.allow_propagation_client = false;
        budget.drop_public_discovery = true;
        budget.phase = "call";
        return budget;
    }

    if (input.nomad_request_active)
    {
        budget.live_packet_limit = input.max_ingress_packets_per_poll;
        budget.deferred_discovery_limit = 0;
        budget.allow_public_discovery = false;
        budget.allow_persistence = false;
        budget.allow_peer_projection = false;
        budget.allow_announce_tx = false;
        budget.allow_propagation_client = false;
        budget.drop_public_discovery = true;
        budget.phase = "nomad";
        return budget;
    }

    const bool maintenance_window =
        input.screen_sleeping && !input.screen_saver_active;
    if (maintenance_window)
    {
        budget.live_packet_limit = 1;
        budget.deferred_discovery_limit = 1;
        budget.allow_public_discovery = true;
        budget.allow_persistence = false;
        budget.allow_peer_projection = false;
        budget.allow_announce_tx = false;
        // Propagation retrieval is already rate-limited by the configured
        // sync interval. Keep it available while the display sleeps so a
        // queued private LXMF can be received and auto-replied to.
        budget.allow_propagation_client = true;
        budget.phase = "sleep";
        return budget;
    }

    if (input.screen_saver_active)
    {
        budget.live_packet_limit = 1;
        budget.deferred_discovery_limit = 0;
        budget.allow_public_discovery = false;
        budget.allow_persistence = false;
        budget.allow_peer_projection = false;
        budget.allow_announce_tx = false;
        // Screen presentation must not freeze queued uploads or their expiry.
        // The client already advances stamp work incrementally and limits sync.
        budget.allow_propagation_client = true;
        budget.drop_public_discovery = true;
        budget.phase = "saver";
        return budget;
    }

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // P4 runs the shared mesh task independently of LVGL and has sufficient
    // compute for foreground announce verification/projection. Keeping the
    // S3 sleep-only discovery policy here leaves an always-lit P4 with an
    // eight-packet deferred queue that can never drain into Contacts/Network.
    budget.live_packet_limit = input.max_ingress_packets_per_poll;
    budget.deferred_discovery_limit = input.max_ingress_packets_per_poll;
    budget.allow_public_discovery = true;
    budget.allow_persistence = true;
    budget.allow_peer_projection = true;
    budget.allow_announce_tx = true;
    budget.allow_propagation_client = true;
    budget.phase = "p4_screen";
    return budget;
#endif

    budget.live_packet_limit = input.max_ingress_packets_per_poll;
    budget.deferred_discovery_limit = 0;
    budget.allow_public_discovery = false;
    budget.allow_persistence = false;
    budget.allow_peer_projection = !input.screen_saver_active;
    budget.allow_announce_tx = true;
    budget.allow_propagation_client = true;
    budget.drop_public_discovery = true;
    budget.phase = "screen";
    return budget;
}

} // namespace chat::lxmf::runtime
