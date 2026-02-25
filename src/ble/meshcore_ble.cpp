#include "meshcore_ble.h"

#include "ble_uuids.h"
#include <Arduino.h>
#include "../board/BoardBase.h"
#include "../chat/domain/contact_types.h"
#include "../chat/infra/meshcore/meshcore_adapter.h"
#include <Preferences.h>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <limits>
#include <sys/time.h>
#include <string>

namespace
{
constexpr uint8_t CMD_DEVICE_QEURY = 22;
constexpr uint8_t CMD_APP_START = 1;
constexpr uint8_t CMD_SEND_TXT_MSG = 2;
constexpr uint8_t CMD_SEND_CHANNEL_TXT_MSG = 3;
constexpr uint8_t CMD_GET_CONTACTS = 4;
constexpr uint8_t CMD_SYNC_NEXT_MESSAGE = 10;
constexpr uint8_t CMD_GET_DEVICE_TIME = 5;
constexpr uint8_t CMD_SET_DEVICE_TIME = 6;
constexpr uint8_t CMD_SEND_SELF_ADVERT = 7;
constexpr uint8_t CMD_SET_ADVERT_NAME = 8;
constexpr uint8_t CMD_ADD_UPDATE_CONTACT = 9;
constexpr uint8_t CMD_GET_BATT_AND_STORAGE = 20;
constexpr uint8_t CMD_SET_RADIO_PARAMS = 11;
constexpr uint8_t CMD_SET_RADIO_TX_POWER = 12;
constexpr uint8_t CMD_RESET_PATH = 13;
constexpr uint8_t CMD_SET_ADVERT_LATLON = 14;
constexpr uint8_t CMD_REMOVE_CONTACT = 15;
constexpr uint8_t CMD_SHARE_CONTACT = 16;
constexpr uint8_t CMD_EXPORT_CONTACT = 17;
constexpr uint8_t CMD_IMPORT_CONTACT = 18;
constexpr uint8_t CMD_REBOOT = 19;
constexpr uint8_t CMD_SET_TUNING_PARAMS = 21;
constexpr uint8_t CMD_GET_TUNING_PARAMS = 43;
constexpr uint8_t CMD_SET_OTHER_PARAMS = 38;
constexpr uint8_t CMD_GET_CHANNEL = 31;
constexpr uint8_t CMD_SET_CHANNEL = 32;
constexpr uint8_t CMD_SIGN_START = 33;
constexpr uint8_t CMD_SIGN_DATA = 34;
constexpr uint8_t CMD_SIGN_FINISH = 35;
constexpr uint8_t CMD_SEND_TRACE_PATH = 36;
constexpr uint8_t CMD_SET_DEVICE_PIN = 37;
constexpr uint8_t CMD_EXPORT_PRIVATE_KEY = 23;
constexpr uint8_t CMD_IMPORT_PRIVATE_KEY = 24;
constexpr uint8_t CMD_SEND_RAW_DATA = 25;
constexpr uint8_t CMD_SEND_LOGIN = 26;
constexpr uint8_t CMD_SEND_STATUS_REQ = 27;
constexpr uint8_t CMD_HAS_CONNECTION = 28;
constexpr uint8_t CMD_LOGOUT = 29;
constexpr uint8_t CMD_SEND_PATH_DISCOVERY_REQ = 52;
constexpr uint8_t CMD_GET_CONTACT_BY_KEY = 30;
constexpr uint8_t CMD_SEND_TELEMETRY_REQ = 39;
constexpr uint8_t CMD_GET_CUSTOM_VARS = 40;
constexpr uint8_t CMD_SET_CUSTOM_VAR = 41;
constexpr uint8_t CMD_GET_ADVERT_PATH = 42;
constexpr uint8_t CMD_SEND_BINARY_REQ = 50;
constexpr uint8_t CMD_FACTORY_RESET = 51;
constexpr uint8_t CMD_SET_FLOOD_SCOPE = 54;
constexpr uint8_t CMD_SEND_CONTROL_DATA = 55;
constexpr uint8_t CMD_GET_STATS = 56;

constexpr uint8_t RESP_CODE_OK = 0;
constexpr uint8_t RESP_CODE_ERR = 1;
constexpr uint8_t RESP_CODE_CONTACTS_START = 2;
constexpr uint8_t RESP_CODE_CONTACT = 3;
constexpr uint8_t RESP_CODE_END_OF_CONTACTS = 4;
constexpr uint8_t RESP_CODE_SELF_INFO = 5;
constexpr uint8_t RESP_CODE_SENT = 6;
constexpr uint8_t RESP_CODE_CONTACT_MSG_RECV = 7;
constexpr uint8_t RESP_CODE_CHANNEL_MSG_RECV = 8;
constexpr uint8_t RESP_CODE_CONTACT_MSG_RECV_V3 = 16;
constexpr uint8_t RESP_CODE_CHANNEL_MSG_RECV_V3 = 17;
constexpr uint8_t RESP_CODE_CURR_TIME = 9;
constexpr uint8_t RESP_CODE_NO_MORE_MESSAGES = 10;
constexpr uint8_t RESP_CODE_EXPORT_CONTACT = 11;
constexpr uint8_t RESP_CODE_BATT_AND_STORAGE = 12;
constexpr uint8_t RESP_CODE_DEVICE_INFO = 13;
constexpr uint8_t RESP_CODE_PRIVATE_KEY = 14;
constexpr uint8_t RESP_CODE_DISABLED = 15;
constexpr uint8_t RESP_CODE_CHANNEL_INFO = 18;
constexpr uint8_t RESP_CODE_SIGN_START = 19;
constexpr uint8_t RESP_CODE_SIGNATURE = 20;
constexpr uint8_t RESP_CODE_CUSTOM_VARS = 21;
constexpr uint8_t RESP_CODE_ADVERT_PATH = 22;
constexpr uint8_t RESP_CODE_TUNING_PARAMS = 23;
constexpr uint8_t RESP_CODE_STATS = 24;

constexpr uint8_t PUSH_CODE_ADVERT = 0x80;
constexpr uint8_t PUSH_CODE_PATH_UPDATED = 0x81;
constexpr uint8_t PUSH_CODE_SEND_CONFIRMED = 0x82;
constexpr uint8_t PUSH_CODE_MSG_WAITING = 0x83;
constexpr uint8_t PUSH_CODE_RAW_DATA = 0x84;
constexpr uint8_t PUSH_CODE_LOGIN_SUCCESS = 0x85;
constexpr uint8_t PUSH_CODE_LOGIN_FAIL = 0x86;
constexpr uint8_t PUSH_CODE_STATUS_RESPONSE = 0x87;
constexpr uint8_t PUSH_CODE_LOG_RX_DATA = 0x88;
constexpr uint8_t PUSH_CODE_TRACE_DATA = 0x89;
constexpr uint8_t PUSH_CODE_NEW_ADVERT = 0x8A;
constexpr uint8_t PUSH_CODE_TELEMETRY_RESPONSE = 0x8B;
constexpr uint8_t PUSH_CODE_BINARY_RESPONSE = 0x8C;
constexpr uint8_t PUSH_CODE_PATH_DISCOVERY_RESPONSE = 0x8D;
constexpr uint8_t PUSH_CODE_CONTROL_DATA = 0x8E;

constexpr uint8_t ERR_CODE_UNSUPPORTED_CMD = 1;
constexpr uint8_t ERR_CODE_NOT_FOUND = 2;
constexpr uint8_t ERR_CODE_TABLE_FULL = 3;
constexpr uint8_t ERR_CODE_BAD_STATE = 4;
constexpr uint8_t ERR_CODE_FILE_IO_ERROR = 5;
constexpr uint8_t ERR_CODE_ILLEGAL_ARG = 6;

constexpr uint8_t ADV_TYPE_CHAT = 1;
constexpr uint8_t ADV_TYPE_ROOM = 3;
constexpr uint8_t TXT_TYPE_PLAIN = 0;
constexpr uint8_t TXT_TYPE_CLI_DATA = 1;
constexpr uint8_t TXT_TYPE_SIGNED_PLAIN = 2;

constexpr uint8_t STATS_TYPE_CORE = 0;
constexpr uint8_t STATS_TYPE_RADIO = 1;
constexpr uint8_t STATS_TYPE_PACKETS = 2;

constexpr size_t kMaxFrameSize = 172;
constexpr size_t kPubKeySize = chat::meshcore::MeshCoreIdentity::kPubKeySize;
constexpr size_t kPubKeyPrefixSize = 6;
constexpr size_t kMaxPathSize = 64;
constexpr uint32_t kBleWriteMinIntervalMs = 60;

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

uint32_t readUint32LE(const uint8_t* src)
{
    uint32_t out = 0;
    memcpy(&out, src, sizeof(out));
    return out;
}

uint32_t nowSeconds()
{
    time_t now = time(nullptr);
    if (now < 0)
    {
        return 0;
    }
    return static_cast<uint32_t>(now);
}

void copyBounded(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

bool isValidBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}

} // namespace

namespace ble
{

class MeshCoreRxCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshCoreRxCallbacks(MeshCoreBleService& owner) : owner_(owner) {}

    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
        if (!characteristic)
        {
            return;
        }
        NimBLEAttValue val = characteristic->getValue();
        if (val.length() == 0 || val.length() > kMaxFrameSize)
        {
            return;
        }
        MeshCoreBleService::Frame frame;
        frame.len = static_cast<uint8_t>(val.length());
        memcpy(frame.buf.data(), val.data(), frame.len);
        owner_.rx_queue_.push_back(frame);
    }

  private:
    MeshCoreBleService& owner_;
};

class MeshCoreServerCallbacks : public NimBLEServerCallbacks
{
  public:
    explicit MeshCoreServerCallbacks(MeshCoreBleService& owner) : owner_(owner) {}

    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override
    {
        (void)server;
        (void)connInfo;
        owner_.connected_ = true;
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        (void)reason;
        owner_.connected_ = false;
        owner_.outbound_.clear();
        owner_.rx_queue_.clear();
    }

  private:
    MeshCoreBleService& owner_;
};

