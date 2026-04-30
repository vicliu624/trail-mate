#pragma once

#include "chat/ble/phone_runtime_context.h"
#include "chat/domain/chat_types.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace chat
{
namespace contacts
{
struct NodeEntry;
}
} // namespace chat

namespace ble
{

struct MeshCoreBleFrame
{
    size_t len = 0;
    std::array<uint8_t, 172> buf{};
};

struct MeshCorePhoneBatteryInfo
{
    uint8_t level = 0;
    uint16_t millivolts = 0;
};

struct MeshCorePhoneLocation
{
    int32_t lat_i6 = 0;
    int32_t lon_i6 = 0;
};

struct MeshCorePhoneRadioStats
{
    int16_t noise_floor_dbm = 0;
    int8_t last_rssi_dbm = 0;
    int8_t last_snr_qdb = 0;
    uint32_t tx_air_seconds = 0;
    uint32_t rx_air_seconds = 0;
};

struct MeshCorePhonePacketStats
{
    uint32_t rx_packets = 0;
    uint32_t tx_packets = 0;
    uint32_t tx_flood = 0;
    uint32_t tx_direct = 0;
    uint32_t rx_flood = 0;
    uint32_t rx_direct = 0;
};

struct MeshCorePhoneTuningParams
{
    uint32_t rx_delay_base_ms = 0;
    uint32_t airtime_factor_milli = 0;
};

class MeshCorePhoneHooks
{
  public:
    virtual ~MeshCorePhoneHooks() = default;
    virtual uint32_t getReportedBlePin() const { return 0; }
    virtual MeshCorePhoneBatteryInfo getBatteryInfo() const { return {}; }
    virtual MeshCorePhoneLocation getAdvertLocation() const { return {}; }
    virtual uint8_t getAdvertLocationPolicy() const { return 0; }
    virtual uint8_t getTelemetryModeBits() const { return 0; }
    virtual bool getManualAddContacts() const { return false; }
    virtual bool resolvePeerPublicKey(const uint8_t* in_pubkey, size_t in_len,
                                      uint8_t* out_pubkey, size_t out_len) const
    {
        if (!in_pubkey || !out_pubkey || out_len == 0)
        {
            return false;
        }
        const size_t copy_len = (in_len < out_len) ? in_len : out_len;
        for (size_t i = 0; i < copy_len; ++i)
        {
            out_pubkey[i] = in_pubkey[i];
        }
        for (size_t i = copy_len; i < out_len; ++i)
        {
            out_pubkey[i] = 0;
        }
        return true;
    }
    virtual void onPendingBinaryRequest(uint32_t) {}
    virtual void onPendingTelemetryRequest(uint32_t) {}
    virtual void onPendingPathDiscoveryRequest(uint32_t) {}
    virtual void onSentRoute(bool) {}
    virtual bool lookupAdvertPath(const uint8_t*, size_t, uint32_t*, uint8_t*, size_t*) const { return false; }
    virtual bool hasActiveConnection(const uint8_t*, size_t) const { return false; }
    virtual void logoutActiveConnection(const uint8_t*, size_t) {}
    virtual bool getRadioStats(MeshCorePhoneRadioStats*) const { return false; }
    virtual bool getPacketStats(MeshCorePhonePacketStats*) const { return false; }
    virtual bool setAdvertLocation(int32_t, int32_t) { return false; }
    virtual bool upsertContactFromFrame(const uint8_t*, size_t) { return false; }
    virtual bool removeContact(const uint8_t*, size_t) { return false; }
    virtual bool exportContact(const uint8_t*, size_t, uint8_t*, size_t*) const { return false; }
    virtual bool importContact(const uint8_t*, size_t) { return false; }
    virtual bool shareContact(const uint8_t*, size_t) { return false; }
    virtual bool popOfflineMessage(uint8_t*, size_t*) { return false; }
    virtual bool setTuningParams(const MeshCorePhoneTuningParams&) { return false; }
    virtual bool getTuningParams(MeshCorePhoneTuningParams*) const { return false; }
    virtual bool setOtherParams(uint8_t, uint8_t, bool, uint8_t, uint8_t) { return false; }
    virtual bool setDevicePin(uint32_t) { return false; }
    virtual bool getCustomVars(std::string*) const { return false; }
    virtual bool setCustomVar(const char*, const char*) { return false; }
    virtual void onFactoryReset() {}
};

class MeshCorePhoneCore
{
  public:
    MeshCorePhoneCore(IPhoneRuntimeContext& ctx, const std::string& device_name,
                      MeshCorePhoneHooks* hooks = nullptr);

    void reset();
    void pumpIncomingAppData();
    void onIncomingText(const chat::MeshIncomingText& msg);
    bool handleRxFrame(const uint8_t* data, size_t len);
    bool popTxFrame(uint8_t* out, size_t* out_len);
    bool canHandleCommand(uint8_t cmd) const;

  private:
    void handleCmdFrame(const uint8_t* data, size_t len);
    void enqueueFrame(const uint8_t* data, size_t len);
    void enqueueSentOk();
    void enqueueErr(uint8_t err);
    void enqueueRawDataPush(const chat::MeshIncomingData& msg);
    uint32_t resolveNodeIdFromPrefix(const uint8_t* prefix, size_t len) const;
    bool buildContactFromNode(const chat::contacts::NodeEntry& entry, uint8_t code, MeshCoreBleFrame& out) const;

    IPhoneRuntimeContext& ctx_;
    std::string device_name_;
    MeshCorePhoneHooks* hooks_ = nullptr;
    std::deque<MeshCoreBleFrame> tx_queue_;
    std::vector<uint8_t> sign_data_;
    bool sign_active_ = false;
    uint32_t stats_rx_packets_ = 0;
    uint32_t stats_tx_packets_ = 0;
    uint32_t stats_tx_flood_ = 0;
    uint32_t stats_tx_direct_ = 0;
    uint32_t stats_rx_flood_ = 0;
    uint32_t stats_rx_direct_ = 0;
};

} // namespace ble
