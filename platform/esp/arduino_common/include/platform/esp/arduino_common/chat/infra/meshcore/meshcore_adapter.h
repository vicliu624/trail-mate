/**
 * @file meshcore_adapter.h
 * @brief MeshCore protocol adapter
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/meshcore/meshcore_ble_backend.h"
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_identity.h"
#include <deque>
#include <limits>
#include <queue>
#include <string>
#include <vector>

namespace chat
{
namespace meshcore
{

/**
 * @brief MeshCore protocol adapter
 */
class MeshCoreAdapter : public IMeshAdapter, public IMeshCoreBleBackend
{
  public:
    /**
     * @brief Constructor
     */
    MeshCoreAdapter(LoraBoard& board);

    /**
     * @brief Destructor
     */
    ~MeshCoreAdapter() override = default;
    MeshCapabilities getCapabilities() const override;

    // IMeshAdapter interface implementation
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool sendDirectTextDetailed(ChannelId channel, const std::string& text,
                                NodeId peer, uint8_t txt_type,
                                uint32_t* out_ack, uint32_t* out_timeout,
                                bool* out_sent_flood);

    bool pollIncomingText(MeshIncomingText* out) override;

    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;

    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool triggerDiscoveryAction(MeshDiscoveryAction action) override;
    bool startKeyVerification(NodeId dest) override;
    bool submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number) override;
    bool isPkiReady() const override;
    bool hasPkiKey(NodeId dest) const override;

    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    void setLastRxStats(float rssi, float snr) override;

    bool isReady() const override;
    NodeId getNodeId() const override { return node_id_; }
    IMeshCoreBleBackend* asMeshCoreBleBackend() override { return this; }
    const IMeshCoreBleBackend* asMeshCoreBleBackend() const override { return this; }

    /**
     * @brief Poll for incoming raw packet data
     * @param out_data Output buffer for raw packet data
     * @param out_len Output packet length
     * @param max_len Maximum buffer size
     * @return true if raw packet data is available
     */
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;

    /**
     * @brief Handle raw packet data (from radio task)
     * @param data Raw packet data
     * @param size Packet size
     */
    void handleRawPacket(const uint8_t* data, size_t size) override;

    /**
     * @brief Process send queue (no-op placeholder)
     */
    void processSendQueue() override;

    static constexpr size_t kMaxPeerPathLen = 64;

    struct PeerInfo
    {
        uint8_t peer_hash = 0;
        NodeId node_id = 0;
        bool has_pubkey = false;
        bool pubkey_verified = false;
        uint8_t pubkey[MeshCoreIdentity::kPubKeySize] = {};
        uint8_t out_path_len = 0;
        uint8_t out_path[kMaxPeerPathLen] = {};
        uint32_t last_seen_ms = 0;
        ChannelId preferred_channel = ChannelId::PRIMARY;
    };

    bool getPeerInfos(std::vector<PeerInfo>& out) const;
    bool lookupPeerByHash(uint8_t hash, PeerInfo* out) const;
    bool lookupPeerByNodeId(NodeId node_id, PeerInfo* out) const;

    bool exportIdentityPublicKey(uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) override;
    bool exportIdentityPublicKey(uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) const;
    bool exportIdentityPrivateKey(uint8_t out_priv[MeshCoreIdentity::kPrivKeySize]) override;
    bool exportIdentityPrivateKey(uint8_t out_priv[MeshCoreIdentity::kPrivKeySize]) const;
    bool importIdentityPrivateKey(const uint8_t* in_priv, size_t len);
    bool signPayload(const uint8_t* data, size_t len,
                     uint8_t out_sig[MeshCoreIdentity::kSignatureSize]) override;
    bool signPayload(const uint8_t* data, size_t len,
                     uint8_t out_sig[MeshCoreIdentity::kSignatureSize]) const;
    uint8_t getSelfHash() const { return self_hash_; }

    bool clearPeerPath(uint8_t peer_hash);

    struct Event
    {
        enum class Type : uint8_t
        {
            Response,
            PathResponse,
            ControlData,
            RawData,
            TraceData,
            Advert,
            PathUpdated,
            SendConfirmed
        };

        Type type = Type::Response;
        uint8_t peer_hash = 0;
        NodeId peer_node = 0;
        int8_t snr_qdb = 0;
        int8_t rssi_dbm = 0;
        uint8_t flags = 0;
        uint32_t tag = 0;
        uint32_t auth = 0;
        uint32_t trip_ms = 0;
        std::vector<uint8_t> payload;
        std::vector<uint8_t> in_path;
        std::vector<uint8_t> out_path;
        std::vector<uint8_t> trace_hashes;
        std::vector<uint8_t> trace_snrs;
        bool advert_is_new = false;
    };

    struct RadioStats
    {
        int16_t noise_floor_dbm = 0;
        uint32_t tx_airtime_ms = 0;
        uint32_t rx_airtime_ms = 0;
    };

    bool pollEvent(Event* out);
    RadioStats getRadioStats() const;

    bool ensurePeerPublicKey(const uint8_t* pubkey, size_t len, bool verified);

    bool sendPeerRequestType(const uint8_t* pubkey, size_t len, uint8_t req_type,
                             uint32_t* out_tag, uint32_t* out_est_timeout,
                             bool* out_sent_flood);
    bool sendSelfAdvert(bool broadcast) override;
    bool sendPeerRequestPayload(const uint8_t* pubkey, size_t len,
                                const uint8_t* payload, size_t payload_len,
                                bool force_flood,
                                uint32_t* out_tag, uint32_t* out_est_timeout,
                                bool* out_sent_flood);
    bool sendAnonRequestPayload(const uint8_t* pubkey, size_t len,
                                const uint8_t* payload, size_t payload_len,
                                uint32_t* out_est_timeout,
                                bool* out_sent_flood);
    bool sendRawData(const uint8_t* path, size_t path_len,
                     const uint8_t* payload, size_t payload_len,
                     uint32_t* out_est_timeout);
    bool sendTracePath(const uint8_t* path, size_t path_len,
                       uint32_t tag, uint32_t auth, uint8_t flags,
                       uint32_t* out_est_timeout);
    bool sendControlData(const uint8_t* payload, size_t payload_len);
    void setFloodScopeKey(const uint8_t* key, size_t len);
    bool sendStoredAdvert(const uint8_t* pubkey, size_t len);
    bool sendIdentityAdvertWithLocation(bool broadcast, bool include_location,
                                        int32_t lat_i6, int32_t lon_i6);
    bool exportAdvertFrame(const uint8_t* pubkey, size_t len,
                           std::vector<uint8_t>& out_frame,
                           bool include_location,
                           int32_t lat_i6, int32_t lon_i6) const;
    bool importAdvertFrame(const uint8_t* frame, size_t len);

  private:
    static constexpr size_t kMaxPeerRouteCandidates = 4;
    static constexpr size_t kMaxPersistedPeerPubKeys = 64;
    static constexpr const char* kPeerPubKeyPrefsNs = "mc_peers";
    static constexpr const char* kPeerPubKeyPrefsKey = "peer_keys";
    static constexpr const char* kPeerPubKeyPrefsKeyVer = "peer_ver";
    static constexpr uint8_t kPeerPubKeyPrefsVersion = 1;
    static constexpr uint32_t kAutoDiscoverCooldownMs = 8000;
    static constexpr size_t kMaxEventQueue = 32;

    struct ScheduledFrame
    {
        std::vector<uint8_t> bytes;
        uint32_t due_ms = 0;
    };

    struct SeenEntry
    {
        uint32_t signature = 0;
        uint32_t seen_ms = 0;
    };

    struct PeerRouteEntry
    {
        struct PathCandidate
        {
            uint8_t path[kMaxPeerPathLen] = {};
            uint8_t path_len = 0;
            ChannelId channel = ChannelId::PRIMARY;
            int16_t snr_x10 = std::numeric_limits<int16_t>::min();
            uint8_t sample_count = 0;
            uint32_t first_seen_ms = 0;
            uint32_t last_seen_ms = 0;
            int16_t quality = std::numeric_limits<int16_t>::min();
        };

        uint8_t peer_hash = 0;
        NodeId node_id_guess = 0;
        uint8_t out_path[kMaxPeerPathLen] = {};
        uint8_t out_path_len = 0;
        bool has_out_path = false;
        bool has_pubkey = false;
        bool pubkey_verified = false;
        uint8_t pubkey[MeshCoreIdentity::kPubKeySize] = {};
        uint32_t pubkey_seen_ms = 0;
        uint8_t last_advert[184] = {};
        uint16_t last_advert_len = 0;
        uint32_t last_advert_ts = 0;
        ChannelId preferred_channel = ChannelId::PRIMARY;
        uint32_t last_seen_ms = 0;
        uint32_t route_blackout_until_ms = 0;
        uint8_t best_candidate = 0;
        uint8_t candidate_count = 0;
        PathCandidate candidates[kMaxPeerRouteCandidates];
    };

    struct PendingAppAck
    {
        uint32_t signature = 0;
        NodeId dest = 0;
        uint32_t portnum = 0;
        uint32_t created_ms = 0;
        uint32_t expire_ms = 0;
    };

    struct KeyVerifySession
    {
        bool active = false;
        bool is_initiator = false;
        bool awaiting_user_number = false;
        NodeId peer = 0;
        uint64_t nonce = 0;
        uint32_t expected_number = 0;
        uint32_t started_ms = 0;
    };

    enum class TxGateReason : uint8_t
    {
        Ok = 0,
        NotInitialized,
        TxDisabled,
        RadioOffline,
        DutyCycleLimited
    };

    LoraBoard& board_;

    // Configuration
    MeshConfig config_;
    std::string user_long_name_;
    std::string user_short_name_;
    uint32_t min_tx_interval_ms_;
    uint32_t last_tx_ms_;
    uint8_t encrypt_mode_;
    bool pki_enabled_;
    NodeId node_id_;
    uint8_t self_hash_;
    float last_rx_rssi_;
    float last_rx_snr_;
    int16_t last_noise_floor_dbm_ = 0;
    uint32_t tx_airtime_ms_ = 0;
    uint32_t rx_airtime_ms_ = 0;
    MeshCoreIdentity identity_;
    uint32_t last_auto_discover_ms_ = 0;
    uint8_t last_auto_discover_hash_ = 0;

    // Implementation state
    bool initialized_;

    // Receive queue for parsed messages
    std::queue<MeshIncomingText> receive_queue_;
    std::queue<MeshIncomingData> app_receive_queue_;

    // Raw packet storage for debugging/inspection
    uint8_t last_raw_packet_[256];
    size_t last_raw_packet_len_;
    bool has_pending_raw_packet_;
    std::deque<ScheduledFrame> scheduled_tx_;
    std::deque<SeenEntry> seen_recent_;
    std::deque<PendingAppAck> pending_app_acks_;
    std::deque<Event> events_;
    std::vector<PeerRouteEntry> peer_routes_;
    std::vector<NodeId> verified_peers_;
    KeyVerifySession key_verify_session_;

    MessageId next_msg_id_;
    std::array<uint8_t, 16> flood_scope_key_ = {};

    TxGateReason checkTxGate(uint32_t now_ms) const;
    static const char* txGateReasonName(TxGateReason reason);
    bool canTransmitNow(uint32_t now_ms) const;
    bool transmitFrameNow(const uint8_t* data, size_t len, uint32_t now_ms);
    bool enqueueScheduled(const uint8_t* data, size_t len, uint32_t delay_ms);
    bool resolveGroupSecret(ChannelId channel, uint8_t out_key16[16],
                            uint8_t out_key32[32], uint8_t* out_hash) const;
    ChannelId resolveChannelFromHash(uint8_t channel_hash, bool* out_match) const;
    bool deriveLegacyDirectSecret(ChannelId channel, uint8_t peer_hash,
                                  uint8_t out_key16[16], uint8_t out_key32[32]) const;
    bool deriveIdentitySecret(uint8_t peer_hash, uint8_t out_key16[16],
                              uint8_t out_key32[32]) const;
    bool deriveDirectSecret(ChannelId channel, uint8_t peer_hash,
                            uint8_t out_key16[16], uint8_t out_key32[32]) const;
    bool lookupPeerPubKey(uint8_t peer_hash, uint8_t out_pubkey[MeshCoreIdentity::kPubKeySize]) const;
    void rememberPeerPubKey(const uint8_t pubkey[MeshCoreIdentity::kPubKeySize],
                            uint32_t now_ms, bool verified);
    void loadPeerPubKeysFromPrefs();
    void savePeerPubKeysToPrefs();
    void maybeAutoDiscoverMissingPeer(uint8_t peer_hash, uint32_t now_ms);
    bool tryDecryptPeerPayload(uint8_t src_hash, const uint8_t* cipher, size_t cipher_len,
                               uint8_t* out_plain, size_t* out_plain_len,
                               ChannelId* out_channel) const;
    PeerRouteEntry* findPeerRouteByHash(uint8_t peer_hash);
    const PeerRouteEntry* findPeerRouteByHash(uint8_t peer_hash) const;
    PeerRouteEntry* selectPeerRouteByHash(uint8_t peer_hash, uint32_t now_ms);
    const PeerRouteEntry* selectPeerRouteByHash(uint8_t peer_hash, uint32_t now_ms) const;
    PeerRouteEntry& upsertPeerRoute(uint8_t peer_hash, uint32_t now_ms);
    void prunePeerRoutes(uint32_t now_ms);
    void refreshBestPeerRoute(PeerRouteEntry& entry, uint32_t now_ms);
    void rememberPeerPathCandidate(PeerRouteEntry& entry,
                                   const uint8_t* path, size_t path_len,
                                   ChannelId channel, int16_t snr_x10,
                                   uint32_t now_ms);
    void penalizePeerRoute(uint8_t peer_hash, uint32_t now_ms);
    static int16_t computePathQuality(uint8_t path_len, int16_t snr_x10,
                                      uint8_t sample_count, uint32_t age_ms);
    void rememberPeerNodeId(uint8_t peer_hash, NodeId node_id, uint32_t now_ms);
    NodeId resolvePeerNodeId(uint8_t peer_hash) const;
    void rememberPeerPath(uint8_t peer_hash, const uint8_t* path, size_t path_len,
                          ChannelId channel, uint32_t now_ms);
    void pruneSeen(uint32_t now_ms);
    bool hasSeenSignature(uint32_t signature, uint32_t now_ms);
    void handleRawPacketInternal(const uint8_t* data, size_t size, bool allow_duplicate);
    void prunePendingAppAcks(uint32_t now_ms);
    void trackPendingAppAck(uint32_t signature, NodeId dest, uint32_t portnum, uint32_t now_ms);
    bool consumePendingAppAck(uint32_t signature, uint32_t now_ms);
    void pushEvent(Event&& ev);
    bool sendNodeInfoFrame(NodeId dest, bool is_query, bool request_reply);
    bool sendDiscoverRequestLocal();
    bool sendIdentityAdvert(bool broadcast);
    bool sendIdentityAdvert(bool broadcast, bool include_location,
                            int32_t lat_i6, int32_t lon_i6);
    bool handleControlAppData(const MeshIncomingData& incoming);
    bool handleNodeInfoControl(const MeshIncomingData& incoming);
    bool handleKeyVerifyControl(const MeshIncomingData& incoming);
    uint32_t computeVerificationNumber(NodeId peer, uint64_t nonce) const;
    bool isPeerVerified(NodeId peer) const;
    void markPeerVerified(NodeId peer);
};

} // namespace meshcore
} // namespace chat