MeshCoreBleService::MeshCoreBleService(app::AppContext& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name)
{
    loadBlePin();
    loadManualContacts();
}

MeshCoreBleService::~MeshCoreBleService()
{
    stop();
}

void MeshCoreBleService::start()
{
    NimBLEDevice::setMTU(185);
    setupService();
    startAdvertising();

    multi_acks_ = ctx_.getConfig().meshcore_config.meshcore_multi_acks ? 1 : 0;

    ctx_.getChatService().addIncomingTextObserver(this);
    if (auto* team = ctx_.getTeamService())
    {
        team->addIncomingDataObserver(this);
    }
}

void MeshCoreBleService::stop()
{
    ctx_.getChatService().removeIncomingTextObserver(this);
    if (auto* team = ctx_.getTeamService())
    {
        team->removeIncomingDataObserver(this);
    }

    outbound_.clear();
    rx_queue_.clear();
    offline_queue_.clear();

    if (server_)
    {
        server_->getAdvertising()->stop();
    }
    service_ = nullptr;
    rx_char_ = nullptr;
    tx_char_ = nullptr;
    server_ = nullptr;
}

void MeshCoreBleService::update()
{
    handleIncomingFrames();
    auto* adapter = static_cast<chat::meshcore::MeshCoreAdapter*>(ctx_.getMeshAdapter());
    if (adapter)
    {
        const uint32_t now_ms = millis();
        for (auto it = connections_.begin(); it != connections_.end();)
        {
            if (it->expires_ms != 0 &&
                static_cast<int32_t>(now_ms - it->expires_ms) >= 0)
            {
                it = connections_.erase(it);
                continue;
            }
            ++it;
        }

        auto fillPrefix = [&](uint8_t peer_hash, chat::NodeId peer_node,
                              uint8_t out_prefix[kPubKeyPrefixSize]) {
            memset(out_prefix, 0, kPubKeyPrefixSize);
            chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
            if (adapter->lookupPeerByHash(peer_hash, &peer) && peer.has_pubkey)
            {
                memcpy(out_prefix, peer.pubkey, kPubKeyPrefixSize);
                return;
            }
            if (peer_node != 0)
            {
                memcpy(out_prefix, &peer_node,
                       std::min(sizeof(peer_node), static_cast<size_t>(kPubKeyPrefixSize)));
                return;
            }
            if (peer_hash != 0)
            {
                out_prefix[0] = peer_hash;
            }
        };

        auto fillPubkey = [&](uint8_t peer_hash, chat::NodeId peer_node,
                              uint8_t out_pubkey[kPubKeySize]) {
            memset(out_pubkey, 0, kPubKeySize);
            chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
            if (adapter->lookupPeerByHash(peer_hash, &peer) && peer.has_pubkey)
            {
                memcpy(out_pubkey, peer.pubkey, kPubKeySize);
                return;
            }
            if (peer_node != 0)
            {
                memcpy(out_pubkey, &peer_node,
                       std::min(sizeof(peer_node), static_cast<size_t>(kPubKeySize)));
                return;
            }
            if (peer_hash != 0)
            {
                out_pubkey[0] = peer_hash;
            }
        };

        chat::meshcore::MeshCoreAdapter::Event ev;
        while (adapter->pollEvent(&ev))
        {
            switch (ev.type)
            {
            case chat::meshcore::MeshCoreAdapter::Event::Type::Response:
            {
                if (ev.payload.size() < 4)
                {
                    break;
                }
                uint32_t tag = 0;
                memcpy(&tag, ev.payload.data(), sizeof(tag));
                uint8_t prefix[kPubKeyPrefixSize] = {};
                fillPrefix(ev.peer_hash, ev.peer_node, prefix);
                uint32_t prefix4 = 0;
                memcpy(&prefix4, prefix, sizeof(prefix4));
                const uint8_t* data = ev.payload.data();
                const size_t data_len = ev.payload.size();

                if (pending_login_ != 0 && prefix4 == pending_login_)
                {
                    pending_login_ = 0;
                    uint8_t out[kMaxFrameSize] = {};
                    int i = 0;
                    if (data_len >= 6 && memcmp(&data[4], "OK", 2) == 0)
                    {
                        out[i++] = PUSH_CODE_LOGIN_SUCCESS;
                        out[i++] = 0;
                        memcpy(&out[i], prefix, kPubKeyPrefixSize);
                        i += kPubKeyPrefixSize;
                        enqueueFrame(out, i);
                        break;
                    }
                    if (data_len >= 8 && data[4] == 0)
                    {
                        uint16_t keep_alive_secs = static_cast<uint16_t>(data[5]) * 16U;
                        if (keep_alive_secs > 0)
                        {
                            ConnectionEntry entry{};
                            entry.prefix4 = prefix4;
                            entry.keep_alive_secs = keep_alive_secs;
                            entry.expires_ms = now_ms + static_cast<uint32_t>(keep_alive_secs) * 2500U;
                            bool updated = false;
                            for (auto& conn : connections_)
                            {
                                if (conn.prefix4 == entry.prefix4)
                                {
                                    conn = entry;
                                    updated = true;
                                    break;
                                }
                            }
                            if (!updated)
                            {
                                connections_.push_back(entry);
                            }
                        }
                        out[i++] = PUSH_CODE_LOGIN_SUCCESS;
                        out[i++] = data[6];
                        memcpy(&out[i], prefix, kPubKeyPrefixSize);
                        i += kPubKeyPrefixSize;
                        memcpy(&out[i], &tag, 4);
                        i += 4;
                        out[i++] = (data_len > 7) ? data[7] : 0;
                        out[i++] = (data_len > 12) ? data[12] : 0;
                        enqueueFrame(out, i);
                        break;
                    }
                    out[i++] = PUSH_CODE_LOGIN_FAIL;
                    out[i++] = 0;
                    memcpy(&out[i], prefix, kPubKeyPrefixSize);
                    i += kPubKeyPrefixSize;
                    enqueueFrame(out, i);
                    break;
                }

                if (pending_status_ != 0 &&
                    (prefix4 == pending_status_ ||
                     (pending_status_tag_ != 0 && tag == pending_status_tag_)))
                {
                    pending_status_ = 0;
                    pending_status_tag_ = 0;
                    if (data_len > 4)
                    {
                        uint8_t out[kMaxFrameSize] = {};
                        int i = 0;
                        out[i++] = PUSH_CODE_STATUS_RESPONSE;
                        out[i++] = 0;
                        memcpy(&out[i], prefix, kPubKeyPrefixSize);
                        i += kPubKeyPrefixSize;
                        size_t copy_len = std::min(data_len - 4, static_cast<size_t>(kMaxFrameSize - i));
                        memcpy(&out[i], &data[4], copy_len);
                        i += static_cast<int>(copy_len);
                        enqueueFrame(out, i);
                    }
                    break;
                }

                if (pending_telemetry_ != 0 && tag == pending_telemetry_)
                {
                    pending_telemetry_ = 0;
                    if (data_len > 4)
                    {
                        uint8_t out[kMaxFrameSize] = {};
                        int i = 0;
                        out[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
                        out[i++] = 0;
                        memcpy(&out[i], prefix, kPubKeyPrefixSize);
                        i += kPubKeyPrefixSize;
                        size_t copy_len = std::min(data_len - 4, static_cast<size_t>(kMaxFrameSize - i));
                        memcpy(&out[i], &data[4], copy_len);
                        i += static_cast<int>(copy_len);
                        enqueueFrame(out, i);
                    }
                    break;
                }

                if (pending_req_ != 0 && tag == pending_req_)
                {
                    pending_req_ = 0;
                    uint8_t out[kMaxFrameSize] = {};
                    int i = 0;
                    out[i++] = PUSH_CODE_BINARY_RESPONSE;
                    out[i++] = 0;
                    memcpy(&out[i], &tag, 4);
                    i += 4;
                    if (data_len > 4)
                    {
                        size_t copy_len = std::min(data_len - 4, static_cast<size_t>(kMaxFrameSize - i));
                        memcpy(&out[i], &data[4], copy_len);
                        i += static_cast<int>(copy_len);
                    }
                    enqueueFrame(out, i);
                    break;
                }
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::PathResponse:
            {
                if (pending_discovery_ == 0 || ev.tag != pending_discovery_)
                {
                    break;
                }
                pending_discovery_ = 0;
                uint8_t prefix[kPubKeyPrefixSize] = {};
                fillPrefix(ev.peer_hash, ev.peer_node, prefix);
                uint8_t out[kMaxFrameSize] = {};
                int i = 0;
                out[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
                out[i++] = 0;
                memcpy(&out[i], prefix, kPubKeyPrefixSize);
                i += kPubKeyPrefixSize;
                uint8_t out_len = static_cast<uint8_t>(
                    std::min(ev.out_path.size(), static_cast<size_t>(kMaxPathSize)));
                out[i++] = out_len;
                if (out_len > 0)
                {
                    memcpy(&out[i], ev.out_path.data(), out_len);
                    i += out_len;
                }
                uint8_t in_len = static_cast<uint8_t>(
                    std::min(ev.in_path.size(), static_cast<size_t>(kMaxPathSize)));
                out[i++] = in_len;
                if (in_len > 0)
                {
                    memcpy(&out[i], ev.in_path.data(), in_len);
                    i += in_len;
                }
                enqueueFrame(out, i);
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::ControlData:
            {
                uint8_t out[kMaxFrameSize] = {};
                int i = 0;
                out[i++] = PUSH_CODE_CONTROL_DATA;
                out[i++] = static_cast<uint8_t>(ev.snr_qdb);
                out[i++] = static_cast<uint8_t>(ev.rssi_dbm);
                out[i++] = ev.flags;
                size_t copy_len = std::min(ev.payload.size(), static_cast<size_t>(kMaxFrameSize - i));
                if (copy_len > 0)
                {
                    memcpy(&out[i], ev.payload.data(), copy_len);
                    i += static_cast<int>(copy_len);
                }
                enqueueFrame(out, i);
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::RawData:
            {
                uint8_t out[kMaxFrameSize] = {};
                int i = 0;
                out[i++] = PUSH_CODE_RAW_DATA;
                out[i++] = static_cast<uint8_t>(ev.snr_qdb);
                out[i++] = static_cast<uint8_t>(ev.rssi_dbm);
                out[i++] = 0xFF;
                size_t copy_len = std::min(ev.payload.size(), static_cast<size_t>(kMaxFrameSize - i));
                if (copy_len > 0)
                {
                    memcpy(&out[i], ev.payload.data(), copy_len);
                    i += static_cast<int>(copy_len);
                }
                enqueueFrame(out, i);
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::TraceData:
            {
                uint8_t out[kMaxFrameSize] = {};
                int i = 0;
                out[i++] = PUSH_CODE_TRACE_DATA;
                out[i++] = 0;
                uint8_t path_len = static_cast<uint8_t>(
                    std::min(ev.trace_hashes.size(), static_cast<size_t>(kMaxPathSize)));
                out[i++] = path_len;
                out[i++] = ev.flags;
                memcpy(&out[i], &ev.tag, 4);
                i += 4;
                memcpy(&out[i], &ev.auth, 4);
                i += 4;
                if (path_len > 0)
                {
                    memcpy(&out[i], ev.trace_hashes.data(), path_len);
                    i += path_len;
                }
                size_t snr_len = std::min(ev.trace_snrs.size(), static_cast<size_t>(kMaxPathSize));
                if (snr_len > 0 && i + snr_len < kMaxFrameSize)
                {
                    memcpy(&out[i], ev.trace_snrs.data(), snr_len);
                    i += static_cast<int>(snr_len);
                }
                if (i < kMaxFrameSize)
                {
                    out[i++] = static_cast<uint8_t>(ev.snr_qdb);
                }
                enqueueFrame(out, i);
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::Advert:
            {
                uint8_t pubkey[kPubKeySize] = {};
                fillPubkey(ev.peer_hash, ev.peer_node, pubkey);
                bool known = false;
                for (uint8_t hash : known_peer_hashes_)
                {
                    if (hash == ev.peer_hash)
                    {
                        known = true;
                        break;
                    }
                }
                if (!known)
                {
                    known_peer_hashes_.push_back(ev.peer_hash);
                }
                const bool is_new = ev.advert_is_new || !known;
                if (manual_add_contacts_ && is_new)
                {
                    chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
                    if (adapter->lookupPeerByHash(ev.peer_hash, &peer))
                    {
                        Frame frame;
                        if (buildContactFrame(peer, PUSH_CODE_NEW_ADVERT, frame))
                        {
                            enqueueFrame(frame.buf.data(), frame.len);
                            break;
                        }
                    }
                }
                uint8_t out[kPubKeySize + 1] = {};
                out[0] = PUSH_CODE_ADVERT;
                memcpy(&out[1], pubkey, kPubKeySize);
                enqueueFrame(out, sizeof(out));
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::PathUpdated:
            {
                uint8_t pubkey[kPubKeySize] = {};
                fillPubkey(ev.peer_hash, ev.peer_node, pubkey);
                uint8_t out[kPubKeySize + 1] = {};
                out[0] = PUSH_CODE_PATH_UPDATED;
                memcpy(&out[1], pubkey, kPubKeySize);
                enqueueFrame(out, sizeof(out));
                break;
            }
            case chat::meshcore::MeshCoreAdapter::Event::Type::SendConfirmed:
            {
                uint8_t out[9] = {};
                out[0] = PUSH_CODE_SEND_CONFIRMED;
                memcpy(&out[1], &ev.tag, 4);
                memcpy(&out[5], &ev.trip_ms, 4);
                enqueueFrame(out, sizeof(out));
                break;
            }
            default:
                break;
            }
        }
    }
    sendPendingFrames();
}

void MeshCoreBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    Frame frame;
    int i = 0;
    const bool use_v3 = (app_target_ver_ >= 3);
    auto encodeSnrQdb = [](int16_t snr_x10) -> int8_t {
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
    };
    const uint8_t path_len = msg.rx_meta.direct ? 0xFF : msg.rx_meta.hop_count;
    if (msg.to == 0xFFFFFFFF || msg.to == 0)
    {
        frame.buf[i++] = use_v3 ? RESP_CODE_CHANNEL_MSG_RECV_V3 : RESP_CODE_CHANNEL_MSG_RECV;
        if (use_v3)
        {
            const int8_t snr = encodeSnrQdb(msg.rx_meta.snr_db_x10);
            frame.buf[i++] = static_cast<uint8_t>(snr);
            frame.buf[i++] = 0;
            frame.buf[i++] = 0;
        }
        frame.buf[i++] = static_cast<uint8_t>(msg.channel);
        frame.buf[i++] = path_len;
    }
    else
    {
        frame.buf[i++] = use_v3 ? RESP_CODE_CONTACT_MSG_RECV_V3 : RESP_CODE_CONTACT_MSG_RECV;
        if (use_v3)
        {
            const int8_t snr = encodeSnrQdb(msg.rx_meta.snr_db_x10);
            frame.buf[i++] = static_cast<uint8_t>(snr);
            frame.buf[i++] = 0;
            frame.buf[i++] = 0;
        }
        uint8_t prefix[kPubKeyPrefixSize] = {};
        auto* adapter = static_cast<chat::meshcore::MeshCoreAdapter*>(ctx_.getMeshAdapter());
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (adapter && adapter->lookupPeerByNodeId(msg.from, &peer) && peer.has_pubkey)
        {
            memcpy(prefix, peer.pubkey, kPubKeyPrefixSize);
        }
        else
        {
            memcpy(prefix, &msg.from, std::min(sizeof(msg.from), sizeof(prefix)));
        }
        memcpy(&frame.buf[i], prefix, kPubKeyPrefixSize);
        i += kPubKeyPrefixSize;
        frame.buf[i++] = path_len;
    }

    frame.buf[i++] = TXT_TYPE_PLAIN;
    memcpy(&frame.buf[i], &msg.timestamp, 4);
    i += 4;

    size_t text_len = msg.text.size();
    size_t avail = kMaxFrameSize - i;
    if (text_len > avail)
    {
        text_len = avail;
    }
    memcpy(&frame.buf[i], msg.text.data(), text_len);
    i += static_cast<int>(text_len);

    frame.len = static_cast<uint8_t>(i);
    enqueueOffline(frame.buf.data(), frame.len);
    sendOfflineTickle();
}

void MeshCoreBleService::onIncomingData(const chat::MeshIncomingData& msg)
{
    (void)msg;
}

void MeshCoreBleService::setupService()
{
    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(new MeshCoreServerCallbacks(*this));

    service_ = server_->createService(NUS_SERVICE_UUID);
    tx_char_ = service_->createCharacteristic(NUS_CHAR_TX_UUID,
                                              NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    rx_char_ = service_->createCharacteristic(NUS_CHAR_RX_UUID, NIMBLE_PROPERTY::WRITE);
    rx_char_->setCallbacks(new MeshCoreRxCallbacks(*this));

    service_->start();
}

void MeshCoreBleService::startAdvertising()
{
    if (!server_)
    {
        return;
    }
    NimBLEAdvertising* adv = server_->getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->enableScanResponse(true);
    adv->start();
}

void MeshCoreBleService::handleIncomingFrames()
{
    while (!rx_queue_.empty())
    {
        Frame frame = rx_queue_.front();
        rx_queue_.pop_front();
        if (frame.len == 0)
        {
            continue;
        }
        memcpy(cmd_frame_, frame.buf.data(), frame.len);
        handleCmdFrame(frame.len);
    }
}

void MeshCoreBleService::handleCmdFrame(size_t len)
{
    if (len == 0 || len > kMaxFrameSize)
    {
        return;
    }

    const uint8_t cmd = cmd_frame_[0];
    auto* adapter = static_cast<chat::meshcore::MeshCoreAdapter*>(ctx_.getMeshAdapter());
    auto& cfg = ctx_.getConfig();

    auto writeOk = [&]() {
        uint8_t code = RESP_CODE_OK;
        enqueueFrame(&code, 1);
    };

    auto writeErr = [&](uint8_t err) {
        uint8_t buf[2] = {RESP_CODE_ERR, err};
        enqueueFrame(buf, sizeof(buf));
    };

    if (cmd == CMD_DEVICE_QEURY && len >= 2)
    {
        app_target_ver_ = cmd_frame_[1];
        uint8_t out[kMaxFrameSize] = {};
        int i = 0;
        out[i++] = RESP_CODE_DEVICE_INFO;
        out[i++] = 8;
        out[i++] = 50;
        out[i++] = 2;
        memcpy(&out[i], &ble_pin_, 4);
        i += 4;
        memset(&out[i], 0, 12);
        strncpy(reinterpret_cast<char*>(&out[i]), __DATE__, 11);
        i += 12;
        strncpy(reinterpret_cast<char*>(&out[i]), "TrailMate", 40);
        i += 40;
        strncpy(reinterpret_cast<char*>(&out[i]), "trail-mate", 20);
        i += 20;
        enqueueFrame(out, i);
        return;
    }

    if (cmd == CMD_APP_START && len >= 8)
    {
        contacts_iter_active_ = false;
        contacts_frames_.clear();
        uint8_t out[kMaxFrameSize] = {};
        int i = 0;
        out[i++] = RESP_CODE_SELF_INFO;
        out[i++] = ADV_TYPE_CHAT;
        out[i++] = static_cast<uint8_t>(cfg.meshcore_config.tx_power);
        out[i++] = static_cast<uint8_t>(app::AppConfig::kTxPowerMaxDbm);
        uint8_t pubkey[kPubKeySize] = {};
        if (adapter)
        {
            adapter->exportIdentityPublicKey(pubkey);
        }
        memcpy(&out[i], pubkey, kPubKeySize);
        i += kPubKeySize;
        int32_t lat = 0;
        int32_t lon = 0;
        memcpy(&out[i], &lat, 4);
        i += 4;
        memcpy(&out[i], &lon, 4);
        i += 4;
        out[i++] = cfg.meshcore_config.meshcore_multi_acks ? 1 : 0;
        out[i++] = advert_loc_policy_;
        out[i++] = static_cast<uint8_t>((telemetry_mode_env_ << 4) | (telemetry_mode_loc_ << 2) | telemetry_mode_base_);
        out[i++] = manual_add_contacts_ ? 1 : 0;

        uint32_t freq = static_cast<uint32_t>(cfg.meshcore_config.meshcore_freq_mhz * 1000.0f);
        uint32_t bw = static_cast<uint32_t>(cfg.meshcore_config.meshcore_bw_khz * 1000.0f);
        memcpy(&out[i], &freq, 4);
        i += 4;
        memcpy(&out[i], &bw, 4);
        i += 4;
        out[i++] = cfg.meshcore_config.meshcore_sf;
        out[i++] = cfg.meshcore_config.meshcore_cr;

        size_t name_len = strnlen(cfg.node_name, sizeof(cfg.node_name));
        if (name_len > (kMaxFrameSize - i))
        {
            name_len = kMaxFrameSize - i;
        }
        memcpy(&out[i], cfg.node_name, name_len);
        i += static_cast<int>(name_len);
        enqueueFrame(out, i);
        return;
    }

    if (cmd == CMD_SEND_TXT_MSG && len >= 14)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        int i = 1;
        uint8_t txt_type = cmd_frame_[i++];
        i++; // attempt
        i += 4; // timestamp
        uint8_t* pub_prefix = &cmd_frame_[i];
        i += 6;

        if (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_CLI_DATA)
        {
            writeErr(ERR_CODE_UNSUPPORTED_CMD);
            return;
        }

        const ContactRecord* manual = findManualContactByPrefix(pub_prefix, kPubKeyPrefixSize);
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        const uint8_t* full_pubkey = nullptr;
        if (manual)
        {
            full_pubkey = manual->pubkey;
        }
        else if (lookupPeerByPrefix(pub_prefix, kPubKeyPrefixSize, &peer))
        {
            full_pubkey = peer.pubkey;
        }
        if (!full_pubkey)
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        adapter->ensurePeerPublicKey(full_pubkey, kPubKeySize, peer.pubkey_verified);
        uint32_t node_id = deriveNodeIdFromPubkey(full_pubkey, kPubKeySize);
        std::string text(reinterpret_cast<char*>(&cmd_frame_[i]), len - i);

        uint32_t expected_ack = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!adapter->sendDirectTextDetailed(chat::ChannelId::PRIMARY, text, node_id,
                                             txt_type,
                                             &expected_ack, &est_timeout, &sent_flood))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1 : 0;
        memcpy(&out[2], &expected_ack, 4);
        memcpy(&out[6], &est_timeout, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_CHANNEL_TXT_MSG && len >= 7)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        int i = 1;
        uint8_t txt_type = cmd_frame_[i++];
        uint8_t channel_idx = cmd_frame_[i++];
        i += 4; // timestamp
        if (txt_type != TXT_TYPE_PLAIN)
        {
            writeErr(ERR_CODE_UNSUPPORTED_CMD);
            return;
        }
        std::string text(reinterpret_cast<char*>(&cmd_frame_[i]), len - i);
        chat::ChannelId ch = (channel_idx == 1) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;
        chat::MessageId out_id = 0;
        if (!adapter->sendText(ch, text, &out_id, 0))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        writeOk();
        return;
    }

    if (cmd == CMD_SEND_LOGIN && len >= 1 + kPubKeySize)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        const uint8_t* pubkey = &cmd_frame_[1];
        const size_t pass_len = (len > (1 + kPubKeySize)) ? (len - (1 + kPubKeySize)) : 0;
        char password[16] = {};
        if (pass_len > 0)
        {
            size_t copy_len = std::min(pass_len, sizeof(password) - 1);
            memcpy(password, &cmd_frame_[1 + kPubKeySize], copy_len);
            password[copy_len] = '\0';
        }

        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        const ContactRecord* manual = nullptr;
        if (!resolveContactByPubkey(pubkey, &peer, &manual))
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        adapter->ensurePeerPublicKey(pubkey, kPubKeySize, peer.pubkey_verified);

        bool is_room = false;
        uint32_t sync_since = 0;
        if (manual)
        {
            is_room = (manual->type == ADV_TYPE_ROOM);
            sync_since = manual->lastmod;
        }

        uint8_t payload[24] = {};
        size_t payload_len = 0;
        uint32_t now_ts = nowSeconds();
        memcpy(&payload[payload_len], &now_ts, 4);
        payload_len += 4;
        if (is_room)
        {
            memcpy(&payload[payload_len], &sync_since, 4);
            payload_len += 4;
        }
        if (password[0] != '\0')
        {
            size_t pw_len = strnlen(password, sizeof(password) - 1);
            if (pw_len > (sizeof(payload) - payload_len))
            {
                pw_len = sizeof(payload) - payload_len;
            }
            memcpy(&payload[payload_len], password, pw_len);
            payload_len += pw_len;
        }

        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!adapter->sendAnonRequestPayload(pubkey, kPubKeySize,
                                             payload, payload_len,
                                             &est_timeout, &sent_flood))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        clearPendingRequests();
        uint32_t prefix4 = 0;
        memcpy(&prefix4, pubkey, sizeof(prefix4));
        pending_login_ = prefix4;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1 : 0;
        memcpy(&out[2], &prefix4, 4);
        memcpy(&out[6], &est_timeout, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_STATUS_REQ && len >= 1 + kPubKeySize)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        const uint8_t* pubkey = &cmd_frame_[1];
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (!resolveContactByPubkey(pubkey, &peer, nullptr))
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        adapter->ensurePeerPublicKey(pubkey, kPubKeySize, peer.pubkey_verified);
        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!adapter->sendPeerRequestType(pubkey, kPubKeySize, 0x01,
                                          &tag, &est_timeout, &sent_flood))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        clearPendingRequests();
        uint32_t prefix4 = 0;
        memcpy(&prefix4, pubkey, sizeof(prefix4));
        pending_status_ = prefix4;
        pending_status_tag_ = tag;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1 : 0;
        memcpy(&out[2], &tag, 4);
        memcpy(&out[6], &est_timeout, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_GET_CONTACTS)
    {
        if (contacts_iter_active_)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }

        uint32_t filter_since = 0;
        if (len >= 5)
        {
            filter_since = readUint32LE(&cmd_frame_[1]);
        }

        size_t total_contacts = manual_contacts_.size();
        if (adapter)
        {
            std::vector<chat::meshcore::MeshCoreAdapter::PeerInfo> peers;
            adapter->getPeerInfos(peers);
            total_contacts += peers.size();
        }
        if (auto* store = ctx_.getNodeStore())
        {
            total_contacts += store->getEntries().size();
        }

        buildContactsSnapshot(filter_since);

        uint8_t start_buf[5] = {RESP_CODE_CONTACTS_START, 0, 0, 0, 0};
        uint32_t total32 = static_cast<uint32_t>(total_contacts);
        memcpy(&start_buf[1], &total32, 4);
        enqueueFrame(start_buf, sizeof(start_buf));

        for (const auto& frame : contacts_frames_)
        {
            enqueueFrame(frame.buf.data(), frame.len);
        }

        uint8_t end_buf[5] = {RESP_CODE_END_OF_CONTACTS, 0, 0, 0, 0};
        memcpy(&end_buf[1], &contacts_most_recent_, 4);
        enqueueFrame(end_buf, sizeof(end_buf));
        contacts_iter_active_ = false;
        return;
    }

    if (cmd == CMD_SET_ADVERT_NAME && len >= 2)
    {
        size_t nlen = len - 1;
        if (nlen >= sizeof(cfg.node_name))
        {
            nlen = sizeof(cfg.node_name) - 1;
        }
        memcpy(cfg.node_name, &cmd_frame_[1], nlen);
        cfg.node_name[nlen] = '\0';
        ctx_.saveConfig();
        ctx_.applyUserInfo();
        writeOk();
        return;
    }

    if (cmd == CMD_SET_ADVERT_LATLON && len >= 9)
    {
        memcpy(&advert_lat_, &cmd_frame_[1], 4);
        memcpy(&advert_lon_, &cmd_frame_[5], 4);
        writeOk();
        return;
    }

    if (cmd == CMD_SEND_SELF_ADVERT)
    {
        const bool broadcast = (len >= 2 && cmd_frame_[1] == 1);
        const bool include_location = (advert_loc_policy_ != 0);
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
        }
        else if (adapter->sendIdentityAdvertWithLocation(broadcast, include_location,
                                                         advert_lat_, advert_lon_))
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_TABLE_FULL);
        }
        return;
    }

    if (cmd == CMD_ADD_UPDATE_CONTACT)
    {
        ContactRecord record{};
        uint32_t lastmod = 0;
        if (!decodeContactPayload(cmd_frame_, len, &record, &lastmod))
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
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
        writeOk();
        return;
    }

    if (cmd == CMD_REMOVE_CONTACT && len >= 1 + kPubKeySize)
    {
        if (removeManualContact(&cmd_frame_[1]))
        {
            saveManualContacts();
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_NOT_FOUND);
        }
        return;
    }

    if (cmd == CMD_EXPORT_CONTACT)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        std::vector<uint8_t> frame;
        if (len < 1 + kPubKeySize)
        {
            const bool include_location = (advert_loc_policy_ != 0);
            if (!adapter->exportAdvertFrame(nullptr, 0, frame,
                                            include_location, advert_lat_, advert_lon_))
            {
                writeErr(ERR_CODE_TABLE_FULL);
                return;
            }
        }
        else
        {
            const uint8_t* pubkey = &cmd_frame_[1];
            if (!adapter->exportAdvertFrame(pubkey, kPubKeySize, frame, false, 0, 0))
            {
                writeErr(ERR_CODE_NOT_FOUND);
                return;
            }
        }
        if (frame.empty() || frame.size() > (kMaxFrameSize - 1))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        uint8_t out[kMaxFrameSize] = {};
        out[0] = RESP_CODE_EXPORT_CONTACT;
        memcpy(&out[1], frame.data(), frame.size());
        enqueueFrame(out, frame.size() + 1);
        return;
    }

    if (cmd == CMD_IMPORT_CONTACT && len >= 2)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (adapter->importAdvertFrame(&cmd_frame_[1], len - 1))
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
        }
        return;
    }

    if (cmd == CMD_RESET_PATH && len >= 1 + kPubKeySize)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t peer_hash = cmd_frame_[1];
        if (adapter->clearPeerPath(peer_hash))
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_NOT_FOUND);
        }
        return;
    }

    if (cmd == CMD_SHARE_CONTACT)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (len < 1 + kPubKeySize)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        if (adapter->sendStoredAdvert(&cmd_frame_[1], kPubKeySize))
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_NOT_FOUND);
        }
        return;
    }

    if (cmd == CMD_REBOOT)
    {
        writeOk();
        delay(200);
        ESP.restart();
        return;
    }

    if (cmd == CMD_SYNC_NEXT_MESSAGE)
    {
        if (offline_queue_.empty())
        {
            uint8_t out = RESP_CODE_NO_MORE_MESSAGES;
            enqueueFrame(&out, 1);
        }
        else
        {
            Frame frame = offline_queue_.front();
            offline_queue_.pop_front();
            enqueueFrame(frame.buf.data(), frame.len);
        }
        return;
    }

    if (cmd == CMD_GET_DEVICE_TIME)
    {
        uint8_t out[5] = {};
        out[0] = RESP_CODE_CURR_TIME;
        uint32_t now = nowSeconds();
        memcpy(&out[1], &now, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SET_DEVICE_TIME && len >= 5)
    {
        uint32_t epoch = readUint32LE(&cmd_frame_[1]);
        timeval tv{};
        tv.tv_sec = static_cast<time_t>(epoch);
        tv.tv_usec = 0;
        if (settimeofday(&tv, nullptr) == 0)
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_FILE_IO_ERROR);
        }
        return;
    }

    if (cmd == CMD_GET_BATT_AND_STORAGE)
    {
        uint8_t out[11] = {};
        out[0] = RESP_CODE_BATT_AND_STORAGE;
        int percent = board.getBatteryLevel();
        uint16_t mv = static_cast<uint16_t>(estimateBatteryMv(percent));
        uint32_t used = 0;
        uint32_t total = 0;
        memcpy(&out[1], &mv, 2);
        memcpy(&out[3], &used, 4);
        memcpy(&out[7], &total, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SET_RADIO_PARAMS && len >= 11)
    {
        uint32_t freq = readUint32LE(&cmd_frame_[1]);
        uint32_t bw = readUint32LE(&cmd_frame_[5]);
        uint8_t sf = cmd_frame_[9];
        uint8_t cr = cmd_frame_[10];
        if (freq < 300000 || freq > 2500000 || bw < 7000 || bw > 500000 || sf < 5 || sf > 12 || cr < 5 || cr > 8)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        cfg.meshcore_config.meshcore_freq_mhz = static_cast<float>(freq) / 1000.0f;
        cfg.meshcore_config.meshcore_bw_khz = static_cast<float>(bw) / 1000.0f;
        cfg.meshcore_config.meshcore_sf = sf;
        cfg.meshcore_config.meshcore_cr = cr;
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        writeOk();
        return;
    }

    if (cmd == CMD_SET_RADIO_TX_POWER && len >= 2)
    {
        int8_t tx = static_cast<int8_t>(cmd_frame_[1]);
        if (tx < app::AppConfig::kTxPowerMinDbm || tx > app::AppConfig::kTxPowerMaxDbm)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        cfg.meshcore_config.tx_power = tx;
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        writeOk();
        return;
    }

    if (cmd == CMD_SET_TUNING_PARAMS && len >= 9)
    {
        uint32_t rx = readUint32LE(&cmd_frame_[1]);
        uint32_t af = readUint32LE(&cmd_frame_[5]);
        cfg.meshcore_config.meshcore_rx_delay_base = static_cast<float>(rx) / 1000.0f;
        cfg.meshcore_config.meshcore_airtime_factor = static_cast<float>(af) / 1000.0f;
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        writeOk();
        return;
    }

    if (cmd == CMD_GET_TUNING_PARAMS)
    {
        uint8_t out[9] = {};
        out[0] = RESP_CODE_TUNING_PARAMS;
        uint32_t rx = static_cast<uint32_t>(cfg.meshcore_config.meshcore_rx_delay_base * 1000.0f);
        uint32_t af = static_cast<uint32_t>(cfg.meshcore_config.meshcore_airtime_factor * 1000.0f);
        memcpy(&out[1], &rx, 4);
        memcpy(&out[5], &af, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SET_OTHER_PARAMS && len >= 2)
    {
        manual_add_contacts_ = cmd_frame_[1] != 0;
        if (len >= 3)
        {
            telemetry_mode_base_ = cmd_frame_[2] & 0x03;
            telemetry_mode_loc_ = (cmd_frame_[2] >> 2) & 0x03;
            telemetry_mode_env_ = (cmd_frame_[2] >> 4) & 0x03;
        }
        if (len >= 4)
        {
            advert_loc_policy_ = cmd_frame_[3];
        }
        if (len >= 5)
        {
            cfg.meshcore_config.meshcore_multi_acks = (cmd_frame_[4] != 0);
            multi_acks_ = cmd_frame_[4];
            ctx_.saveConfig();
            ctx_.applyMeshConfig();
        }
        writeOk();
        return;
    }

    if (cmd == CMD_GET_CHANNEL && len >= 2)
    {
        uint8_t channel_idx = cmd_frame_[1];
        if (channel_idx > 1)
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        uint8_t out[kMaxFrameSize] = {};
        int i = 0;
        out[i++] = RESP_CODE_CHANNEL_INFO;
        out[i++] = channel_idx;
        if (channel_idx == 0)
        {
            copyBounded(reinterpret_cast<char*>(&out[i]), 32, cfg.meshcore_config.meshcore_channel_name);
            i += 32;
            memcpy(&out[i], cfg.meshcore_config.primary_key, 16);
            i += 16;
        }
        else
        {
            copyBounded(reinterpret_cast<char*>(&out[i]), 32, "Secondary");
            i += 32;
            memcpy(&out[i], cfg.meshcore_config.secondary_key, 16);
            i += 16;
        }
        enqueueFrame(out, i);
        return;
    }

    if (cmd == CMD_SET_CHANNEL && len >= 2 + 32 + 32)
    {
        writeErr(ERR_CODE_UNSUPPORTED_CMD);
        return;
    }

    if (cmd == CMD_SET_CHANNEL && len >= 2 + 32 + 16)
    {
        uint8_t channel_idx = cmd_frame_[1];
        if (channel_idx > 1)
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        if (channel_idx == 0)
        {
            copyBounded(cfg.meshcore_config.meshcore_channel_name,
                        sizeof(cfg.meshcore_config.meshcore_channel_name),
                        reinterpret_cast<char*>(&cmd_frame_[2]));
            memcpy(cfg.meshcore_config.primary_key, &cmd_frame_[2 + 32], 16);
        }
        else
        {
            memcpy(cfg.meshcore_config.secondary_key, &cmd_frame_[2 + 32], 16);
        }
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        writeOk();
        return;
    }

    if (cmd == CMD_SET_DEVICE_PIN && len >= 5)
    {
        uint32_t pin = readUint32LE(&cmd_frame_[1]);
        if (!isValidBlePin(pin))
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        ble_pin_ = pin;
        saveBlePin();
        writeOk();
        return;
    }

    if (cmd == CMD_EXPORT_PRIVATE_KEY)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t priv[chat::meshcore::MeshCoreIdentity::kPrivKeySize] = {};
        if (!adapter->exportIdentityPrivateKey(priv))
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t out[1 + chat::meshcore::MeshCoreIdentity::kPrivKeySize] = {};
        out[0] = RESP_CODE_PRIVATE_KEY;
        memcpy(&out[1], priv, chat::meshcore::MeshCoreIdentity::kPrivKeySize);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_IMPORT_PRIVATE_KEY && len >= 1 + chat::meshcore::MeshCoreIdentity::kPrivKeySize)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (!adapter->importIdentityPrivateKey(&cmd_frame_[1], chat::meshcore::MeshCoreIdentity::kPrivKeySize))
        {
            writeErr(ERR_CODE_FILE_IO_ERROR);
            return;
        }
        writeOk();
        return;
    }

    if (cmd == CMD_SEND_PATH_DISCOVERY_REQ)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (len < 2 + kPubKeySize || cmd_frame_[1] != 0)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        const uint8_t* pubkey = &cmd_frame_[2];
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (!resolveContactByPubkey(pubkey, &peer, nullptr))
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        adapter->ensurePeerPublicKey(pubkey, kPubKeySize, peer.pubkey_verified);
        uint8_t req_data[9] = {};
        req_data[0] = 0x03;
        req_data[1] = static_cast<uint8_t>(~0x01);
        memset(&req_data[2], 0, 3);
        uint32_t nonce = static_cast<uint32_t>(esp_random());
        memcpy(&req_data[5], &nonce, 4);

        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!adapter->sendPeerRequestPayload(pubkey, kPubKeySize,
                                             req_data, sizeof(req_data),
                                             true,
                                             &tag, &est_timeout, &sent_flood))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        clearPendingRequests();
        pending_discovery_ = tag;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1 : 0;
        memcpy(&out[2], &tag, 4);
        memcpy(&out[6], &est_timeout, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_GET_CONTACT_BY_KEY && len >= 2)
    {
        uint8_t* key = &cmd_frame_[1];
        if (len - 1 >= kPubKeySize)
        {
            if (const ContactRecord* rec = findManualContact(key))
            {
                Frame frame;
                int i = 0;
                frame.buf[i++] = RESP_CODE_CONTACT;
                memcpy(&frame.buf[i], rec->pubkey, kPubKeySize);
                i += kPubKeySize;
                frame.buf[i++] = rec->type;
                frame.buf[i++] = rec->flags;
                frame.buf[i++] = rec->out_path_len;
                memset(&frame.buf[i], 0, kMaxPathSize);
                if (rec->out_path_len > 0)
                {
                    memcpy(&frame.buf[i], rec->out_path, std::min<size_t>(rec->out_path_len, kMaxPathSize));
                }
                i += kMaxPathSize;
                copyBounded(reinterpret_cast<char*>(&frame.buf[i]), 32, rec->name);
                i += 32;
                memcpy(&frame.buf[i], &rec->last_advert, 4);
                i += 4;
                memcpy(&frame.buf[i], &rec->lat, 4);
                i += 4;
                memcpy(&frame.buf[i], &rec->lon, 4);
                i += 4;
                memcpy(&frame.buf[i], &rec->lastmod, 4);
                i += 4;
                frame.len = static_cast<uint8_t>(i);
                enqueueFrame(frame.buf.data(), frame.len);
                return;
            }
        }

        chat::meshcore::MeshCoreAdapter::PeerInfo peer;
        if (lookupPeerByPrefix(key, len - 1, &peer))
        {
            Frame frame;
            if (buildContactFrame(peer, RESP_CODE_CONTACT, frame))
            {
                enqueueFrame(frame.buf.data(), frame.len);
            }
        }
        else if (auto* store = ctx_.getNodeStore())
        {
            for (const auto& entry : store->getEntries())
            {
                if (memcmp(&entry.node_id, key, std::min<size_t>(sizeof(entry.node_id), len - 1)) == 0)
                {
                    Frame frame;
                    if (buildContactFromNode(entry, RESP_CODE_CONTACT, frame))
                    {
                        enqueueFrame(frame.buf.data(), frame.len);
                    }
                    return;
                }
            }
            writeErr(ERR_CODE_NOT_FOUND);
        }
        else
        {
            writeErr(ERR_CODE_NOT_FOUND);
        }
        return;
    }

    if (cmd == CMD_SIGN_START)
    {
        sign_active_ = true;
        sign_data_.clear();
        sign_data_.reserve(8 * 1024);
        uint8_t out[6] = {};
        out[0] = RESP_CODE_SIGN_START;
        out[1] = 0;
        uint32_t max_len = 8 * 1024;
        memcpy(&out[2], &max_len, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SIGN_DATA && len > 1)
    {
        if (!sign_active_)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        size_t chunk = len - 1;
        if (sign_data_.size() + chunk > (8 * 1024))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        sign_data_.insert(sign_data_.end(), &cmd_frame_[1], &cmd_frame_[len]);
        writeOk();
        return;
    }

    if (cmd == CMD_SIGN_FINISH)
    {
        if (!sign_active_)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        sign_active_ = false;
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t sig[chat::meshcore::MeshCoreIdentity::kSignatureSize] = {};
        if (!adapter->signPayload(sign_data_.data(), sign_data_.size(), sig))
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t out[1 + chat::meshcore::MeshCoreIdentity::kSignatureSize] = {};
        out[0] = RESP_CODE_SIGNATURE;
        memcpy(&out[1], sig, chat::meshcore::MeshCoreIdentity::kSignatureSize);
        enqueueFrame(out, sizeof(out));
        sign_data_.clear();
        return;
    }

    if (cmd == CMD_SEND_TELEMETRY_REQ)
    {
        if (len == 4)
        {
            uint8_t out[kMaxFrameSize] = {};
            int i = 0;
            out[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
            out[i++] = 0;
            uint8_t prefix[kPubKeyPrefixSize] = {};
            if (adapter)
            {
                uint8_t pubkey[kPubKeySize] = {};
                if (adapter->exportIdentityPublicKey(pubkey))
                {
                    memcpy(prefix, pubkey, kPubKeyPrefixSize);
                }
            }
            if (prefix[0] == 0)
            {
                uint32_t self_id = ctx_.getSelfNodeId();
                memcpy(prefix, &self_id, std::min(sizeof(self_id), sizeof(prefix)));
            }
            memcpy(&out[i], prefix, kPubKeyPrefixSize);
            i += kPubKeyPrefixSize;

            uint16_t mv = static_cast<uint16_t>(estimateBatteryMv(board.getBatteryLevel()));
            uint16_t lpp_val = static_cast<uint16_t>(mv / 10);
            out[i++] = 1;   // channel
            out[i++] = 116; // LPP_VOLTAGE
            memcpy(&out[i], &lpp_val, 2);
            i += 2;

            enqueueFrame(out, i);
            return;
        }
        if (len >= 4 + kPubKeySize)
        {
            if (!adapter)
            {
                writeErr(ERR_CODE_BAD_STATE);
                return;
            }
            const uint8_t* pubkey = &cmd_frame_[4];
            chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
            if (!resolveContactByPubkey(pubkey, &peer, nullptr))
            {
                writeErr(ERR_CODE_NOT_FOUND);
                return;
            }
            adapter->ensurePeerPublicKey(pubkey, kPubKeySize, peer.pubkey_verified);
            uint32_t tag = 0;
            uint32_t est_timeout = 0;
            bool sent_flood = false;
            if (!adapter->sendPeerRequestType(pubkey, kPubKeySize, 0x03,
                                              &tag, &est_timeout, &sent_flood))
            {
                writeErr(ERR_CODE_TABLE_FULL);
                return;
            }
            clearPendingRequests();
            pending_telemetry_ = tag;
            uint8_t out[10] = {};
            out[0] = RESP_CODE_SENT;
            out[1] = sent_flood ? 1 : 0;
            memcpy(&out[2], &tag, 4);
            memcpy(&out[6], &est_timeout, 4);
            enqueueFrame(out, sizeof(out));
            return;
        }
        writeErr(ERR_CODE_ILLEGAL_ARG);
        return;
    }

    if (cmd == CMD_GET_CUSTOM_VARS)
    {
        uint8_t out = RESP_CODE_CUSTOM_VARS;
        enqueueFrame(&out, 1);
        return;
    }

    if (cmd == CMD_SET_CUSTOM_VAR)
    {
        writeErr(ERR_CODE_UNSUPPORTED_CMD);
        return;
    }

    if (cmd == CMD_SEND_BINARY_REQ && len >= 2 + kPubKeySize)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        const uint8_t* pubkey = &cmd_frame_[1];
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (!resolveContactByPubkey(pubkey, &peer, nullptr))
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }
        adapter->ensurePeerPublicKey(pubkey, kPubKeySize, peer.pubkey_verified);
        const uint8_t* payload = &cmd_frame_[1 + kPubKeySize];
        size_t payload_len = len - (1 + kPubKeySize);
        if (payload_len == 0)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!adapter->sendPeerRequestPayload(pubkey, kPubKeySize,
                                             payload, payload_len,
                                             false,
                                             &tag, &est_timeout, &sent_flood))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        clearPendingRequests();
        pending_req_ = tag;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1 : 0;
        memcpy(&out[2], &tag, 4);
        memcpy(&out[6], &est_timeout, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_GET_ADVERT_PATH && len >= 2 + kPubKeySize)
    {
        const uint8_t* pubkey = &cmd_frame_[2];
        uint8_t peer_hash = pubkey[0];
        uint32_t ts = 0;
        uint8_t path_len = 0;
        uint8_t path[kMaxPathSize] = {};
        bool found = false;

        if (const ContactRecord* rec = findManualContact(pubkey))
        {
            ts = rec->last_advert;
            path_len = rec->out_path_len;
            if (path_len > 0)
            {
                memcpy(path, rec->out_path, std::min<size_t>(path_len, kMaxPathSize));
            }
            found = true;
        }
        else if (adapter)
        {
            chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
            if (adapter->lookupPeerByHash(peer_hash, &peer))
            {
                ts = peer.last_seen_ms / 1000;
                path_len = peer.out_path_len;
                if (path_len > 0)
                {
                    memcpy(path, peer.out_path, std::min<size_t>(path_len, kMaxPathSize));
                }
                found = true;
            }
        }

        if (!found)
        {
            writeErr(ERR_CODE_NOT_FOUND);
            return;
        }

        uint8_t out[kMaxFrameSize] = {};
        int i = 0;
        out[i++] = RESP_CODE_ADVERT_PATH;
        memcpy(&out[i], &ts, 4);
        i += 4;
        out[i++] = path_len;
        memcpy(&out[i], path, path_len);
        i += path_len;
        enqueueFrame(out, i);
        return;
    }

    if (cmd == CMD_GET_STATS && len >= 2)
    {
        uint8_t stats_type = cmd_frame_[1];
        if (stats_type == STATS_TYPE_CORE)
        {
            uint8_t out[16] = {};
            int i = 0;
            out[i++] = RESP_CODE_STATS;
            out[i++] = STATS_TYPE_CORE;
            uint16_t mv = static_cast<uint16_t>(estimateBatteryMv(board.getBatteryLevel()));
            uint32_t uptime = millis() / 1000;
            uint16_t err_flags = 0;
            uint8_t queue_len = static_cast<uint8_t>(outbound_.size());
            memcpy(&out[i], &mv, 2);
            i += 2;
            memcpy(&out[i], &uptime, 4);
            i += 4;
            memcpy(&out[i], &err_flags, 2);
            i += 2;
            out[i++] = queue_len;
            enqueueFrame(out, i);
            return;
        }
        if (stats_type == STATS_TYPE_RADIO)
        {
            uint8_t out[16] = {};
            int i = 0;
            out[i++] = RESP_CODE_STATS;
            out[i++] = STATS_TYPE_RADIO;
            int16_t noise_floor = 0;
            int8_t last_rssi = 0;
            int8_t last_snr = 0;
            uint32_t tx_air = 0;
            uint32_t rx_air = 0;
            memcpy(&out[i], &noise_floor, 2);
            i += 2;
            out[i++] = static_cast<uint8_t>(last_rssi);
            out[i++] = static_cast<uint8_t>(last_snr);
            memcpy(&out[i], &tx_air, 4);
            i += 4;
            memcpy(&out[i], &rx_air, 4);
            i += 4;
            enqueueFrame(out, i);
            return;
        }
        if (stats_type == STATS_TYPE_PACKETS)
        {
            uint8_t out[32] = {};
            int i = 0;
            out[i++] = RESP_CODE_STATS;
            out[i++] = STATS_TYPE_PACKETS;
            uint32_t zeros[6] = {};
            memcpy(&out[i], zeros, sizeof(zeros));
            i += sizeof(zeros);
            enqueueFrame(out, i);
            return;
        }
        writeErr(ERR_CODE_ILLEGAL_ARG);
        return;
    }

    if (cmd == CMD_SEND_RAW_DATA && len >= 6)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        int i = 1;
        int8_t path_len = static_cast<int8_t>(cmd_frame_[i++]);
        if (path_len >= 0 && (i + path_len + 4) <= static_cast<int>(len))
        {
            const uint8_t* path = &cmd_frame_[i];
            i += path_len;
            const uint8_t* payload = &cmd_frame_[i];
            size_t payload_len = len - i;
            uint32_t est_timeout = 0;
            if (!adapter->sendRawData(path, static_cast<size_t>(path_len),
                                      payload, payload_len, &est_timeout))
            {
                writeErr(ERR_CODE_TABLE_FULL);
                return;
            }
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_UNSUPPORTED_CMD);
        }
        return;
    }

    if (cmd == CMD_SEND_TRACE_PATH && len > 10)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint32_t tag = 0;
        uint32_t auth = 0;
        memcpy(&tag, &cmd_frame_[1], 4);
        memcpy(&auth, &cmd_frame_[5], 4);
        const uint8_t flags = cmd_frame_[9];
        const size_t path_len = len - 10;
        const uint8_t path_sz_bits = flags & 0x03;
        const size_t path_stride = static_cast<size_t>(1U << path_sz_bits);
        if (path_stride == 0 || (path_len >> path_sz_bits) > kMaxPathSize || (path_len % path_stride) != 0)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        const uint8_t* path = &cmd_frame_[10];
        uint32_t est_timeout = 0;
        if (!adapter->sendTracePath(path, path_len, tag, auth, flags, &est_timeout))
        {
            writeErr(ERR_CODE_TABLE_FULL);
            return;
        }
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = 0;
        memcpy(&out[2], &tag, 4);
        memcpy(&out[6], &est_timeout, 4);
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame_[1] & 0x80) != 0)
    {
        if (!adapter)
        {
            writeErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (adapter->sendControlData(&cmd_frame_[1], len - 1))
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_TABLE_FULL);
        }
        return;
    }

    if (cmd == CMD_FACTORY_RESET && len >= 6)
    {
        if (memcmp(&cmd_frame_[1], "reset", 5) != 0)
        {
            writeErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        ctx_.resetMeshConfig();
        ctx_.clearNodeDb();
        ctx_.clearMessageDb();
        manual_contacts_.clear();
        saveManualContacts();
        ble_pin_ = 0;
        saveBlePin();
        connections_.clear();
        known_peer_hashes_.clear();
        if (adapter)
        {
            adapter->setFloodScopeKey(nullptr, 0);
        }
        writeOk();
        delay(1000);
        ESP.restart();
        return;
    }

    if (cmd == CMD_SET_FLOOD_SCOPE && len >= 2 && cmd_frame_[1] == 0)
    {
        if (adapter)
        {
            if (len >= 2 + 16)
            {
                adapter->setFloodScopeKey(&cmd_frame_[2], 16);
            }
            else
            {
                adapter->setFloodScopeKey(nullptr, 0);
            }
        }
        writeOk();
        return;
    }

    if (cmd == CMD_HAS_CONNECTION && len >= 1 + kPubKeySize)
    {
        uint32_t prefix4 = 0;
        memcpy(&prefix4, &cmd_frame_[1], sizeof(prefix4));
        const uint32_t now_ms = millis();
        bool found = false;
        for (auto it = connections_.begin(); it != connections_.end();)
        {
            if (it->expires_ms != 0 &&
                static_cast<int32_t>(now_ms - it->expires_ms) >= 0)
            {
                it = connections_.erase(it);
                continue;
            }
            if (it->prefix4 == prefix4)
            {
                found = true;
                break;
            }
            ++it;
        }
        if (found)
        {
            writeOk();
        }
        else
        {
            writeErr(ERR_CODE_NOT_FOUND);
        }
        return;
    }

    if (cmd == CMD_LOGOUT && len >= 1 + kPubKeySize)
    {
        uint32_t prefix4 = 0;
        memcpy(&prefix4, &cmd_frame_[1], sizeof(prefix4));
        for (auto it = connections_.begin(); it != connections_.end(); ++it)
        {
            if (it->prefix4 == prefix4)
            {
                connections_.erase(it);
                break;
            }
        }
        writeOk();
        return;
    }

    writeErr(ERR_CODE_UNSUPPORTED_CMD);
}

