#pragma once

#include "../app/app_context.h"
#include "../chat/infra/meshcore/meshcore_adapter.h"
#include "../chat/ports/i_node_store.h"
#include "../chat/usecase/chat_service.h"
#include "../team/usecase/team_service.h"
#include "ble_manager.h"
#include <NimBLEDevice.h>
#include <array>
#include <deque>
#include <string>
#include <vector>

namespace ble
{

class MeshCoreBleService : public BleService,
                           public chat::ChatService::IncomingTextObserver,
                           public team::TeamService::IncomingDataObserver
{
  public:
    MeshCoreBleService(app::AppContext& ctx, const std::string& device_name);
    ~MeshCoreBleService() override;

    void start() override;
    void stop() override;
    void update() override;

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;

  private:
    friend class MeshCoreRxCallbacks;
    friend class MeshCoreServerCallbacks;

    struct Frame
    {
        uint8_t len = 0;
        std::array<uint8_t, 172> buf{};
    };

    struct ContactRecord
    {
        uint8_t pubkey[chat::meshcore::MeshCoreIdentity::kPubKeySize] = {};
        uint8_t type = 0;
        uint8_t flags = 0;
        uint8_t out_path_len = 0;
        uint8_t out_path[64] = {};
        char name[32] = {};
        uint32_t last_advert = 0;
        int32_t lat = 0;
        int32_t lon = 0;
        uint32_t lastmod = 0;
    } __attribute__((packed));

    app::AppContext& ctx_;
    std::string device_name_;
    NimBLEServer* server_ = nullptr;
    NimBLEService* service_ = nullptr;
    NimBLECharacteristic* rx_char_ = nullptr;
    NimBLECharacteristic* tx_char_ = nullptr;
    bool connected_ = false;

    std::deque<Frame> outbound_;
    std::deque<Frame> rx_queue_;
    std::deque<Frame> offline_queue_;
    std::vector<ContactRecord> manual_contacts_;
    std::vector<uint8_t> known_peer_hashes_;

    uint32_t last_write_ms_ = 0;

    uint8_t app_target_ver_ = 0;
    bool contacts_iter_active_ = false;
    size_t contacts_iter_index_ = 0;
    uint32_t contacts_filter_since_ = 0;
    uint32_t contacts_most_recent_ = 0;
    std::vector<Frame> contacts_frames_;

    uint8_t cmd_frame_[173] = {};

    bool manual_add_contacts_ = false;
    uint8_t telemetry_mode_base_ = 0;
    uint8_t telemetry_mode_loc_ = 0;
    uint8_t telemetry_mode_env_ = 0;
    uint8_t advert_loc_policy_ = 0;
    uint8_t multi_acks_ = 0;

    std::vector<uint8_t> sign_data_;
    bool sign_active_ = false;
    uint32_t ble_pin_ = 0;
    int32_t advert_lat_ = 0;
    int32_t advert_lon_ = 0;
    uint32_t pending_login_ = 0;
    uint32_t pending_status_ = 0;
    uint32_t pending_status_tag_ = 0;
    uint32_t pending_telemetry_ = 0;
    uint32_t pending_req_ = 0;
    uint32_t pending_discovery_ = 0;

    struct ConnectionEntry
    {
        uint32_t prefix4 = 0;
        uint32_t expires_ms = 0;
        uint16_t keep_alive_secs = 0;
    };
    std::vector<ConnectionEntry> connections_;

    void setupService();
    void startAdvertising();
    void handleIncomingFrames();
    void handleCmdFrame(size_t len);
    void enqueueFrame(const uint8_t* data, size_t len);
    void enqueueOffline(const uint8_t* data, size_t len);
    void sendPendingFrames();
    void sendOfflineTickle();

    void loadManualContacts();
    void saveManualContacts();
    void loadBlePin();
    void saveBlePin();
    ContactRecord* findManualContact(const uint8_t* pubkey);
    const ContactRecord* findManualContactByPrefix(const uint8_t* prefix, size_t len) const;
    bool resolveContactByPubkey(const uint8_t* pubkey,
                                chat::meshcore::MeshCoreAdapter::PeerInfo* out_peer,
                                const ContactRecord** out_manual) const;
    void upsertManualContact(const ContactRecord& record);
    bool removeManualContact(const uint8_t* pubkey);
    void buildContactsSnapshot(uint32_t filter_since);
    static bool decodeContactPayload(const uint8_t* frame, size_t len,
                                     ContactRecord* out, uint32_t* out_lastmod);

    void clearPendingRequests();

    bool buildContactFrame(const chat::meshcore::MeshCoreAdapter::PeerInfo& peer,
                           uint8_t code, Frame& out);
    bool buildContactFromNode(const chat::contacts::NodeEntry& entry,
                              uint8_t code, Frame& out);
    bool lookupPeerByPrefix(const uint8_t* prefix, size_t len,
                            chat::meshcore::MeshCoreAdapter::PeerInfo* out) const;
    chat::meshcore::MeshCoreAdapter* meshCoreAdapter();
    const chat::meshcore::MeshCoreAdapter* meshCoreAdapter() const;
    uint32_t deriveNodeIdFromPubkey(const uint8_t* pubkey, size_t len) const;
};

} // namespace ble
