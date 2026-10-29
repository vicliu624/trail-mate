/**
 * @file i_mesh_adapter.h
 * @brief Mesh adapter interface
 */

#pragma once

#include "../domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/ports/device_author.h"
#include "chat/ports/device_identity_restore.h"
#include "chat/ports/track_binding_registration.h"
#include <memory>

namespace track_sharing
{
struct ManifestView;
}
namespace hostlink::management
{
struct IdentityRestoreView;
struct RadioIdentityRestoreView;
} // namespace hostlink::management
namespace chat
{
class IIncomingDeliveryCommitPort;
class IGeocachingTransport;
class ITrackTransport;
namespace lxmf
{
struct TrackDeliveryBinding;
}

namespace meshcore
{
class IMeshCoreBleBackend;
}

struct MeshCapabilities
{
    bool supports_unicast_text = false;
    bool supports_reticulum_destination_text = false;
    bool supports_unicast_appdata = false;
    bool supports_broadcast_appdata = false;
    bool supports_appdata_ack = false;
    bool provides_appdata_sender = false;
    bool supports_node_info = false;
    bool supports_pki = false;
    bool supports_discovery_actions = false;
    bool supports_node_info_query = false;
    bool supports_node_info_reply = false;
    bool supports_node_info_reannounce = false;
    bool supports_position_request = false;
    bool supports_position_reply = false;
    bool supports_trace_route_request = false;
    bool supports_trace_route_reply = false;
    bool supports_protocol_app_response = false;
    bool supports_protocol_ack_tracking = false;
    bool supports_meshcore_direct_route_table = false;
    bool supports_meshcore_identity_keys = false;
    bool supports_meshcore_peer_secret_derivation = false;
    bool supports_meshcore_rich_trace_projection = false;
    bool supports_reticulum_destination_ping = false;
    bool supports_reticulum_audio_call = false;
};

struct MeshTransportStatus
{
    bool ready = false;
    bool radio_ready = false;
    bool network_ready = false;
};

struct ReticulumLocalIdentityInfo
{
    bool ready = false;
    bool anonymous_peer = false;
    NodeId node_id = 0;
    char display_name[32] = {};
    uint8_t identity_hash[kReticulumPeerHashSize] = {};
    uint8_t lxmf_address[kReticulumPeerHashSize] = {};
    uint8_t propagation_address[kReticulumPeerHashSize] = {};
};

/**
 * @brief Mesh adapter interface
 * Abstracts mesh protocol implementation (Meshtastic, custom, etc.)
 */
class IMeshAdapter
{
  public:
    virtual DeviceIdentityRestoreResult restoreDeviceIdentity(const hostlink::management::IdentityRestoreView&, uint8_t*, size_t)
    {
        return DeviceIdentityRestoreResult::Unsupported;
    }
    virtual DeviceIdentityRestoreResult restoreRadioIdentity(const hostlink::management::RadioIdentityRestoreView&, uint8_t*, size_t)
    {
        return DeviceIdentityRestoreResult::Unsupported;
    }
    virtual DeviceAuthorResult readRadioIdentityInfo(uint8_t, uint32_t, uint8_t*, size_t, size_t& written) const
    {
        written = 0;
        return DeviceAuthorResult::Unsupported;
    }
    virtual DeviceAuthorResult exportRadioIdentityBackup(uint8_t, uint32_t, uint8_t*, size_t, size_t& written, const uint8_t*, const uint8_t*) const
    {
        written = 0;
        return DeviceAuthorResult::Unsupported;
    }
    virtual DeviceAuthorResult readDeviceAuthorKey(uint8_t*, uint32_t&) const { return DeviceAuthorResult::Unsupported; }
    // Internal physical-USB export port. Caller owns authorization and the
    // transient output; implementations must serialize identity access.
    virtual DeviceAuthorResult exportDeviceIdentityBackup(uint32_t, uint8_t*, size_t, size_t& written) const
    {
        written = 0;
        return DeviceAuthorResult::Unsupported;
    }
    // Internal primitive only: publication caller must validate file, identity,
    // immutable lease and durable revision reservation before invoking this.
    virtual DeviceAuthorResult signTrackManifest(const uint8_t*, size_t, uint32_t, track_sharing::ManifestView&,
                                                 uint8_t*, size_t, uint8_t*, size_t) const { return DeviceAuthorResult::Unsupported; }
    virtual DeviceAuthorResult proveDeviceAuthor(const uint8_t*, const uint8_t*, uint32_t,
                                                 uint8_t*, size_t, uint8_t*, uint32_t&, uint8_t*) const
    {
        return DeviceAuthorResult::Unsupported;
    }
    virtual IGeocachingTransport* geocachingTransport() { return nullptr; }
    virtual ITrackTransport* trackTransport() { return nullptr; }
    virtual bool bindTrackDelivery(const lxmf::TrackDeliveryBinding*) { return false; }
    // Nonblocking ownership change, serialized with callback execution. Caller
    // lends replacement until successful removal or an observed Conflict.
    virtual TrackBindingChange replaceTrackDelivery(const lxmf::TrackDeliveryBinding*, const lxmf::TrackDeliveryBinding*) { return TrackBindingChange::Unsupported; }
    // Run a bounded job step under callback serialization, after refreshing its
    // author guard. Visitor must not reenter router APIs or send radio data.
    virtual TrackBindingChange visitTrackDelivery(const lxmf::TrackDeliveryBinding*, uint8_t*, uint32_t&, TrackBindingVisit, void*) { return TrackBindingChange::Unsupported; }
    virtual bool trackLocalDestination(uint8_t*) { return false; }
    virtual bool requestTrackDirectoryPath(const uint8_t*) { return false; }
    virtual MeshSendResult sendTrackData(const uint8_t*, lxmf::ByteSpan, const uint8_t*, uint32_t,
                                         bool, std::array<uint8_t, 32>*) { return MeshSendResult::fail(MeshOperationFailure::Unsupported); }
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
     * @brief Send text and preserve the protocol-specific rejection reason.
     */
    virtual MeshSendResult sendTextDetailed(ChannelId channel, const std::string& text,
                                            MessageId forced_msg_id = 0,
                                            NodeId peer = 0)
    {
        MessageId msg_id = 0;
        const bool ok = sendTextWithId(channel, text, forced_msg_id, &msg_id, peer);
        return ok ? MeshSendResult::success(msg_id)
                  : MeshSendResult::fail(MeshOperationFailure::Unknown, msg_id);
    }