void MeshCoreBleService::enqueueFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > kMaxFrameSize)
    {
        return;
    }
    Frame frame;
    frame.len = static_cast<uint8_t>(len);
    memcpy(frame.buf.data(), data, len);
    outbound_.push_back(frame);
}

void MeshCoreBleService::enqueueOffline(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > kMaxFrameSize)
    {
        return;
    }
    Frame frame;
    frame.len = static_cast<uint8_t>(len);
    memcpy(frame.buf.data(), data, len);
    offline_queue_.push_back(frame);
}

void MeshCoreBleService::sendPendingFrames()
{
    if (!connected_ || !tx_char_ || outbound_.empty())
    {
        return;
    }
    uint32_t now = millis();
    if (now - last_write_ms_ < kBleWriteMinIntervalMs)
    {
        return;
    }

    Frame frame = outbound_.front();
    outbound_.pop_front();
    tx_char_->setValue(frame.buf.data(), frame.len);
    tx_char_->notify();
    last_write_ms_ = now;
}

void MeshCoreBleService::sendOfflineTickle()
{
    if (!connected_)
    {
        return;
    }
    uint8_t code = PUSH_CODE_MSG_WAITING;
    enqueueFrame(&code, 1);
}

void MeshCoreBleService::loadBlePin()
{
    Preferences prefs;
    if (prefs.begin("mc_ble", true))
    {
        ble_pin_ = prefs.getUInt("pin", 0);
        prefs.end();
    }
}

