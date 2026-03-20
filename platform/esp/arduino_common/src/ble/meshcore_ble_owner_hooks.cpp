#include "ble/meshcore_ble.h"

#include "app/app_config.h"
#include "board/BoardBase.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "sys/clock.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
constexpr size_t kPubKeySize = chat::meshcore::MeshCoreIdentity::kPubKeySize;
constexpr bool kMeshCoreBleSecurityEnabled = true;

int estimateBatteryMv(int percent)
{
    if (percent < 0)
    {
        return 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    return 3300 + (percent * 9);
}

bool isConfiguredBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}

int8_t encodeSnrQdb(int16_t snr_x10)
{
    if (snr_x10 == std::numeric_limits<int16_t>::min())
    {
        return 0;
    }
    int32_t qdb = (static_cast<int32_t>(snr_x10) * 4) / 10;
    if (qdb > 127)
    {
        qdb = 127;
    }
    else if (qdb < -128)
    {
        qdb = -128;
    }
    return static_cast<int8_t>(qdb);
}

int8_t encodeRssiDbm(int16_t rssi_x10)
{
    if (rssi_x10 == std::numeric_limits<int16_t>::min())
    {
        return 0;
    }
    int32_t dbm = rssi_x10 / 10;
    if (dbm > 127)
    {
        dbm = 127;
    }
    else if (dbm < -128)
    {
        dbm = -128;
    }
    return static_cast<int8_t>(dbm);
}

uint32_t nowSeconds()
{
    return sys::epoch_seconds_now();
}
} // namespace

