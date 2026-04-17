/**
 * @file lxmf_adapter.h
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"

#include <queue>
#include <vector>

namespace chat::lxmf
{

class LxmfAdapter : public IMeshAdapter
{
  public:
    explicit LxmfAdapter(LoraBoard& board);

    MeshCapabilities getCapabilities() const override;
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    NodeId getNodeId() const override;
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;

  private:
    struct PeerInfo
    {
        uint32_t node_id = 0;
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t enc_pub[LxmfIdentity::kEncPubKeySize] = {};
        uint8_t sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        char display_name[32] = {};
        uint32_t last_seen_s = 0;
        uint32_t last_path_request_ms = 0;
    };

    struct PathEntry
    {
        uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t next_hop_transport[reticulum::kTruncatedHashSize] = {};
        uint8_t cached_packet_hash[reticulum::kFullHashSize] = {};
        uint8_t cached_announce[reticulum::kReticulumMtu] = {};
        size_t cached_announce_len = 0;
        uint8_t hops = 0;
        uint32_t last_seen_s = 0;
        bool direct = false;
    };

    struct PacketFilterEntry
    {
        uint8_t packet_hash[reticulum::kFullHashSize] = {};
        uint32_t seen_ms = 0;
    };

    struct ReverseEntry
    {
        uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
        uint32_t created_ms = 0;
    };

    struct LinkRelayEntry
    {
        uint8_t link_id[reticulum::kTruncatedHashSize] = {};
        uint8_t initiator_hops = 0;
        uint8_t responder_hops = 0;
        uint32_t last_seen_ms = 0;
    };

    static constexpr uint32_t kAnnounceIntervalMs = 120000;
    static constexpr uint32_t kInitialAnnounceDelayMs = 1500;

    rnode::RNodeAdapter raw_;
    LxmfIdentity identity_;
    MeshConfig config_{};
    std::queue<MeshIncomingText> text_receive_queue_;
    std::vector<PeerInfo> peers_;
    std::vector<PathEntry> paths_;
    std::vector<PacketFilterEntry> packet_filter_;
    std::vector<ReverseEntry> reverse_table_;
    std::vector<LinkRelayEntry> link_relays_;
    std::string user_long_name_;
    std::string user_short_name_;
    uint32_t last_announce_ms_ = 0;
    bool announce_pending_ = true;
    bool peers_loaded_ = false;

    void processRadioPackets();
    void maybeAnnounce();
    bool sendAnnounce(reticulum::PacketContext context = reticulum::PacketContext::None);
    bool handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                              const reticulum::ParsedPacket& packet);
    bool handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                          const reticulum::ParsedPacket& packet);
    bool handleProofPacket(const uint8_t* raw_packet, size_t raw_len,
                           const reticulum::ParsedPacket& packet);
    bool handleLinkRequestPacket(const uint8_t* raw_packet, size_t raw_len,
                                 const reticulum::ParsedPacket& packet);
    bool handlePathRequestPacket(const reticulum::ParsedPacket& packet);
    bool handleCacheRequestPacket(const reticulum::ParsedPacket& packet);
    bool maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                     const reticulum::ParsedPacket& packet);
    bool maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                const reticulum::ParsedPacket& packet);
    bool sendProofForPacket(const uint8_t* raw_packet, size_t raw_len);
    bool sendPathRequest(PeerInfo& peer);
    bool shouldRequestPath(const PeerInfo& peer) const;
    bool buildEncryptedPacketForPeer(const PeerInfo& peer,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_packet, size_t* inout_len);
    bool routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                            bool allow_transport);
    bool sendCachedAnnounceResponse(const PathEntry& path,
                                    reticulum::PacketContext context);
    bool sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize]);
    bool shouldRebroadcastAnnounce(const reticulum::ParsedPacket& packet) const;
    bool rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet);
    bool isDuplicatePacket(const uint8_t packet_hash[reticulum::kFullHashSize]);
    void rememberPacket(const uint8_t packet_hash[reticulum::kFullHashSize]);
    void rememberReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    ReverseEntry* findReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
    void cullTransportState();
    PathEntry& upsertPath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    const PathEntry* findPath(const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const;
    LinkRelayEntry& upsertLinkRelay(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    LinkRelayEntry* findLinkRelay(const uint8_t link_id[reticulum::kTruncatedHashSize]);
    PeerInfo* findPeerByNodeId(NodeId node_id);
    const PeerInfo* findPeerByDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize]) const;
    PeerInfo& upsertPeer(const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
    void publishPeerUpdate(const PeerInfo& peer) const;
    void loadPersistedPeers();
    bool persistPeers() const;
    uint32_t currentTimestampSeconds() const;
    const char* effectiveDisplayName() const;
    static uint32_t messageIdFromHash(const uint8_t hash[reticulum::kFullHashSize]);
    static void pathRequestDestinationHash(uint8_t out_hash[reticulum::kTruncatedHashSize]);
};

} // namespace chat::lxmf
