#pragma once

#include "phone/common/phone_runtime_context.h"

namespace phone::tests
{

class FakePhoneRuntimeContext final : public IPhoneRuntimeContext
{
  public:
    MeshtasticPhoneConfigSnapshot getMeshtasticPhoneConfig() const override { return meshtastic_config_; }
    void setMeshtasticPhoneConfig(const MeshtasticPhoneConfigSnapshot& config) override
    {
        meshtastic_config_ = config;
    }
    MeshCorePhoneConfigSnapshot getMeshCorePhoneConfig() const override { return meshcore_config_; }
    void setMeshCorePhoneConfig(const MeshCorePhoneConfigSnapshot& config) override
    {
        meshcore_config_ = config;
    }
    void saveConfig() override { save_config_count++; }
    void applyMeshConfig() override { apply_mesh_config_count++; }
    void applyUserInfo() override { apply_user_info_count++; }
    void applyPositionConfig() override { apply_position_config_count++; }
    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override
    {
        copyBounded(out_long, long_len, "Trail Mate");
        copyBounded(out_short, short_len, "TM");
    }

    chat::ChatService& getChatService() override { return *chat_service_; }
    chat::contacts::ContactService& getContactService() override { return *contact_service_; }
    chat::IMeshAdapter* getMeshAdapter() override { return mesh_adapter_; }
    const chat::IMeshAdapter* getMeshAdapter() const override { return mesh_adapter_; }
    chat::contacts::INodeStore* getNodeStore() override { return node_store_; }
    const chat::contacts::INodeStore* getNodeStore() const override { return node_store_; }
    chat::NodeId getSelfNodeId() const override { return self_node_id; }

    bool isBleEnabled() const override { return ble_enabled; }
    void setBleEnabled(bool enabled) override { ble_enabled = enabled; }
    bool getDeviceMacAddress(uint8_t out_mac[6]) const override
    {
        if (!out_mac)
        {
            return false;
        }
        for (size_t i = 0; i < 6; ++i)
        {
            out_mac[i] = mac[i];
        }
        return true;
    }
    bool syncCurrentEpochSeconds(uint32_t epoch_seconds) override
    {
        last_synced_epoch = epoch_seconds;
        sync_time_count++;
        return true;
    }
    void resetMeshConfig() override { reset_mesh_config_count++; }
    void clearNodeDb() override { clear_node_db_count++; }
    void clearMessageDb() override { clear_message_db_count++; }
    void restartDevice() override { restart_device_count++; }

    chat::NodeId self_node_id = 0x12345678;
    bool ble_enabled = true;
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    uint32_t last_synced_epoch = 0;
    int save_config_count = 0;
    int apply_mesh_config_count = 0;
    int apply_user_info_count = 0;
    int apply_position_config_count = 0;
    int sync_time_count = 0;
    int reset_mesh_config_count = 0;
    int clear_node_db_count = 0;
    int clear_message_db_count = 0;
    int restart_device_count = 0;

  private:
    static void copyBounded(char* dst, std::size_t dst_len, const char* src)
    {
        if (!dst || dst_len == 0)
        {
            return;
        }
        std::size_t i = 0;
        for (; src && src[i] != '\0' && i + 1 < dst_len; ++i)
        {
            dst[i] = src[i];
        }
        dst[i] = '\0';
    }

    MeshtasticPhoneConfigSnapshot meshtastic_config_{};
    MeshCorePhoneConfigSnapshot meshcore_config_{};
    chat::ChatService* chat_service_ = nullptr;
    chat::contacts::ContactService* contact_service_ = nullptr;
    chat::IMeshAdapter* mesh_adapter_ = nullptr;
    chat::contacts::INodeStore* node_store_ = nullptr;
};

} // namespace phone::tests
