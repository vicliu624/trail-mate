/**
 * @file lxmf_delivery_planner.h
 * @brief Shared Reticulum/LXMF outbound delivery route decision owner.
 */

#pragma once

#include "chat/domain/reticulum_network_config.h"

namespace chat::lxmf::runtime
{

enum class OutboundDeliveryPath : uint8_t
{
    None = 0,
    Link,
    Opportunistic,
    DeferredLink,
    Propagation,
};

struct OutboundDeliveryPlanInput
{
    bool has_active_link = false;
    bool peer_has_usable_ratchet = false;
    bool propagation_enabled = false;
    chat::reticulum::LxmfDeliveryPreference propagation_preference =
        chat::reticulum::LxmfDeliveryPreference::Automatic;
    bool propagation_peer_available = false;
};

struct OutboundDeliveryPlan
{
    OutboundDeliveryPath path = OutboundDeliveryPath::None;
    bool propagation_only = false;
    bool propagation_first = false;
    bool may_fallback_to_link = true;
};

class ReticulumDeliveryPlanner
{
  public:
    ReticulumDeliveryPlanner() = default;
    ReticulumDeliveryPlanner(const ReticulumDeliveryPlanner&) = delete;
    ReticulumDeliveryPlanner& operator=(const ReticulumDeliveryPlanner&) = delete;
    ReticulumDeliveryPlanner(ReticulumDeliveryPlanner&&) = delete;
    ReticulumDeliveryPlanner& operator=(ReticulumDeliveryPlanner&&) = delete;

    static OutboundDeliveryPlan plan(const OutboundDeliveryPlanInput& input);
    static const char* pathName(OutboundDeliveryPath path);
};

} // namespace chat::lxmf::runtime
