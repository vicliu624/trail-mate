#pragma once

#include "app/app_facades.h"
#include "ble/ble_manager.h"
#include "chat/ble/meshcore_phone_core.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "team/usecase/team_service.h"
#include <NimBLEDevice.h>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace ble
{

class MeshCoreBleService : public BleService,
                           public MeshCorePhoneHooks,
                           public chat::ChatService::IncomingTextObserver,
                           public team::TeamService::IncomingDataObserver,
                           public IPhoneRuntimeContext
{
  public:
    MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshCoreBleService() override;

    bool start() override;
    void stop() override;
    void update() override;

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;

  private:
    friend class MeshCoreRxCallbacks;
    friend class MeshCoreTxCallbacks;
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

    app::IAppBleFacade& ctx_;
    std::string device_name_;
    NimBLEServer* server_ = nullptr;
    NimBLEService* service_ = nullptr;
    NimBLECharacteristic* rx_char_ = nullptr;
    NimBLECharacteristic* tx_char_ = nullptr;
    bool connected_ = false;
    bool tx_subscribed_ = false;
    uint16_t conn_handle_ = 0;
    bool conn_handle_valid_ = false;
    uint16_t negotiated_mtu_ = 23;

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
    uint32_t active_ble_pin_ = 0;
    std::atomic<uint32_t> pending_passkey_{0};
    int16_t last_rssi_dbm_x10_ = 0;
    int16_t last_snr_db_x10_ = 0;
    uint32_t stats_tx_packets_ = 0;
    uint32_t stats_rx_packets_ = 0;
    uint32_t stats_tx_flood_ = 0;
    uint32_t stats_tx_direct_ = 0;
    uint32_t stats_rx_flood_ = 0;
    uint32_t stats_rx_direct_ = 0;
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
    std::unique_ptr<MeshCorePhoneCore> shared_core_;

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
    uint32_t effectiveBlePin() const;
    void refreshBlePin();
    void noteLinkStats(int16_t rssi_dbm_x10, int16_t snr_db_x10);
    void noteRxMeta(const chat::RxMeta& rx_meta);
    void noteEventRx(int8_t rssi_dbm, int8_t snr_qdb);
    void noteSentRoute(bool sent_flood);
    void enqueueRawDataPush(const uint8_t* payload, size_t len, const chat::RxMeta* meta);
    bool handleCustomVarSet(const char* key, const char* value);
    void appendCustomVar(std::string& out, const char* key, const char* value) const;
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
    bool shouldUseSharedCore(uint8_t cmd) const;
    bool handleViaSharedCore(size_t len);
    MeshCorePhoneBatteryInfo getBatteryInfo() const override;
    MeshCorePhoneLocation getAdvertLocation() const override;
    uint32_t getReportedBlePin() const override;
    uint8_t getAdvertLocationPolicy() const override;
    uint8_t getTelemetryModeBits() const override;
    bool getManualAddContacts() const override;
    bool resolvePeerPublicKey(const uint8_t* in_pubkey, size_t in_len,
                              uint8_t* out_pubkey, size_t out_len) const override;
    void onPendingBinaryRequest(uint32_t tag) override;
    void onPendingTelemetryRequest(uint32_t tag) override;
    void onPendingPathDiscoveryRequest(uint32_t tag) override;
    void onSentRoute(bool sent_flood) override;
    bool lookupAdvertPath(const uint8_t* pubkey, size_t len,
                          uint32_t* out_ts, uint8_t* out_path, size_t* inout_len) const override;
    bool hasActiveConnection(const uint8_t* prefix, size_t len) const override;
    void logoutActiveConnection(const uint8_t* prefix, size_t len) override;
    bool getRadioStats(MeshCorePhoneRadioStats* out) const override;
    bool getPacketStats(MeshCorePhonePacketStats* out) const override;
    bool setAdvertLocation(int32_t lat, int32_t lon) override;
    bool upsertContactFromFrame(const uint8_t* frame, size_t len) override;
    bool removeContact(const uint8_t* pubkey, size_t len) override;
    bool exportContact(const uint8_t* pubkey, size_t len, uint8_t* out, size_t* out_len) const override;
    bool importContact(const uint8_t* frame, size_t len) override;
    bool shareContact(const uint8_t* pubkey, size_t len) override;
    bool popOfflineMessage(uint8_t* out, size_t* out_len) override;
    bool setTuningParams(const MeshCorePhoneTuningParams& params) override;
    bool getTuningParams(MeshCorePhoneTuningParams* out) const override;
    bool setOtherParams(uint8_t manual_add_contacts, uint8_t telemetry_bits,
                        bool has_multi_acks, uint8_t advert_loc_policy, uint8_t multi_acks) override;
    bool setDevicePin(uint32_t pin) override;
    bool getCustomVars(std::string* out) const override;
    bool setCustomVar(const char* key, const char* value) override;
    void onFactoryReset() override;
    MeshtasticPhoneConfigSnapshot getMeshtasticPhoneConfig() const override;
    void setMeshtasticPhoneConfig(const MeshtasticPhoneConfigSnapshot& config) override;
    MeshCorePhoneConfigSnapshot getMeshCorePhoneConfig() const override;
    void setMeshCorePhoneConfig(const MeshCorePhoneConfigSnapshot& config) override;
    void saveConfig() override { ctx_.saveConfig(); }
    void applyMeshConfig() override { ctx_.applyMeshConfig(); }
    void applyUserInfo() override { ctx_.applyUserInfo(); }
    void applyPositionConfig() override { ctx_.applyPositionConfig(); }
    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override
    {
        ctx_.getEffectiveUserInfo(out_long, long_len, out_short, short_len);
    }
    chat::ChatService& getChatService() override { return ctx_.getChatService(); }
    chat::contacts::ContactService& getContactService() override { return ctx_.getContactService(); }
    chat::IMeshAdapter* getMeshAdapter() override { return ctx_.getMeshAdapter(); }
    const chat::IMeshAdapter* getMeshAdapter() const override { return ctx_.getMeshAdapter(); }
    chat::contacts::INodeStore* getNodeStore() override { return ctx_.getNodeStore(); }
    const chat::contacts::INodeStore* getNodeStore() const override { return ctx_.getNodeStore(); }
    chat::NodeId getSelfNodeId() const override { return ctx_.getSelfNodeId(); }
    bool isBleEnabled() const override { return ctx_.isBleEnabled(); }
    void setBleEnabled(bool enabled) override { ctx_.setBleEnabled(enabled); }
    bool getDeviceMacAddress(uint8_t out_mac[6]) const override { return ctx_.getDeviceMacAddress(out_mac); }
    bool syncCurrentEpochSeconds(uint32_t epoch_seconds) override
    {
        return ctx_.syncCurrentEpochSeconds(epoch_seconds);
    }
    void resetMeshConfig() override { ctx_.resetMeshConfig(); }
    void clearNodeDb() override { ctx_.clearNodeDb(); }
    void clearMessageDb() override { ctx_.clearMessageDb(); }
    void restartDevice() override { ctx_.restartDevice(); }
};

} // namespace ble
