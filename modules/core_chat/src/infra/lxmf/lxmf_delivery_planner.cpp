/**
 * @file lxmf_delivery_planner.cpp
 * @brief Reticulum/LXMF outbound delivery route decision owner.
 */

#include "chat/infra/lxmf/lxmf_delivery_planner.h"

namespace chat::lxmf::runtime
{

OutboundDeliveryPlan ReticulumDeliveryPlanner::plan(
    const OutboundDeliveryPlanInput& input)
{
    OutboundDeliveryPlan result{};
    const bool propagation_only =
        input.propagation_enabled &&
        input.propagation_preference ==
            chat::reticulum::LxmfDeliveryPreference::Propagated;
    const bool propagation_automatic =
        input.propagation_enabled &&
        input.propagation_preference ==
            chat::reticulum::LxmfDeliveryPreference::Automatic &&
        !input.has_active_link && !input.peer_has_usable_ratchet &&
        input.propagation_peer_available;

    result.propagation_only = propagation_only;
    result.propagation_first = propagation_only || propagation_automatic;
    result.may_fallback_to_link = !propagation_only;

    if (result.propagation_first)
    {
        result.path = OutboundDeliveryPath::Propagation;
    }
    else if (input.has_active_link)
    {
        result.path = OutboundDeliveryPath::Link;
    }
    else if (input.peer_has_usable_ratchet)
    {
        result.path = OutboundDeliveryPath::Opportunistic;
    }
    else
    {
        result.path = OutboundDeliveryPath::DeferredLink;
    }

    return result;
}

const char* ReticulumDeliveryPlanner::pathName(OutboundDeliveryPath path)
{
    switch (path)
    {
    case OutboundDeliveryPath::Link:
        return "link";
    case OutboundDeliveryPath::Opportunistic:
        return "opportunistic";
    case OutboundDeliveryPath::DeferredLink:
        return "deferred_link";
    case OutboundDeliveryPath::Propagation:
        return "propagation";
    case OutboundDeliveryPath::None:
        return "none";
    }
    return "none";
}

} // namespace chat::lxmf::runtime
