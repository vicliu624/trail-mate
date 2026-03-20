#pragma once

#include "board/LoraBoard.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_dedup.h"
#include "chat/ports/i_mesh_adapter.h"

#include <map>
#include <queue>
#include <string>

namespace apps::esp_idf
{

class MeshtasticRadioAdapter final : public chat::IMeshAdapter
{
  public:
    explicit MeshtasticRadioAdapter(LoraBoard& board);

    chat::MeshCapabilities getCapabilities() const override;
    bool sendText(chat::ChannelId channel, const std::string& text,
                  chat::MessageId* out_msg_id, chat::NodeId peer = 0) override;
    bool pollIncomingText(chat::MeshIncomingText* out) override;
    bool sendAppData(chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     chat::NodeId dest = 0, bool want_ack = false,
                     chat::MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(chat::MeshIncomingData* out) override;
    bool requestNodeInfo(chat::NodeId dest, bool want_response) override;
    bool triggerDiscoveryAction(chat::MeshDiscoveryAction action) override;
    void applyConfig(const chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;
    chat::NodeId getNodeId() const override;

    bool broadcastNodeInfo();

  private:
    static constexpr uint32_t kBroadcastNodeId = 0xFFFFFFFFu;

    bool sendEncodedPayload(chat::ChannelId channel,
                            const uint8_t* payload,
                            size_t len,
                            chat::NodeId dest,
                            bool want_ack,
                            chat::MessageId packet_id,
                            bool publish_send_result);
    bool sendNodeInfoTo(chat::NodeId dest, bool want_response, chat::ChannelId channel);
    bool sendRoutingAck(chat::NodeId dest, chat::MessageId request_id, chat::ChannelId channel);
    void processReceivedPacket(const uint8_t* data, size_t size);
    void pollRadio();
    void configureRadio();
    void updateChannelKeys();
    void initNodeIdentity();
    void ensureReceiveStarted();
    bool decodeUserPayload(const uint8_t* payload, size_t len,
                           const chat::RxMeta& rx_meta,
                           chat::NodeId from_node,
                           uint8_t channel_index);
    void publishPositionEvent(chat::NodeId node_id, const meshtastic_Position& pos);
    uint8_t channelHashFor(chat::ChannelId channel) const;
    const uint8_t* channelKeyFor(chat::ChannelId channel, size_t* out_len) const;

    LoraBoard& board_;
    chat::MeshConfig config_{};
    chat::meshtastic::MtDedup dedup_{};
    std::queue<chat::MeshIncomingText> text_queue_{};
    std::queue<chat::MeshIncomingData> data_queue_{};
    std::map<chat::NodeId, chat::ChannelId> node_last_channel_{};
    std::string user_long_name_{};
    std::string user_short_name_{};
    chat::MessageId next_packet_id_ = 1;
    chat::NodeId node_id_ = 0;
    uint8_t mac_addr_[6] = {};
    bool ready_ = false;
    bool rx_started_ = false;
    bool nodeinfo_broadcast_sent_ = false;
    float last_rx_rssi_ = 0.0f;
    float last_rx_snr_ = 0.0f;
    uint32_t radio_freq_hz_ = 0;
    uint32_t radio_bw_hz_ = 0;
    uint8_t radio_sf_ = 0;
    uint8_t radio_cr_ = 0;
    uint8_t primary_channel_hash_ = 0x00;
    uint8_t secondary_channel_hash_ = 0x00;
    uint8_t primary_psk_[16] = {};
    size_t primary_psk_len_ = 0;
    uint8_t secondary_psk_[16] = {};
    size_t secondary_psk_len_ = 0;
};

} // namespace apps::esp_idf