void MeshCoreBleService::saveBlePin()
{
    Preferences prefs;
    if (prefs.begin("mc_ble", false))
    {
        prefs.putUInt("pin", ble_pin_);
        prefs.end();
    }
}

void MeshCoreBleService::loadManualContacts()
{
    manual_contacts_.clear();
    Preferences prefs;
    if (!prefs.begin("mc_contacts", true))
    {
        return;
    }
    uint8_t ver = prefs.getUChar("ver", 0);
    if (ver != 1)
    {
        prefs.end();
        return;
    }
    size_t len = prefs.getBytesLength("blob");
    if (len == 0 || (len % sizeof(ContactRecord)) != 0)
    {
        prefs.end();
        return;
    }
    size_t count = len / sizeof(ContactRecord);
    manual_contacts_.resize(count);
    prefs.getBytes("blob", manual_contacts_.data(), len);
    prefs.end();
}

void MeshCoreBleService::saveManualContacts()
{
    Preferences prefs;
    if (!prefs.begin("mc_contacts", false))
    {
        return;
    }
    prefs.putUChar("ver", 1);
    if (manual_contacts_.empty())
    {
        prefs.remove("blob");
    }
    else
    {
        prefs.putBytes("blob", manual_contacts_.data(),
                       manual_contacts_.size() * sizeof(ContactRecord));
    }
    prefs.end();
}

