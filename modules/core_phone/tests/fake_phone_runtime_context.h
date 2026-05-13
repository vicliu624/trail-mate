#pragma once

#include "phone/common/phone_app_facade.h"

#include <algorithm>
#include <vector>

namespace phone::tests
{

class FakePhoneRuntimeContext final : public IPhoneAppFacade
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

    chat::NodeId getSelfNodeId() const override { return self_node_id; }
    bool sendPhoneText(chat::ChannelId,
                       const std::string&,
                       chat::MessageId forced_msg_id,
                       chat::NodeId,
                       chat::MessageId& out_msg_id) override
    {
        send_text_count++;
        out_msg_id = forced_msg_id != 0 ? forced_msg_id : next_message_id++;
        return true;
    }
    bool sendPhoneAppData(chat::ChannelId,
                          uint32_t,
                          const uint8_t* payload,
                          std::size_t len,
                          chat::NodeId,
                          bool,
                          chat::MessageId,
                          bool) override
    {
        send_app_data_count++;
        return payload != nullptr || len == 0;
    }
    bool pollIncomingPhoneData(chat::MeshIncomingData& out) override
    {
        if (incoming_data.empty())
        {
            return false;
        }
        out = incoming_data.front();
        incoming_data.erase(incoming_data.begin());
        return true;
    }
    std::size_t phoneNodeCount() const override { return nodes.size(); }
    bool getPhoneNodeByIndex(std::size_t index, PhoneNodeView& out) const override
    {
        if (index >= nodes.size())
        {
            return false;
        }
        out = nodes[index];
        return true;
    }
    bool findPhoneNode(chat::NodeId node_id, PhoneNodeView& out) const override
    {
        for (const auto& node : nodes)
        {
            if (node.node_id == node_id)
            {
                out = node;
                return true;
            }
        }
        return false;
    }
    bool isMeshPkiReady() const override { return mesh_pki_ready; }

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

    bool meshCoreExportIdentityPublicKey(uint8_t* out_pubkey, std::size_t len) override
    {
        return copyFixed(out_pubkey, len, meshcore_public_key, sizeof(meshcore_public_key));
    }
    bool meshCoreExportIdentityPrivateKey(uint8_t* out_priv, std::size_t len) override
    {
        return copyFixed(out_priv, len, meshcore_private_key, sizeof(meshcore_private_key));
    }
    bool meshCoreImportIdentityPrivateKey(const uint8_t* in_priv, std::size_t len) override
    {
        if (!in_priv || len != sizeof(meshcore_private_key))
        {
            return false;
        }
        std::copy(in_priv, in_priv + len, meshcore_private_key);
        return true;
    }
    bool meshCoreSignPayload(const uint8_t*, std::size_t, uint8_t* out_sig, std::size_t out_len) override
    {
        if (!out_sig || out_len < 64)
        {
            return false;
        }
        for (std::size_t i = 0; i < 64; ++i)
        {
            out_sig[i] = static_cast<uint8_t>(i);
        }
        return true;
    }
    bool meshCoreSendSelfAdvert(bool) override { return meshcore_backend_ready; }
    bool meshCoreSendPeerRequestType(const uint8_t*,
                                     std::size_t,
                                     uint8_t,
                                     uint32_t* out_tag,
                                     uint32_t* out_est_timeout,
                                     bool* out_sent_flood) override
    {
        return markMeshCoreSend(out_tag, out_est_timeout, out_sent_flood);
    }
    bool meshCoreSendPeerRequestPayload(const uint8_t*,
                                        std::size_t,
                                        const uint8_t*,
                                        std::size_t,
                                        bool,
                                        uint32_t* out_tag,
                                        uint32_t* out_est_timeout,
                                        bool* out_sent_flood) override
    {
        return markMeshCoreSend(out_tag, out_est_timeout, out_sent_flood);
    }
    bool meshCoreSendAnonRequestPayload(const uint8_t*,
                                        std::size_t,
                                        const uint8_t*,
                                        std::size_t,
                                        uint32_t* out_est_timeout,
                                        bool* out_sent_flood) override
    {
        if (!meshcore_backend_ready)
        {
            return false;
        }
        if (out_est_timeout)
        {
            *out_est_timeout = 0;
        }
        if (out_sent_flood)
        {
            *out_sent_flood = true;
        }
        meshcore_send_count++;
        return true;
    }
    bool meshCoreSendTracePath(const uint8_t*, std::size_t, uint32_t, uint32_t, uint8_t, uint32_t* out_est_timeout) override
    {
        if (out_est_timeout)
        {
            *out_est_timeout = 0;
        }
        meshcore_send_count++;
        return meshcore_backend_ready;
    }
    bool meshCoreSendControlData(const uint8_t*, std::size_t) override
    {
        meshcore_send_count++;
        return meshcore_backend_ready;
    }
    bool meshCoreSendRawData(const uint8_t*, std::size_t, const uint8_t*, std::size_t, uint32_t* out_est_timeout) override
    {
        if (out_est_timeout)
        {
            *out_est_timeout = 0;
        }
        meshcore_send_count++;
        return meshcore_backend_ready;
    }
    void meshCoreSetFloodScopeKey(const uint8_t*, std::size_t) override { meshcore_set_flood_scope_count++; }

    chat::NodeId self_node_id = 0x12345678;
    bool ble_enabled = true;
    bool mesh_pki_ready = false;
    bool meshcore_backend_ready = false;
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    uint8_t meshcore_public_key[32] = {};
    uint8_t meshcore_private_key[32] = {};
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
    int send_text_count = 0;
    int send_app_data_count = 0;
    int meshcore_send_count = 0;
    int meshcore_set_flood_scope_count = 0;
    chat::MessageId next_message_id = 1;
    std::vector<PhoneNodeView> nodes;
    std::vector<chat::MeshIncomingData> incoming_data;

  private:
    static bool copyFixed(uint8_t* out, std::size_t out_len, const uint8_t* in, std::size_t in_len)
    {
        if (!out || !in || out_len < in_len)
        {
            return false;
        }
        std::copy(in, in + in_len, out);
        return true;
    }

    bool markMeshCoreSend(uint32_t* out_tag, uint32_t* out_est_timeout, bool* out_sent_flood)
    {
        if (!meshcore_backend_ready)
        {
            return false;
        }
        if (out_tag)
        {
            *out_tag = static_cast<uint32_t>(meshcore_send_count + 1);
        }
        if (out_est_timeout)
        {
            *out_est_timeout = 0;
        }
        if (out_sent_flood)
        {
            *out_sent_flood = true;
        }
        meshcore_send_count++;
        return true;
    }

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
};

} // namespace phone::tests
