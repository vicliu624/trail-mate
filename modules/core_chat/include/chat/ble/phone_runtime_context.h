#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat
{
class ChatService;
class IMeshAdapter;
namespace contacts
{
class ContactService;
class INodeStore;
} // namespace contacts
} // namespace chat

namespace ble
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

class IPhoneRuntimeContext
{
  public:
    virtual ~IPhoneRuntimeContext() = default;

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

    virtual chat::ChatService& getChatService() = 0;
    virtual chat::contacts::ContactService& getContactService() = 0;
    virtual chat::IMeshAdapter* getMeshAdapter() = 0;
    virtual const chat::IMeshAdapter* getMeshAdapter() const = 0;
    virtual chat::contacts::INodeStore* getNodeStore() = 0;
    virtual const chat::contacts::INodeStore* getNodeStore() const = 0;
    virtual chat::NodeId getSelfNodeId() const = 0;

    virtual bool isBleEnabled() const = 0;
    virtual void setBleEnabled(bool enabled) = 0;
    virtual bool getDeviceMacAddress(uint8_t out_mac[6]) const = 0;
    virtual bool syncCurrentEpochSeconds(uint32_t epoch_seconds) = 0;
    virtual void resetMeshConfig() = 0;
    virtual void clearNodeDb() = 0;
    virtual void clearMessageDb() = 0;
    virtual void restartDevice() = 0;
};

} // namespace ble
