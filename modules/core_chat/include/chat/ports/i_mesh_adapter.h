/**
 * @file i_mesh_adapter.h
 * @brief Mesh adapter interface
 */

#pragma once

#include "../domain/chat_types.h"
#include <memory>

namespace chat
{
namespace meshcore
{
class IMeshCoreBleBackend;
}

struct MeshCapabilities
{
    bool supports_unicast_text = false;
    bool supports_unicast_appdata = false;
    bool supports_broadcast_appdata = false;
    bool supports_appdata_ack = false;
    bool provides_appdata_sender = false;
    bool supports_node_info = false;
    bool supports_pki = false;
    bool supports_discovery_actions = false;
};

/**
 * @brief Mesh adapter interface
 * Abstracts mesh protocol implementation (Meshtastic, custom, etc.)
 */
class IMeshAdapter
{
  public:
    virtual ~IMeshAdapter() = default;

    /**
     * @brief Get adapter capabilities
     */
    virtual MeshCapabilities getCapabilities() const
    {
        return MeshCapabilities{};
    }

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
     * @brief Send text message with a caller-supplied message ID if supported
     * @param channel Channel ID
     * @param text Message text
     * @param forced_msg_id Preferred message ID (0 = auto)
     * @param out_msg_id Output message ID (if successful)
     * @param peer Destination node (0 for broadcast)
     * @return true if queued successfully
     */
    virtual bool sendTextWithId(ChannelId channel, const std::string& text,
                                MessageId forced_msg_id,
                                MessageId* out_msg_id, NodeId peer = 0)
    {
        (void)forced_msg_id;
        return sendText(channel, text, out_msg_id, peer);
    }

    /**
     * @brief Poll for incoming text messages
     * @param out Output message (if available)
     * @return true if message available
     */
    virtual bool pollIncomingText(MeshIncomingText* out) = 0;

    /**
     * @brief Send app payload data (non-text)
     * @param channel Channel ID
     * @param portnum Application port number
     * @param payload Payload bytes
     * @param len Payload length
     * @param dest Destination node (0 for broadcast)
     * @param want_ack Request ACK if supported
     * @param packet_id Optional packet id hint (0 = auto)
     * @param want_response Request app-level response if supported
     * @return true if queued successfully
     */
    virtual bool sendAppData(ChannelId channel, uint32_t portnum,
                             const uint8_t* payload, size_t len,
                             NodeId dest = 0, bool want_ack = false,
                             MessageId packet_id = 0,
                             bool want_response = false) = 0;

    /**
     * @brief Poll for incoming app payload data
     * @param out Output data (if available)
     * @return true if data available
     */
    virtual bool pollIncomingData(MeshIncomingData* out) = 0;

    /**
     * @brief Request NodeInfo from a specific node (if supported)
     * @param dest Destination node (0 for broadcast)
     * @param want_response Request response if supported
     * @return true if request queued
     */
    virtual bool requestNodeInfo(NodeId dest, bool want_response)
    {
        (void)dest;
        (void)want_response;
        return false;
    }

    /**
     * @brief Start PKI key verification with a remote node (if supported)
     * @param dest Destination node
     * @return true if started
     */
    virtual bool startKeyVerification(NodeId dest)
    {
        (void)dest;
        return false;
    }

    /**
     * @brief Submit PKI verification number (if supported)
     * @param dest Destination node
     * @param nonce Verification nonce
     * @param number Security number
     * @return true if accepted
     */
    virtual bool submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
    {
        (void)dest;
        (void)nonce;
        (void)number;
        return false;
    }

    /**
     * @brief Get local node ID (if supported)
     */
    virtual NodeId getNodeId() const
    {
        return 0;
    }

    /**
     * @brief Check if PKI is ready (if supported)
     */
    virtual bool isPkiReady() const
    {
        return false;
    }

    /**
     * @brief Check if PKI public key for node is known (if supported)
     */
    virtual bool hasPkiKey(NodeId dest) const
    {
        (void)dest;
        return false;
    }

    /**
     * @brief Trigger protocol-specific discovery action (if supported)
     */
    virtual bool triggerDiscoveryAction(MeshDiscoveryAction action)
    {
        (void)action;
        return false;
    }

    /**
     * @brief Apply mesh configuration
     * @param config Configuration to apply
     */
    virtual void applyConfig(const MeshConfig& config) = 0;

    /**
     * @brief Update user identity (long/short name)
     */
    virtual void setUserInfo(const char* long_name, const char* short_name)
    {
        (void)long_name;
        (void)short_name;
    }

    /**
     * @brief Apply network utilization limits
     */
    virtual void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
    {
        (void)duty_cycle_enabled;
        (void)util_percent;
    }

    /**
     * @brief Apply privacy configuration
     */
    virtual void setPrivacyConfig(uint8_t encrypt_mode)
    {
        (void)encrypt_mode;
    }

    /**
     * @brief Check if adapter is ready
     */
    virtual bool isReady() const = 0;

    /**
     * @brief Poll for incoming raw packet data
     * @param out_data Output buffer for raw packet data
     * @param out_len Output packet length
     * @param max_len Maximum buffer size
     * @return true if raw packet data is available
     */
    virtual bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) = 0;

    /**
     * @brief Handle raw packet data from the radio task
     * @param data Raw packet data
     * @param size Packet size
     */
    virtual void handleRawPacket(const uint8_t* data, size_t size)
    {
        (void)data;
        (void)size;
    }

    /**
     * @brief Update last RX signal stats (optional)
     */
    virtual void setLastRxStats(float rssi, float snr)
    {
        (void)rssi;
        (void)snr;
    }

    /**
     * @brief Install or swap an active backend (optional runtime host support)
     */
    virtual bool installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
    {
        (void)protocol;
        (void)backend;
        return false;
    }

    /**
     * @brief Check whether a hosted backend is present (optional runtime host support)
     */
    virtual bool hasBackend() const
    {
        return false;
    }

    /**
     * @brief Report the currently active backend protocol (optional runtime host support)
     */
    virtual MeshProtocol backendProtocol() const
    {
        return MeshProtocol::Meshtastic;
    }

    /**
     * @brief Access a hosted backend for a specific protocol (optional runtime host support)
     */
    virtual IMeshAdapter* backendForProtocol(MeshProtocol protocol)
    {
        (void)protocol;
        return nullptr;
    }

    /**
     * @brief Const access to a hosted backend for a specific protocol (optional runtime host support)
     */
    virtual const IMeshAdapter* backendForProtocol(MeshProtocol protocol) const
    {
        (void)protocol;
        return nullptr;
    }

    virtual meshcore::IMeshCoreBleBackend* asMeshCoreBleBackend()
    {
        return nullptr;
    }

    virtual const meshcore::IMeshCoreBleBackend* asMeshCoreBleBackend() const
    {
        return nullptr;
    }

    /**
     * @brief Process any pending send queue work
     */
    virtual void processSendQueue() {}
};

} // namespace chat
