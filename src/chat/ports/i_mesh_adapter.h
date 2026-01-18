/**
 * @file i_mesh_adapter.h
 * @brief Mesh adapter interface
 */

#pragma once

#include "../domain/chat_types.h"

namespace chat {

/**
 * @brief Mesh adapter interface
 * Abstracts mesh protocol implementation (Meshtastic, custom, etc.)
 */
class IMeshAdapter {
public:
    virtual ~IMeshAdapter() = default;
    
    /**
     * @brief Send text message
     * @param channel Channel ID
     * @param text Message text
     * @param out_msg_id Output message ID (if successful)
     * @return true if queued successfully
     */
    virtual bool sendText(ChannelId channel, const std::string& text, 
                         MessageId* out_msg_id, NodeId peer = 0) = 0;
    
    /**
     * @brief Poll for incoming text messages
     * @param out Output message (if available)
     * @return true if message available
     */
    virtual bool pollIncomingText(MeshIncomingText* out) = 0;
    
    /**
     * @brief Apply mesh configuration
     * @param config Configuration to apply
     */
    virtual void applyConfig(const MeshConfig& config) = 0;
    
    /**
     * @brief Check if adapter is ready
     */
    virtual bool isReady() const = 0;
};

} // namespace chat
