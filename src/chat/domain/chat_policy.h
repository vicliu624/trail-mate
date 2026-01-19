/**
 * @file chat_policy.h
 * @brief Chat policy for outdoor/outdoor-optimized behavior
 */

#pragma once

#include <Arduino.h>

namespace chat
{

/**
 * @brief Chat policy configuration
 * Defines behavior for outdoor/outdoor-optimized scenarios
 */
struct ChatPolicy
{
    bool enable_relay;         // Enable message relay/forwarding
    uint8_t hop_limit_default; // Default hop limit (1-3)
    bool ack_for_broadcast;    // Require ACK for broadcast messages
    bool ack_for_squad;        // Require ACK for squad messages
    uint8_t max_tx_retries;    // Maximum TX retries (outdoor: keep low)
    uint8_t max_channels;      // Maximum number of channels

    /**
     * @brief Get default outdoor policy
     * Optimized for low power, low frequency, event-driven communication
     */
    static ChatPolicy outdoor()
    {
        ChatPolicy policy;
        policy.enable_relay = true;       // Enable relay for mesh
        policy.hop_limit_default = 2;     // 2 hops default
        policy.ack_for_broadcast = false; // No ACK for broadcast (save airtime)
        policy.ack_for_squad = true;      // ACK for squad (more reliable)
        policy.max_tx_retries = 1;        // Minimal retries (outdoor: be quiet)
        policy.max_channels = 3;          // Max 3 channels
        return policy;
    }

    /**
     * @brief Get default policy (same as outdoor for now)
     */
    static ChatPolicy defaults()
    {
        return outdoor();
    }
};

} // namespace chat