namespace ble
{
MeshCorePhoneBatteryInfo MeshCoreBleService::getBatteryInfo() const
{
    MeshCorePhoneBatteryInfo info{};
    int level = -1;
    if (BoardBase* board = ctx_.getBoard())
    {
        level = board->getBatteryLevel();
    }
    if (level < 0)
    {
        level = 0;
    }
    if (level > 100)
    {
        level = 100;
    }
    info.level = static_cast<uint8_t>(level);
    info.millivolts = static_cast<uint16_t>(estimateBatteryMv(level));
    return info;
}

MeshCorePhoneLocation MeshCoreBleService::getAdvertLocation() const
{
    MeshCorePhoneLocation location{};
    location.lat_i6 = advert_lat_;
    location.lon_i6 = advert_lon_;
    return location;
}

uint32_t MeshCoreBleService::getReportedBlePin() const
{
    return effectiveBlePin();
}

uint8_t MeshCoreBleService::getAdvertLocationPolicy() const
{
    return advert_loc_policy_;
}

uint8_t MeshCoreBleService::getTelemetryModeBits() const
{
    return static_cast<uint8_t>((telemetry_mode_base_ & 0x03U) |
                                ((telemetry_mode_loc_ & 0x03U) << 2U) |
                                ((telemetry_mode_env_ & 0x03U) << 4U));
}

bool MeshCoreBleService::getManualAddContacts() const
{
    return manual_add_contacts_;
}

bool MeshCoreBleService::resolvePeerPublicKey(const uint8_t* in_pubkey, size_t in_len,
                                              uint8_t* out_pubkey, size_t out_len) const
{
    if (!in_pubkey || !out_pubkey || out_len < kPubKeySize || in_len < kPubKeySize)
    {
        return false;
    }
    chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
    if (!resolveContactByPubkey(in_pubkey, &peer, nullptr))
    {
        return false;
    }
    std::memcpy(out_pubkey, peer.pubkey, kPubKeySize);
    if (auto* adapter = const_cast<MeshCoreBleService*>(this)->meshCoreAdapter())
    {
        adapter->ensurePeerPublicKey(out_pubkey, kPubKeySize, peer.pubkey_verified);
    }
    return true;
}

void MeshCoreBleService::onPendingBinaryRequest(uint32_t tag)
{
    clearPendingRequests();
    pending_req_ = tag;
}

void MeshCoreBleService::onPendingTelemetryRequest(uint32_t tag)
{
    clearPendingRequests();
    pending_telemetry_ = tag;
}

void MeshCoreBleService::onPendingPathDiscoveryRequest(uint32_t tag)
{
    clearPendingRequests();
    pending_discovery_ = tag;
}

void MeshCoreBleService::onSentRoute(bool sent_flood)
{
    noteSentRoute(sent_flood);
}

bool MeshCoreBleService::lookupAdvertPath(const uint8_t* pubkey, size_t len,
                                          uint32_t* out_ts, uint8_t* out_path, size_t* inout_len) const
{
    if (!pubkey || len < kPubKeySize || !out_ts || !out_path || !inout_len)
    {
        return false;
    }

    if (const ContactRecord* rec = const_cast<MeshCoreBleService*>(this)->findManualContact(pubkey))
    {
        *out_ts = rec->last_advert;
        const size_t copy_len = std::min(static_cast<size_t>(rec->out_path_len), *inout_len);
        if (copy_len > 0)
        {
            std::memcpy(out_path, rec->out_path, copy_len);
        }
        *inout_len = copy_len;
        return true;
    }

    if (const auto* adapter = meshCoreAdapter())
    {
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (adapter->lookupPeerByHash(pubkey[0], &peer))
        {
            *out_ts = peer.last_seen_ms / 1000U;
            const size_t copy_len = std::min(static_cast<size_t>(peer.out_path_len), *inout_len);
            if (copy_len > 0)
            {
                std::memcpy(out_path, peer.out_path, copy_len);
            }
            *inout_len = copy_len;
            return true;
        }
    }

    return false;
}

bool MeshCoreBleService::hasActiveConnection(const uint8_t* prefix, size_t len) const
{
    if (!prefix || len < sizeof(uint32_t))
    {
        return false;
    }
    uint32_t prefix4 = 0;
    std::memcpy(&prefix4, prefix, sizeof(prefix4));
    const uint32_t now_ms = millis();
    for (const auto& entry : connections_)
    {
        if (entry.expires_ms != 0 && static_cast<int32_t>(now_ms - entry.expires_ms) >= 0)
        {
            continue;
        }
        if (entry.prefix4 == prefix4)
        {
            return true;
        }
    }
    return false;
}

void MeshCoreBleService::logoutActiveConnection(const uint8_t* prefix, size_t len)
{
    if (!prefix || len < sizeof(uint32_t))
    {
        return;
    }
    uint32_t prefix4 = 0;
    std::memcpy(&prefix4, prefix, sizeof(prefix4));
    for (auto it = connections_.begin(); it != connections_.end(); ++it)
    {
        if (it->prefix4 == prefix4)
        {
            connections_.erase(it);
            break;
        }
    }
}

bool MeshCoreBleService::getRadioStats(MeshCorePhoneRadioStats* out) const
{
    if (!out)
    {
        return false;
    }
    out->last_rssi_dbm = encodeRssiDbm(last_rssi_dbm_x10_);
    out->last_snr_qdb = encodeSnrQdb(last_snr_db_x10_);
    out->noise_floor_dbm = 0;
    out->tx_air_seconds = 0;
    out->rx_air_seconds = 0;
    if (const auto* adapter = meshCoreAdapter())
    {
        const auto radio_stats = adapter->getRadioStats();
        out->noise_floor_dbm = radio_stats.noise_floor_dbm;
        out->tx_air_seconds = radio_stats.tx_airtime_ms / 1000U;
        out->rx_air_seconds = radio_stats.rx_airtime_ms / 1000U;
    }
    return true;
}

bool MeshCoreBleService::getPacketStats(MeshCorePhonePacketStats* out) const
{
    if (!out)
    {
        return false;
    }
    out->rx_packets = stats_rx_packets_;
    out->tx_packets = stats_tx_packets_;
    out->tx_flood = stats_tx_flood_;
    out->tx_direct = stats_tx_direct_;
    out->rx_flood = stats_rx_flood_;
    out->rx_direct = stats_rx_direct_;
    return true;
}

bool MeshCoreBleService::setAdvertLocation(int32_t lat, int32_t lon)
{
    advert_lat_ = lat;
    advert_lon_ = lon;
    return true;
}

bool MeshCoreBleService::upsertContactFromFrame(const uint8_t* frame, size_t len)
{
    if (!frame)
    {
        return false;
    }
    ContactRecord record{};
    uint32_t lastmod = 0;
    if (!decodeContactPayload(frame, len, &record, &lastmod))
    {
        return false;
    }
    if (lastmod == 0)
    {
        lastmod = nowSeconds();
    }
    record.lastmod = lastmod;
    if (record.last_advert == 0)
    {
        record.last_advert = nowSeconds();
    }
    upsertManualContact(record);
    saveManualContacts();
    return true;
}

bool MeshCoreBleService::removeContact(const uint8_t* pubkey, size_t len)
{
    if (!pubkey || len < kPubKeySize)
    {
        return false;
    }
    if (!removeManualContact(pubkey))
    {
        return false;
    }
    saveManualContacts();
    return true;
}

bool MeshCoreBleService::exportContact(const uint8_t* pubkey, size_t len, uint8_t* out, size_t* out_len) const
{
    if (!out || !out_len)
    {
        return false;
    }
    auto* adapter = const_cast<MeshCoreBleService*>(this)->meshCoreAdapter();
    if (!adapter)
    {
        return false;
    }
    std::vector<uint8_t> frame;
    if (!pubkey || len < kPubKeySize)
    {
        const bool include_location = (advert_loc_policy_ != 0);
        if (!adapter->exportAdvertFrame(nullptr, 0, frame, include_location, advert_lat_, advert_lon_))
        {
            return false;
        }
    }
    else if (!adapter->exportAdvertFrame(pubkey, kPubKeySize, frame, false, 0, 0))
    {
        return false;
    }
    if (frame.empty() || frame.size() > *out_len)
    {
        return false;
    }
    std::memcpy(out, frame.data(), frame.size());
    *out_len = frame.size();
    return true;
}

bool MeshCoreBleService::importContact(const uint8_t* frame, size_t len)
{
    auto* adapter = meshCoreAdapter();
    return adapter ? adapter->importAdvertFrame(frame, len) : false;
}

bool MeshCoreBleService::shareContact(const uint8_t* pubkey, size_t len)
{
    auto* adapter = meshCoreAdapter();
    return (adapter && pubkey && len >= kPubKeySize) ? adapter->sendStoredAdvert(pubkey, kPubKeySize) : false;
}

bool MeshCoreBleService::popOfflineMessage(uint8_t* out, size_t* out_len)
{
    if (!out || !out_len || offline_queue_.empty())
    {
        return false;
    }
    Frame frame = offline_queue_.front();
    offline_queue_.pop_front();
    std::memcpy(out, frame.buf.data(), frame.len);
    *out_len = frame.len;
    return true;
}

bool MeshCoreBleService::setTuningParams(const MeshCorePhoneTuningParams& params)
{
    auto& cfg = ctx_.getConfig();
    cfg.meshcore_config.meshcore_rx_delay_base = static_cast<float>(params.rx_delay_base_ms) / 1000.0f;
    cfg.meshcore_config.meshcore_airtime_factor = static_cast<float>(params.airtime_factor_milli) / 1000.0f;
    ctx_.saveConfig();
    ctx_.applyMeshConfig();
    return true;
}

bool MeshCoreBleService::getTuningParams(MeshCorePhoneTuningParams* out) const
{
    if (!out)
    {
        return false;
    }
    const auto& cfg = ctx_.getConfig();
    out->rx_delay_base_ms = static_cast<uint32_t>(cfg.meshcore_config.meshcore_rx_delay_base * 1000.0f);
    out->airtime_factor_milli = static_cast<uint32_t>(cfg.meshcore_config.meshcore_airtime_factor * 1000.0f);
    return true;
}

bool MeshCoreBleService::setOtherParams(uint8_t manual_add_contacts, uint8_t telemetry_bits,
                                        bool has_multi_acks, uint8_t advert_loc_policy, uint8_t multi_acks)
{
    manual_add_contacts_ = (manual_add_contacts != 0);
    telemetry_mode_base_ = telemetry_bits & 0x03U;
    telemetry_mode_loc_ = (telemetry_bits >> 2U) & 0x03U;
    telemetry_mode_env_ = (telemetry_bits >> 4U) & 0x03U;
    advert_loc_policy_ = advert_loc_policy;
    if (has_multi_acks)
    {
        auto& cfg = ctx_.getConfig();
        cfg.meshcore_config.meshcore_multi_acks = (multi_acks != 0);
        multi_acks_ = multi_acks;
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
    }
    saveBlePin();
    return true;
}

bool MeshCoreBleService::setDevicePin(uint32_t pin)
{
    if (!isConfiguredBlePin(pin))
    {
        return false;
    }
    ble_pin_ = pin;
    if (kMeshCoreBleSecurityEnabled)
    {
        refreshBlePin();
    }
    saveBlePin();
    return true;
}

bool MeshCoreBleService::getCustomVars(std::string* out) const
{
    if (!out)
    {
        return false;
    }
    out->clear();
    char buf[40];
    if (gps::gps_is_enabled())
    {
        appendCustomVar(*out, "gps", gps::gps_is_powered() ? "1" : "0");
    }
    const auto& cfg = ctx_.getConfig();
    appendCustomVar(*out, "node_name", cfg.node_name);
    appendCustomVar(*out, "channel_name", cfg.meshcore_config.meshcore_channel_name);
    std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(ble_pin_));
    appendCustomVar(*out, "ble_pin", buf);
    appendCustomVar(*out, "manual_add_contacts", manual_add_contacts_ ? "1" : "0");
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(telemetry_mode_base_));
    appendCustomVar(*out, "telemetry_base", buf);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(telemetry_mode_loc_));
    appendCustomVar(*out, "telemetry_loc", buf);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(telemetry_mode_env_));
    appendCustomVar(*out, "telemetry_env", buf);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(advert_loc_policy_));
    appendCustomVar(*out, "advert_loc_policy", buf);
    appendCustomVar(*out, "multi_acks", cfg.meshcore_config.meshcore_multi_acks ? "1" : "0");
    return true;
}

bool MeshCoreBleService::setCustomVar(const char* key, const char* value)
{
    return handleCustomVarSet(key, value);
}

void MeshCoreBleService::onFactoryReset()
{
    manual_contacts_.clear();
    saveManualContacts();
    ble_pin_ = 0;
    manual_add_contacts_ = false;
    telemetry_mode_base_ = 0;
    telemetry_mode_loc_ = 0;
    telemetry_mode_env_ = 0;
    advert_loc_policy_ = 0;
    advert_lat_ = 0;
    advert_lon_ = 0;
    if (kMeshCoreBleSecurityEnabled)
    {
        refreshBlePin();
    }
    saveBlePin();
    connections_.clear();
    known_peer_hashes_.clear();
    multi_acks_ = 0;
    clearPendingRequests();
    if (auto* adapter = meshCoreAdapter())
    {
        adapter->setFloodScopeKey(nullptr, 0);
    }
}

} // namespace ble
