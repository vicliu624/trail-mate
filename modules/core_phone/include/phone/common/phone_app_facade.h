#pragma once

#include "chat/domain/chat_types.h"
#include "chat/domain/contact_types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace phone
{

struct MeshtasticPhoneConfigSnapshot
{
    chat::MeshConfig mesh{};
    bool relay_enabled = false;
    char node_name[32] = {};
    char short_name[16] = {};
    bool primary_enabled = false;
    bool secondary_enabled = false;
    bool primary_uplink_enabled = false;
    bool primary_downlink_enabled = false;
    bool secondary_uplink_enabled = false;
    bool secondary_downlink_enabled = false;
    bool gps_enabled = true;
    uint8_t gps_mode = 0;
    uint32_t gps_interval_ms = 0;
    uint8_t gps_strategy = 0;
};

struct MeshCorePhoneConfigSnapshot
{
    chat::MeshProtocol active_protocol = chat::MeshProtocol::Meshtastic;
    chat::MeshConfig mesh{};
    char node_name[32] = {};
    char short_name[16] = {};
    int8_t tx_power_min_dbm = 0;
    int8_t tx_power_max_dbm = 0;
};

struct PhoneNodeView
{
    uint32_t node_id = 0;
    char short_name[10] = {};
    char long_name[32] = {};
    uint32_t last_seen = 0;
    float snr = 0.0f;
    float rssi = 0.0f;
    uint8_t hops_away = 0xFF;
    uint8_t channel = 0xFF;
    uint8_t next_hop = 0;
    uint8_t protocol = 0;
    uint8_t role = 0xFF;
    uint8_t hw_model = 0;
    bool has_macaddr = false;
    uint8_t macaddr[6] = {};
    bool via_mqtt = false;
    bool is_ignored = false;
    bool has_public_key = false;
    bool key_manually_verified = false;
    bool has_device_metrics = false;
    chat::contacts::NodeDeviceMetrics device_metrics{};
    chat::contacts::NodePosition position{};
};

class IPhoneAppFacade
{
  public:
    virtual ~IPhoneAppFacade() = default;

    virtual MeshtasticPhoneConfigSnapshot getMeshtasticPhoneConfig() const = 0;
    virtual void setMeshtasticPhoneConfig(const MeshtasticPhoneConfigSnapshot& config) = 0;
    virtual MeshCorePhoneConfigSnapshot getMeshCorePhoneConfig() const = 0;
    virtual void setMeshCorePhoneConfig(const MeshCorePhoneConfigSnapshot& config) = 0;
    virtual void saveConfig() = 0;
    virtual void applyMeshConfig() = 0;
    virtual void applyUserInfo() = 0;
    virtual void applyPositionConfig() = 0;
    virtual void getEffectiveUserInfo(char* out_long,
                                      std::size_t long_len,
                                      char* out_short,
                                      std::size_t short_len) const = 0;

    virtual chat::NodeId getSelfNodeId() const = 0;
    virtual bool sendPhoneText(chat::ChannelId channel,
                               const std::string& text,
                               chat::MessageId forced_msg_id,
                               chat::NodeId peer,
                               chat::MessageId& out_msg_id) = 0;
    virtual bool sendPhoneAppData(chat::ChannelId channel,
                                  uint32_t portnum,
                                  const uint8_t* payload,
                                  std::size_t len,
                                  chat::NodeId dest,
                                  bool want_ack,
                                  chat::MessageId packet_id,
                                  bool want_response) = 0;
    virtual bool pollIncomingPhoneData(chat::MeshIncomingData& out) = 0;
    virtual std::size_t phoneNodeCount() const = 0;
    virtual bool getPhoneNodeByIndex(std::size_t index, PhoneNodeView& out) const = 0;
    virtual bool findPhoneNode(chat::NodeId node_id, PhoneNodeView& out) const = 0;
    virtual bool isMeshPkiReady() const = 0;

    virtual bool isBleEnabled() const = 0;
    virtual void setBleEnabled(bool enabled) = 0;
    virtual bool getDeviceMacAddress(uint8_t out_mac[6]) const = 0;
    virtual bool syncCurrentEpochSeconds(uint32_t epoch_seconds) = 0;
    virtual void resetMeshConfig() = 0;
    virtual void clearNodeDb() = 0;
    virtual void clearMessageDb() = 0;
    virtual void restartDevice() = 0;

    virtual bool meshCoreExportIdentityPublicKey(uint8_t* out_pubkey, std::size_t len) = 0;
    virtual bool meshCoreExportIdentityPrivateKey(uint8_t* out_priv, std::size_t len) = 0;
    virtual bool meshCoreImportIdentityPrivateKey(const uint8_t* in_priv, std::size_t len) = 0;
    virtual bool meshCoreSignPayload(const uint8_t* data, std::size_t len, uint8_t* out_sig, std::size_t out_len) = 0;
    virtual bool meshCoreSendSelfAdvert(bool broadcast) = 0;
    virtual bool meshCoreSendPeerRequestType(const uint8_t* pubkey,
                                             std::size_t len,
                                             uint8_t req_type,
                                             uint32_t* out_tag,
                                             uint32_t* out_est_timeout,
                                             bool* out_sent_flood) = 0;
    virtual bool meshCoreSendPeerRequestPayload(const uint8_t* pubkey,
                                                std::size_t len,
                                                const uint8_t* payload,
                                                std::size_t payload_len,
                                                bool force_flood,
                                                uint32_t* out_tag,
                                                uint32_t* out_est_timeout,
                                                bool* out_sent_flood) = 0;
    virtual bool meshCoreSendAnonRequestPayload(const uint8_t* pubkey,
                                                std::size_t len,
                                                const uint8_t* payload,
                                                std::size_t payload_len,
                                                uint32_t* out_est_timeout,
                                                bool* out_sent_flood) = 0;
    virtual bool meshCoreSendTracePath(const uint8_t* path,
                                       std::size_t path_len,
                                       uint32_t tag,
                                       uint32_t auth,
                                       uint8_t flags,
                                       uint32_t* out_est_timeout) = 0;
    virtual bool meshCoreSendControlData(const uint8_t* payload, std::size_t payload_len) = 0;
    virtual bool meshCoreSendRawData(const uint8_t* path,
                                     std::size_t path_len,
                                     const uint8_t* payload,
                                     std::size_t payload_len,
                                     uint32_t* out_est_timeout) = 0;
    virtual void meshCoreSetFloodScopeKey(const uint8_t* key, std::size_t len) = 0;
};

} // namespace phone