MeshCoreBleService::ContactRecord* MeshCoreBleService::findManualContact(const uint8_t* pubkey)
{
    if (!pubkey)
    {
        return nullptr;
    }
    for (auto& entry : manual_contacts_)
    {
        if (memcmp(entry.pubkey, pubkey, kPubKeySize) == 0)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MeshCoreBleService::ContactRecord* MeshCoreBleService::findManualContactByPrefix(const uint8_t* prefix,
                                                                                       size_t len) const
{
    if (!prefix || len == 0)
    {
        return nullptr;
    }
    size_t cmp_len = std::min(len, static_cast<size_t>(kPubKeyPrefixSize));
    for (const auto& entry : manual_contacts_)
    {
        if (memcmp(entry.pubkey, prefix, cmp_len) == 0)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool MeshCoreBleService::resolveContactByPubkey(const uint8_t* pubkey,
                                                chat::meshcore::MeshCoreAdapter::PeerInfo* out_peer,
                                                const ContactRecord** out_manual) const
{
    if (out_peer)
    {
        *out_peer = {};
    }
    if (out_manual)
    {
        *out_manual = nullptr;
    }
    if (!pubkey)
    {
        return false;
    }
    for (const auto& entry : manual_contacts_)
    {
        if (memcmp(entry.pubkey, pubkey, kPubKeySize) == 0)
        {
            if (out_manual)
            {
                *out_manual = &entry;
            }
            return true;
        }
    }
    const auto* adapter = static_cast<const chat::meshcore::MeshCoreAdapter*>(ctx_.getMeshAdapter());
    if (adapter)
    {
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (adapter->lookupPeerByHash(pubkey[0], &peer) &&
            peer.has_pubkey &&
            memcmp(peer.pubkey, pubkey, kPubKeySize) == 0)
        {
            if (out_peer)
            {
                *out_peer = peer;
            }
            return true;
        }
    }
    return false;
}

void MeshCoreBleService::upsertManualContact(const ContactRecord& record)
{
    for (auto& entry : manual_contacts_)
    {
        if (memcmp(entry.pubkey, record.pubkey, kPubKeySize) == 0)
        {
            entry = record;
            return;
        }
    }
    manual_contacts_.push_back(record);
}

bool MeshCoreBleService::removeManualContact(const uint8_t* pubkey)
{
    if (!pubkey)
    {
        return false;
    }
    for (auto it = manual_contacts_.begin(); it != manual_contacts_.end(); ++it)
    {
        if (memcmp(it->pubkey, pubkey, kPubKeySize) == 0)
        {
            manual_contacts_.erase(it);
            return true;
        }
    }
    return false;
}

void MeshCoreBleService::clearPendingRequests()
{
    pending_login_ = 0;
    pending_status_ = 0;
    pending_status_tag_ = 0;
    pending_telemetry_ = 0;
    pending_req_ = 0;
    pending_discovery_ = 0;
}

void MeshCoreBleService::buildContactsSnapshot(uint32_t filter_since)
{
    contacts_frames_.clear();
    contacts_most_recent_ = 0;

    auto add_contact_frame = [&](const ContactRecord& record, uint8_t code) {
        Frame frame;
        int i = 0;
        frame.buf[i++] = code;
        memcpy(&frame.buf[i], record.pubkey, kPubKeySize);
        i += kPubKeySize;
        frame.buf[i++] = record.type;
        frame.buf[i++] = record.flags;
        frame.buf[i++] = record.out_path_len;
        memset(&frame.buf[i], 0, kMaxPathSize);
        if (record.out_path_len > 0)
        {
            size_t copy_len = std::min<size_t>(record.out_path_len, kMaxPathSize);
            memcpy(&frame.buf[i], record.out_path, copy_len);
        }
        i += kMaxPathSize;
        copyBounded(reinterpret_cast<char*>(&frame.buf[i]), 32, record.name);
        i += 32;
        memcpy(&frame.buf[i], &record.last_advert, 4);
        i += 4;
        memcpy(&frame.buf[i], &record.lat, 4);
        i += 4;
        memcpy(&frame.buf[i], &record.lon, 4);
        i += 4;
        memcpy(&frame.buf[i], &record.lastmod, 4);
        i += 4;
        frame.len = static_cast<uint8_t>(i);
        contacts_frames_.push_back(frame);
        contacts_most_recent_ = std::max(contacts_most_recent_, record.lastmod);
    };

    for (const auto& record : manual_contacts_)
    {
        if (filter_since != 0 && record.lastmod <= filter_since)
        {
            continue;
        }
        add_contact_frame(record, RESP_CODE_CONTACT);
    }

    if (auto* adapter = static_cast<chat::meshcore::MeshCoreAdapter*>(ctx_.getMeshAdapter()))
    {
        std::vector<chat::meshcore::MeshCoreAdapter::PeerInfo> peers;
        adapter->getPeerInfos(peers);
        for (const auto& peer : peers)
        {
            uint32_t lastmod = peer.last_seen_ms / 1000;
            if (filter_since != 0 && lastmod <= filter_since)
            {
                continue;
            }
            Frame frame;
            if (buildContactFrame(peer, RESP_CODE_CONTACT, frame))
            {
                contacts_frames_.push_back(frame);
                contacts_most_recent_ = std::max(contacts_most_recent_, lastmod);
            }
        }
    }

    if (auto* store = ctx_.getNodeStore())
    {
        for (const auto& entry : store->getEntries())
        {
            uint32_t lastmod = entry.last_seen;
            if (filter_since != 0 && lastmod <= filter_since)
            {
                continue;
            }
            Frame frame;
            if (buildContactFromNode(entry, RESP_CODE_CONTACT, frame))
            {
                contacts_frames_.push_back(frame);
                contacts_most_recent_ = std::max(contacts_most_recent_, lastmod);
            }
        }
    }
}

bool MeshCoreBleService::decodeContactPayload(const uint8_t* frame, size_t len,
                                              ContactRecord* out, uint32_t* out_lastmod)
{
    if (!frame || !out || len < (1 + kPubKeySize + 1 + 1 + 1 + kMaxPathSize + 32 + 4))
    {
        return false;
    }
    size_t i = 1;
    memcpy(out->pubkey, &frame[i], kPubKeySize);
    i += kPubKeySize;
    out->type = frame[i++];
    out->flags = frame[i++];
    out->out_path_len = frame[i++];
    memset(out->out_path, 0, sizeof(out->out_path));
    if (out->out_path_len > 0)
    {
        size_t copy_len = std::min<size_t>(out->out_path_len, kMaxPathSize);
        memcpy(out->out_path, &frame[i], copy_len);
    }
    i += kMaxPathSize;
    memcpy(out->name, &frame[i], sizeof(out->name));
    out->name[sizeof(out->name) - 1] = '\0';
    i += 32;
    memcpy(&out->last_advert, &frame[i], 4);
    i += 4;

    out->lat = 0;
    out->lon = 0;
    uint32_t lastmod = 0;
    if (len >= i + 8)
    {
        memcpy(&out->lat, &frame[i], 4);
        i += 4;
        memcpy(&out->lon, &frame[i], 4);
        i += 4;
        if (len >= i + 4)
        {
            memcpy(&lastmod, &frame[i], 4);
        }
    }

    if (out_lastmod)
    {
        *out_lastmod = lastmod;
    }
    return true;
}

bool MeshCoreBleService::buildContactFrame(const chat::meshcore::MeshCoreAdapter::PeerInfo& peer,
                                           uint8_t code, Frame& out)
{
    int i = 0;
    out.buf[i++] = code;
    uint8_t pubkey[kPubKeySize] = {};
    if (peer.has_pubkey)
    {
        memcpy(pubkey, peer.pubkey, kPubKeySize);
    }
    else
    {
        memcpy(pubkey, &peer.node_id, std::min(sizeof(peer.node_id), sizeof(pubkey)));
    }
    memcpy(&out.buf[i], pubkey, kPubKeySize);
    i += kPubKeySize;
    out.buf[i++] = ADV_TYPE_CHAT;
    out.buf[i++] = 0;
    out.buf[i++] = peer.out_path_len;
    memset(&out.buf[i], 0, kMaxPathSize);
    if (peer.out_path_len > 0)
    {
        memcpy(&out.buf[i], peer.out_path, std::min<size_t>(peer.out_path_len, kMaxPathSize));
    }
    i += kMaxPathSize;

    char name[32] = {};
    const auto* store = ctx_.getNodeStore();
    if (store)
    {
        for (const auto& entry : store->getEntries())
        {
            if (entry.node_id == peer.node_id)
            {
                copyBounded(name, sizeof(name), entry.long_name[0] != '\0' ? entry.long_name : entry.short_name);
                break;
            }
        }
    }
    if (name[0] == '\0')
    {
        snprintf(name, sizeof(name), "%02X%02X", pubkey[0], pubkey[1]);
    }
    copyBounded(reinterpret_cast<char*>(&out.buf[i]), 32, name);
    i += 32;

    uint32_t last_adv = peer.last_seen_ms / 1000;
    memcpy(&out.buf[i], &last_adv, 4);
    i += 4;
    int32_t lat = 0;
    int32_t lon = 0;
    memcpy(&out.buf[i], &lat, 4);
    i += 4;
    memcpy(&out.buf[i], &lon, 4);
    i += 4;
    memcpy(&out.buf[i], &last_adv, 4);
    i += 4;

    out.len = static_cast<uint8_t>(i);
    return true;
}

bool MeshCoreBleService::buildContactFromNode(const chat::contacts::NodeEntry& entry,
                                              uint8_t code, Frame& out)
{
    int i = 0;
    out.buf[i++] = code;
    uint8_t pubkey[kPubKeySize] = {};
    memcpy(pubkey, &entry.node_id, std::min(sizeof(entry.node_id), sizeof(pubkey)));
    memcpy(&out.buf[i], pubkey, kPubKeySize);
    i += kPubKeySize;
    out.buf[i++] = ADV_TYPE_CHAT;
    out.buf[i++] = 0;
    out.buf[i++] = 0;
    memset(&out.buf[i], 0, kMaxPathSize);
    i += kMaxPathSize;

    char name[32] = {};
    copyBounded(name, sizeof(name), entry.long_name[0] != '\0' ? entry.long_name : entry.short_name);
    if (name[0] == '\0')
    {
        snprintf(name, sizeof(name), "%08lX", static_cast<unsigned long>(entry.node_id));
    }
    copyBounded(reinterpret_cast<char*>(&out.buf[i]), 32, name);
    i += 32;

    uint32_t last_adv = entry.last_seen;
    memcpy(&out.buf[i], &last_adv, 4);
    i += 4;
    int32_t lat = 0;
    int32_t lon = 0;
    memcpy(&out.buf[i], &lat, 4);
    i += 4;
    memcpy(&out.buf[i], &lon, 4);
    i += 4;
    memcpy(&out.buf[i], &last_adv, 4);
    i += 4;

    out.len = static_cast<uint8_t>(i);
    return true;
}

bool MeshCoreBleService::lookupPeerByPrefix(const uint8_t* prefix, size_t len,
                                            chat::meshcore::MeshCoreAdapter::PeerInfo* out) const
{
    if (!prefix || len == 0 || !out)
    {
        return false;
    }
    const auto* adapter = static_cast<const chat::meshcore::MeshCoreAdapter*>(ctx_.getMeshAdapter());
    if (!adapter)
    {
        return false;
    }

    std::vector<chat::meshcore::MeshCoreAdapter::PeerInfo> peers;
    adapter->getPeerInfos(peers);
    for (const auto& peer : peers)
    {
        if (!peer.has_pubkey)
        {
            continue;
        }
        size_t cmp_len = std::min(len, static_cast<size_t>(kPubKeyPrefixSize));
        if (memcmp(peer.pubkey, prefix, cmp_len) == 0)
        {
            *out = peer;
            return true;
        }
    }
    return false;
}

uint32_t MeshCoreBleService::deriveNodeIdFromPubkey(const uint8_t* pubkey, size_t len) const
{
    if (!pubkey || len == 0)
    {
        return 0;
    }
    uint32_t node = 0;
    if (len >= sizeof(node))
    {
        memcpy(&node, pubkey, sizeof(node));
        node = (node & 0xFFFFFF00UL) | static_cast<uint32_t>(pubkey[0]);
    }
    if (node == 0)
    {
        node = 0x42000000UL | static_cast<uint32_t>(pubkey[0]);
    }
    return node;
}

} // namespace ble