    /**
     * @brief Send text to a Reticulum destination-keyed conversation.
     *
     * This is intentionally optional so peer-addressed protocols do not inherit
     * Reticulum group semantics. Adapters that support group/plain destination
     * delivery should return the same destination identity in MeshSendResult.
     */
    virtual MeshSendResult sendTextToReticulumDestination(
        ChannelId channel,
        const std::string& text,
        MessageId forced_msg_id,
        const ReticulumPeerIdentity& destination)
    {
        (void)channel;
        (void)text;
        (void)forced_msg_id;
        return hasReticulumDestinationIdentity(destination)
                   ? MeshSendResult::fail(MeshOperationFailure::Unsupported)
                   : MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    }

    /**
     * @brief Poll for incoming text messages
     * @param out Output message (if available)
     * @return true if message available
     */
    virtual bool pollIncomingText(MeshIncomingText* out) = 0;

    /**
     * Expose the optional two-phase incoming-delivery capability.
     */
    virtual IIncomingDeliveryCommitPort* incomingDeliveryCommitPort()
    {
        return nullptr;
    }

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
     * @brief Broadcast this device's self identity on the active mesh protocol
     * @return true if the broadcast was queued or sent successfully
     */
    virtual bool broadcastSelfIdentity()
    {
        return requestNodeInfo(0xFFFFFFFFUL, false);
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
     * @brief Read local Reticulum/LXMF identity facts for UI display.
     */
    virtual bool getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
    {
        if (out)
        {
            *out = ReticulumLocalIdentityInfo{};
        }
        return false;
    }

    // Optional Reticulum control intent. Native adapters may acknowledge queue
    // admission; true does not establish a path or message delivery. The owner
    // reports physical failures separately and never changes chat readiness.
    virtual bool requestDirectoryPath(const uint8_t destination[16])
    {
        (void)destination;
        return false;
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
     * @brief Trigger discovery and preserve the rejection reason.
     */
    virtual MeshActionResult triggerDiscoveryActionDetailed(MeshDiscoveryAction action)
    {
        return triggerDiscoveryAction(action)
                   ? MeshActionResult::success()
                   : MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }

    /**
     * @brief Start a Sideband-compatible LXST telephony Link call.
     */
    virtual MeshActionResult startReticulumAudioCall(
        const ReticulumPeerIdentity& destination)
    {
        (void)destination;
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }

    /**
     * @brief Send a Reticulum Ping Destination proof probe.
     *
     * This is a protocol action: it sends an empty encrypted packet to an
     * LXMF delivery destination so the remote can answer with a Reticulum
     * proof. It is not a chat message and does not mutate contact state.
     */
    virtual MeshActionResult pingReticulumDestination(
        const ReticulumPeerIdentity& destination)
    {
        return hasReticulumDestinationIdentity(destination)
                   ? MeshActionResult::fail(MeshOperationFailure::Unsupported)
                   : MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    /**
     * @brief Persist a known Reticulum peer into the SD-backed LXMF address book.
     *
     * This is a user-action path, not a runtime RX path. Reticulum adapters may
     * perform synchronous durable writes here so adding a contact survives an
     * immediate reboot. Other protocols keep the default Unsupported result.
     */
    virtual MeshActionResult persistReticulumPeer(
        const ReticulumPeerIdentity& destination,
        bool favorite)
    {
        (void)favorite;
        return hasReticulumDestinationIdentity(destination)
                   ? MeshActionResult::fail(MeshOperationFailure::Unsupported)
                   : MeshActionResult::fail(MeshOperationFailure::InvalidInput);
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
     * @brief Notify the adapter that the platform Wi-Fi transport became available or unavailable.
     *
     * Implementations with Wi-Fi-owned sockets must close them synchronously when disabled.
     * The default keeps adapters without Wi-Fi dependencies source-compatible.
     */
    virtual bool setWifiTransportEnabled(bool enabled)
    {
        (void)enabled;
        return true;
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
    // Optional live bearer facts; legacy adapters retain their existing UI
    // projection until they provide this port. No device names enter the UI.
    virtual bool getTransportStatus(MeshTransportStatus*) const { return false; }
};

} // namespace chat
