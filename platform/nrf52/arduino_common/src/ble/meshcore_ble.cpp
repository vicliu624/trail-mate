#include "../../include/ble/meshcore_ble.h"

#include "app/app_config.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "ble/ble_uuids.h"
#include "chat/ports/i_node_store.h"
#include "platform/ui/device_runtime.h"
#include "platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_adapter_lite.h"
#include "sys/clock.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cstring>

namespace ble
{
namespace
{

constexpr uint8_t CMD_APP_START = 1;
constexpr uint8_t CMD_SEND_TXT_MSG = 2;
constexpr uint8_t CMD_SEND_CHANNEL_TXT_MSG = 3;
constexpr uint8_t CMD_GET_CONTACTS = 4;
constexpr uint8_t CMD_GET_DEVICE_TIME = 5;
constexpr uint8_t CMD_SET_DEVICE_TIME = 6;
constexpr uint8_t CMD_SEND_SELF_ADVERT = 7;
constexpr uint8_t CMD_SET_ADVERT_NAME = 8;
constexpr uint8_t CMD_SET_RADIO_PARAMS = 11;
constexpr uint8_t CMD_SET_RADIO_TX_POWER = 12;
constexpr uint8_t CMD_RESET_PATH = 13;
constexpr uint8_t CMD_GET_BATT_AND_STORAGE = 20;
constexpr uint8_t CMD_SEND_RAW_DATA = 25;
constexpr uint8_t CMD_SEND_LOGIN = 26;
constexpr uint8_t CMD_SEND_STATUS_REQ = 27;
constexpr uint8_t CMD_HAS_CONNECTION = 28;
constexpr uint8_t CMD_LOGOUT = 29;
constexpr uint8_t CMD_GET_CONTACT_BY_KEY = 30;
constexpr uint8_t CMD_GET_CHANNEL = 31;
constexpr uint8_t CMD_SET_CHANNEL = 32;
constexpr uint8_t CMD_SIGN_START = 33;
constexpr uint8_t CMD_SIGN_DATA = 34;
constexpr uint8_t CMD_SIGN_FINISH = 35;
constexpr uint8_t CMD_SEND_TRACE_PATH = 36;
constexpr uint8_t CMD_SEND_BINARY_REQ = 50;
constexpr uint8_t CMD_EXPORT_PRIVATE_KEY = 23;
constexpr uint8_t CMD_IMPORT_PRIVATE_KEY = 24;
constexpr uint8_t CMD_REBOOT = 19;
constexpr uint8_t CMD_FACTORY_RESET = 51;
constexpr uint8_t CMD_SEND_PATH_DISCOVERY_REQ = 52;
constexpr uint8_t CMD_SET_FLOOD_SCOPE = 54;
constexpr uint8_t CMD_SEND_CONTROL_DATA = 55;
constexpr uint8_t CMD_GET_STATS = 56;
constexpr uint8_t CMD_SEND_TELEMETRY_REQ = 39;
constexpr uint8_t CMD_GET_ADVERT_PATH = 42;
constexpr uint8_t CMD_DEVICE_QEURY = 22;

constexpr uint8_t RESP_CODE_OK = 0;
constexpr uint8_t RESP_CODE_ERR = 1;
constexpr uint8_t RESP_CODE_CONTACTS_START = 2;
constexpr uint8_t RESP_CODE_CONTACT = 3;
constexpr uint8_t RESP_CODE_END_OF_CONTACTS = 4;
constexpr uint8_t RESP_CODE_SELF_INFO = 5;
constexpr uint8_t RESP_CODE_SENT = 6;
constexpr uint8_t RESP_CODE_CONTACT_MSG_RECV = 7;
constexpr uint8_t RESP_CODE_CHANNEL_MSG_RECV = 8;
constexpr uint8_t RESP_CODE_CURR_TIME = 9;
constexpr uint8_t RESP_CODE_DEVICE_INFO = 13;
constexpr uint8_t RESP_CODE_PRIVATE_KEY = 14;
constexpr uint8_t RESP_CODE_CHANNEL_INFO = 18;
constexpr uint8_t RESP_CODE_SIGN_START = 19;
constexpr uint8_t RESP_CODE_SIGNATURE = 20;
constexpr uint8_t RESP_CODE_ADVERT_PATH = 22;
constexpr uint8_t RESP_CODE_STATS = 24;
constexpr uint8_t RESP_CODE_BATT_AND_STORAGE = 25;

constexpr uint8_t PUSH_CODE_RAW_DATA = 0x84;
constexpr uint8_t PUSH_CODE_TELEMETRY_RESPONSE = 0x8B;

constexpr uint8_t ERR_CODE_UNSUPPORTED_CMD = 1;
constexpr uint8_t ERR_CODE_NOT_FOUND = 2;
constexpr uint8_t ERR_CODE_TABLE_FULL = 3;
constexpr uint8_t ERR_CODE_BAD_STATE = 4;
constexpr uint8_t ERR_CODE_ILLEGAL_ARG = 6;

constexpr uint8_t ADV_TYPE_CHAT = 1;
constexpr uint8_t TXT_TYPE_PLAIN = 0;
constexpr uint8_t TXT_TYPE_CLI_DATA = 1;
constexpr uint8_t kCompatFirmwareVerCode = 8;
constexpr uint8_t kCompatMaxContactsDiv2 = 50;
constexpr uint8_t kCompatMaxGroupChannels = 1;
constexpr const char* kCompatFirmwareVersion = "v1.11.0";
constexpr size_t kPrefixSize = 6;
constexpr size_t kMaxFrameSize = 172;
constexpr size_t kPubKeySize = chat::meshcore::kMeshCorePubKeySize;
constexpr size_t kMaxPathSize = 64;
constexpr uint8_t STATS_TYPE_CORE = 0;
constexpr uint8_t STATS_TYPE_RADIO = 1;
constexpr uint8_t STATS_TYPE_PACKETS = 2;

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
    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

MeshCoreBleService* s_active_service = nullptr;

uint32_t nowSeconds()
{
    return sys::epoch_seconds_now();
}

void prepareBluefruit(const std::string& device_name)
{
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.setName(device_name.c_str());
}

void startAdvertising(::BLEService& service)
{
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(service);
    Bluefruit.ScanResponse.addName();
    Bluefruit.ScanResponse.addTxPower();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 668);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
}

void authorizeRead(uint16_t conn_handle)
{
    ble_gatts_rw_authorize_reply_params_t reply = {.type = BLE_GATTS_AUTHORIZE_TYPE_READ};
    reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
    sd_ble_gatts_rw_authorize_reply(conn_handle, &reply);
}

void onRxWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len)
{
    if (!s_active_service || !data || len == 0)
    {
        return;
    }
    (void)s_active_service->handleRxFrame(data, len);
}

void onTxAuthorize(uint16_t conn_handle, BLECharacteristic* chr, ble_gatts_evt_read_t* request)
{
    if (!chr || !request)
    {
        authorizeRead(conn_handle);
        return;
    }

    if (request->offset == 0)
    {
        uint8_t out[kMaxFrameSize] = {};
        size_t out_len = 0;
        if (s_active_service && s_active_service->popTxFrame(out, &out_len))
        {
            chr->write(out, out_len);
        }
        else
        {
            uint8_t empty = 0;
            chr->write(&empty, 0);
        }
    }

    authorizeRead(conn_handle);
}

platform::nrf52::arduino_common::chat::meshcore::MeshCoreAdapterLite* resolveMeshCoreAdapter(
    app::IAppBleFacade& ctx)
{
    auto* adapter = ctx.getMeshAdapter();
    if (!adapter || ctx.getConfig().mesh_protocol != chat::MeshProtocol::MeshCore)
    {
        return nullptr;
    }

    if (auto* backend = adapter->backendForProtocol(chat::MeshProtocol::MeshCore))
    {
        return static_cast<platform::nrf52::arduino_common::chat::meshcore::MeshCoreAdapterLite*>(backend);
    }

    return static_cast<platform::nrf52::arduino_common::chat::meshcore::MeshCoreAdapterLite*>(adapter);
}

} // namespace

MeshCoreBleService::MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name),
      service_(::BLEUuid(NUS_SERVICE_UUID)),
      rx_char_(::BLEUuid(NUS_CHAR_RX_UUID)),
      tx_char_(::BLEUuid(NUS_CHAR_TX_UUID))
{
}

MeshCoreBleService::~MeshCoreBleService()
{
    stop();
}

void MeshCoreBleService::start()
{
    s_active_service = this;
    prepareBluefruit(device_name_);

    service_.begin();

    rx_char_.setProperties(CHR_PROPS_WRITE);
    rx_char_.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    rx_char_.setFixedLen(0);
    rx_char_.setMaxLen(kMaxFrameSize);
    rx_char_.setWriteCallback(onRxWrite, false);
    rx_char_.begin();

    tx_char_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    tx_char_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    tx_char_.setFixedLen(0);
    tx_char_.setMaxLen(kMaxFrameSize);
    tx_char_.setReadAuthorizeCallback(onTxAuthorize, false);
    tx_char_.begin();

    ctx_.getChatService().addIncomingTextObserver(this);
    startAdvertising(service_);
    active_ = true;
}

void MeshCoreBleService::stop()
{
    ctx_.getChatService().removeIncomingTextObserver(this);
    Bluefruit.Advertising.stop();
    tx_queue_.clear();
    active_ = false;
    if (s_active_service == this)
    {
        s_active_service = nullptr;
    }
}

void MeshCoreBleService::update()
{
    if (!active_)
    {
        return;
    }

    pumpIncomingAppData();
    sendPendingNotifications();

    if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning())
    {
        Bluefruit.Advertising.start(0);
    }
}

void MeshCoreBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    ++stats_rx_packets_;
    if (msg.rx_meta.direct)
    {
        ++stats_rx_direct_;
    }
    else
    {
        ++stats_rx_flood_;
    }

    Frame frame{};
    size_t index = 0;
    if (msg.to == 0xFFFFFFFFUL || msg.to == 0)
    {
        frame.buf[index++] = RESP_CODE_CHANNEL_MSG_RECV;
        frame.buf[index++] = static_cast<uint8_t>(msg.channel);
        frame.buf[index++] = msg.rx_meta.direct ? 0xFFU : msg.rx_meta.hop_count;
    }
    else
    {
        frame.buf[index++] = RESP_CODE_CONTACT_MSG_RECV;
        std::memcpy(&frame.buf[index], &msg.from, std::min(sizeof(msg.from), kPrefixSize));
        index += kPrefixSize;
        frame.buf[index++] = msg.rx_meta.direct ? 0xFFU : msg.rx_meta.hop_count;
    }

    frame.buf[index++] = TXT_TYPE_PLAIN;
    std::memcpy(&frame.buf[index], &msg.timestamp, sizeof(msg.timestamp));
    index += sizeof(msg.timestamp);

    const size_t text_len = std::min(msg.text.size(), frame.buf.size() - index);
    if (text_len > 0)
    {
        std::memcpy(&frame.buf[index], msg.text.data(), text_len);
        index += text_len;
    }
    frame.len = index;
    tx_queue_.push_back(frame);
}

bool MeshCoreBleService::handleRxFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > kMaxFrameSize)
    {
        return false;
    }
    handleCmdFrame(data, len);
    return true;
}

bool MeshCoreBleService::popTxFrame(uint8_t* out, size_t* out_len)
{
    if (!out || !out_len || tx_queue_.empty())
    {
        return false;
    }
    const Frame frame = tx_queue_.front();
    tx_queue_.pop_front();
    std::memcpy(out, frame.buf.data(), frame.len);
    *out_len = frame.len;
    return true;
}

void MeshCoreBleService::pumpIncomingAppData()
{
    chat::IMeshAdapter* adapter = ctx_.getMeshAdapter();
    if (!adapter)
    {
        return;
    }

    for (uint8_t count = 0; count < 4; ++count)
    {
        chat::MeshIncomingData msg{};
        if (!adapter->pollIncomingData(&msg))
        {
            break;
        }
        enqueueRawDataPush(msg);
    }
}

void MeshCoreBleService::handleCmdFrame(const uint8_t* data, size_t len)
{
    const uint8_t cmd = data[0];
    chat::IMeshAdapter* adapter = ctx_.getMeshAdapter();
    auto& cfg = ctx_.getConfig();
    auto* lite = resolveMeshCoreAdapter(ctx_);

    if (cmd == CMD_DEVICE_QEURY && len >= 2)
    {
        uint8_t out[kMaxFrameSize] = {};
        size_t index = 0;
        out[index++] = RESP_CODE_DEVICE_INFO;
        out[index++] = kCompatFirmwareVerCode;
        out[index++] = kCompatMaxContactsDiv2;
        out[index++] = kCompatMaxGroupChannels;
        const uint32_t ble_pin = 0;
        std::memcpy(&out[index], &ble_pin, sizeof(ble_pin));
        index += sizeof(ble_pin);
        std::memset(&out[index], 0, 12);
        std::strncpy(reinterpret_cast<char*>(&out[index]), __DATE__, 11);
        index += 12;
        std::strncpy(reinterpret_cast<char*>(&out[index]), device_name_.c_str(), 40);
        index += 40;
        std::strncpy(reinterpret_cast<char*>(&out[index]), kCompatFirmwareVersion, 20);
        index += 20;
        enqueueFrame(out, index);
        return;
    }

    if (cmd == CMD_APP_START && len >= 8)
    {
        uint8_t out[kMaxFrameSize] = {};
        size_t index = 0;
        out[index++] = RESP_CODE_SELF_INFO;
        out[index++] = ADV_TYPE_CHAT;
        out[index++] = static_cast<uint8_t>(cfg.meshcore_config.tx_power);
        out[index++] = static_cast<uint8_t>(app::AppConfig::kTxPowerMaxDbm);

        uint8_t pubkey[chat::meshcore::kMeshCorePubKeySize] = {};
        if (lite)
        {
            lite->exportIdentityPublicKey(pubkey, sizeof(pubkey));
        }
        std::memcpy(&out[index], pubkey, sizeof(pubkey));
        index += sizeof(pubkey);

        const int32_t lat = 0;
        const int32_t lon = 0;
        std::memcpy(&out[index], &lat, sizeof(lat));
        index += sizeof(lat);
        std::memcpy(&out[index], &lon, sizeof(lon));
        index += sizeof(lon);

        out[index++] = cfg.meshcore_config.meshcore_multi_acks ? 1U : 0U;
        out[index++] = 0;
        out[index++] = 0;

        const uint32_t freq = static_cast<uint32_t>(cfg.meshcore_config.meshcore_freq_mhz * 1000.0f);
        const uint32_t bw = static_cast<uint32_t>(cfg.meshcore_config.meshcore_bw_khz * 1000.0f);
        std::memcpy(&out[index], &freq, sizeof(freq));
        index += sizeof(freq);
        std::memcpy(&out[index], &bw, sizeof(bw));
        index += sizeof(bw);
        out[index++] = cfg.meshcore_config.meshcore_sf;
        out[index++] = cfg.meshcore_config.meshcore_cr;

        const size_t name_len = strnlen(cfg.node_name, sizeof(cfg.node_name));
        const size_t copy_len = std::min(name_len, static_cast<size_t>(kMaxFrameSize - index));
        if (copy_len > 0)
        {
            std::memcpy(&out[index], cfg.node_name, copy_len);
            index += copy_len;
        }
        enqueueFrame(out, index);
        return;
    }

    if (cmd == CMD_GET_CONTACTS)
    {
        uint32_t filter_since = 0;
        if (len >= 5)
        {
            std::memcpy(&filter_since, &data[1], sizeof(filter_since));
        }

        uint32_t total = 0;
        if (const auto* store = ctx_.getNodeStore())
        {
            for (const auto& entry : store->getEntries())
            {
                if (entry.node_id == 0)
                {
                    continue;
                }
                if (filter_since != 0 && entry.last_seen <= filter_since)
                {
                    continue;
                }
                ++total;
            }
        }

        uint8_t start_buf[5] = {RESP_CODE_CONTACTS_START, 0, 0, 0, 0};
        std::memcpy(&start_buf[1], &total, sizeof(total));
        enqueueFrame(start_buf, sizeof(start_buf));

        uint32_t most_recent = 0;
        if (const auto* store = ctx_.getNodeStore())
        {
            for (const auto& entry : store->getEntries())
            {
                if (entry.node_id == 0)
                {
                    continue;
                }
                if (filter_since != 0 && entry.last_seen <= filter_since)
                {
                    continue;
                }
                Frame frame{};
                if (buildContactFromNode(entry, RESP_CODE_CONTACT, frame))
                {
                    enqueueFrame(frame.buf.data(), frame.len);
                    most_recent = std::max(most_recent, entry.last_seen);
                }
            }
        }

        uint8_t end_buf[5] = {RESP_CODE_END_OF_CONTACTS, 0, 0, 0, 0};
        std::memcpy(&end_buf[1], &most_recent, sizeof(most_recent));
        enqueueFrame(end_buf, sizeof(end_buf));
        return;
    }

    if (cmd == CMD_SEND_TXT_MSG && len >= 14)
    {
        if (!adapter)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        size_t index = 1;
        const uint8_t txt_type = data[index++];
        index++;
        index += 4;
        const uint8_t* prefix = &data[index];
        index += kPrefixSize;
        if (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_CLI_DATA)
        {
            enqueueErr(ERR_CODE_UNSUPPORTED_CMD);
            return;
        }

        const uint32_t dest = resolveNodeIdFromPrefix(prefix, kPrefixSize);
        if (dest == 0)
        {
            enqueueErr(ERR_CODE_NOT_FOUND);
            return;
        }

        const std::string text(reinterpret_cast<const char*>(&data[index]), len - index);
        chat::MessageId msg_id = 0;
        if (!adapter->sendText(chat::ChannelId::PRIMARY, text, &msg_id, dest))
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }

        ++stats_tx_packets_;
        ++stats_tx_direct_;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = 0;
        const uint32_t ack = msg_id;
        const uint32_t timeout = 0;
        std::memcpy(&out[2], &ack, sizeof(ack));
        std::memcpy(&out[6], &timeout, sizeof(timeout));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_CHANNEL_TXT_MSG && len >= 7)
    {
        if (!adapter)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        size_t index = 1;
        const uint8_t txt_type = data[index++];
        const uint8_t channel_idx = data[index++];
        index += 4;
        if (txt_type != TXT_TYPE_PLAIN)
        {
            enqueueErr(ERR_CODE_UNSUPPORTED_CMD);
            return;
        }
        const std::string text(reinterpret_cast<const char*>(&data[index]), len - index);
        chat::MessageId msg_id = 0;
        const chat::ChannelId channel = (channel_idx == 1U) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;
        if (!adapter->sendText(channel, text, &msg_id, 0))
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        ++stats_tx_packets_;
        ++stats_tx_flood_;
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SEND_LOGIN && len >= 1 + kPubKeySize)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        const uint8_t* pubkey = &data[1];
        const size_t pass_len = (len > (1 + kPubKeySize)) ? (len - (1 + kPubKeySize)) : 0;
        uint8_t payload[24] = {};
        size_t payload_len = 0;
        const uint32_t now_ts = nowSeconds();
        std::memcpy(&payload[payload_len], &now_ts, sizeof(now_ts));
        payload_len += sizeof(now_ts);
        if (pass_len > 0)
        {
            const size_t copy_len = std::min(pass_len, sizeof(payload) - payload_len);
            std::memcpy(&payload[payload_len], &data[1 + kPubKeySize], copy_len);
            payload_len += copy_len;
        }

        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!lite->sendAnonRequestPayload(pubkey,
                                          kPubKeySize,
                                          payload,
                                          payload_len,
                                          &est_timeout,
                                          &sent_flood))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }

        ++stats_tx_packets_;
        ++stats_tx_flood_;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1U : 0U;
        std::memcpy(&out[2], pubkey, sizeof(uint32_t));
        std::memcpy(&out[6], &est_timeout, sizeof(est_timeout));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_STATUS_REQ && len >= 1 + kPubKeySize)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        const uint8_t* pubkey = &data[1];
        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!lite->sendPeerRequestType(pubkey,
                                       kPubKeySize,
                                       0x01,
                                       &tag,
                                       &est_timeout,
                                       &sent_flood))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }

        ++stats_tx_packets_;
        ++stats_tx_flood_;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1U : 0U;
        std::memcpy(&out[2], &tag, sizeof(tag));
        std::memcpy(&out[6], &est_timeout, sizeof(est_timeout));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SET_ADVERT_NAME && len >= 2)
    {
        const size_t nlen = std::min(len - 1, sizeof(cfg.node_name) - 1);
        std::memcpy(cfg.node_name, &data[1], nlen);
        cfg.node_name[nlen] = '\0';
        ctx_.saveConfig();
        ctx_.applyUserInfo();
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SEND_SELF_ADVERT)
    {
        const bool broadcast = (len >= 2 && data[1] == 1);
        if (lite && lite->sendSelfAdvert(broadcast))
        {
            ++stats_tx_packets_;
            if (broadcast)
            {
                ++stats_tx_flood_;
            }
            else
            {
                ++stats_tx_direct_;
            }
            enqueueSentOk();
        }
        else
        {
            enqueueErr(ERR_CODE_BAD_STATE);
        }
        return;
    }

    if (cmd == CMD_GET_DEVICE_TIME)
    {
        uint8_t out[5] = {};
        out[0] = RESP_CODE_CURR_TIME;
        const uint32_t now = nowSeconds();
        std::memcpy(&out[1], &now, sizeof(now));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SET_DEVICE_TIME && len >= 5)
    {
        uint32_t epoch = 0;
        std::memcpy(&epoch, &data[1], sizeof(epoch));
        ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setCurrentEpochSeconds(epoch);
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_GET_BATT_AND_STORAGE)
    {
        uint8_t out[11] = {};
        out[0] = RESP_CODE_BATT_AND_STORAGE;
        const auto battery = platform::ui::device::battery_info();
        const uint8_t level = (battery.level >= 0) ? static_cast<uint8_t>(battery.level) : 0;
        const uint16_t mv = static_cast<uint16_t>(3000U + static_cast<uint16_t>(level) * 12U);
        const uint32_t used = 0;
        const uint32_t total = 0;
        std::memcpy(&out[1], &mv, sizeof(mv));
        std::memcpy(&out[3], &used, sizeof(used));
        std::memcpy(&out[7], &total, sizeof(total));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SET_RADIO_PARAMS && len >= 11)
    {
        uint32_t freq = 0;
        uint32_t bw = 0;
        std::memcpy(&freq, &data[1], sizeof(freq));
        std::memcpy(&bw, &data[5], sizeof(bw));
        const uint8_t sf = data[9];
        const uint8_t cr = data[10];
        if (freq < 300000U || freq > 2500000U || bw < 7000U || bw > 500000U || sf < 5U || sf > 12U || cr < 5U ||
            cr > 8U)
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        cfg.meshcore_config.meshcore_freq_mhz = static_cast<float>(freq) / 1000.0f;
        cfg.meshcore_config.meshcore_bw_khz = static_cast<float>(bw) / 1000.0f;
        cfg.meshcore_config.meshcore_sf = sf;
        cfg.meshcore_config.meshcore_cr = cr;
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SET_RADIO_TX_POWER && len >= 2)
    {
        const int8_t tx = static_cast<int8_t>(data[1]);
        if (tx < app::AppConfig::kTxPowerMinDbm || tx > app::AppConfig::kTxPowerMaxDbm)
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        cfg.meshcore_config.tx_power = tx;
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_GET_CHANNEL && len >= 2)
    {
        const uint8_t channel_idx = data[1];
        if (channel_idx > 1U)
        {
            enqueueErr(ERR_CODE_NOT_FOUND);
            return;
        }
        uint8_t out[kMaxFrameSize] = {};
        size_t index = 0;
        out[index++] = RESP_CODE_CHANNEL_INFO;
        out[index++] = channel_idx;
        if (channel_idx == 0U)
        {
            copyBounded(reinterpret_cast<char*>(&out[index]), 32, cfg.meshcore_config.meshcore_channel_name);
            index += 32;
            std::memcpy(&out[index], cfg.meshcore_config.primary_key, 16);
            index += 16;
        }
        else
        {
            copyBounded(reinterpret_cast<char*>(&out[index]), 32, "Secondary");
            index += 32;
            std::memcpy(&out[index], cfg.meshcore_config.secondary_key, 16);
            index += 16;
        }
        enqueueFrame(out, index);
        return;
    }

    if (cmd == CMD_EXPORT_PRIVATE_KEY)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t priv[chat::meshcore::kMeshCorePrivKeySize] = {};
        if (!lite->exportIdentityPrivateKey(priv, sizeof(priv)))
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint8_t out[1 + chat::meshcore::kMeshCorePrivKeySize] = {};
        out[0] = RESP_CODE_PRIVATE_KEY;
        std::memcpy(&out[1], priv, sizeof(priv));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_IMPORT_PRIVATE_KEY && len >= 1 + chat::meshcore::kMeshCorePrivKeySize)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (!lite->importIdentityPrivateKey(&data[1], chat::meshcore::kMeshCorePrivKeySize))
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SEND_PATH_DISCOVERY_REQ)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (len < 2 + kPubKeySize || data[1] != 0)
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        const uint8_t* pubkey = &data[2];
        uint8_t req_data[9] = {};
        req_data[0] = 0x03;
        req_data[1] = static_cast<uint8_t>(~0x01U);
        const uint32_t nonce = static_cast<uint32_t>(millis());
        std::memcpy(&req_data[5], &nonce, sizeof(nonce));

        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!lite->sendPeerRequestPayload(pubkey,
                                          kPubKeySize,
                                          req_data,
                                          sizeof(req_data),
                                          true,
                                          &tag,
                                          &est_timeout,
                                          &sent_flood))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }

        ++stats_tx_packets_;
        ++stats_tx_flood_;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1U : 0U;
        std::memcpy(&out[2], &tag, sizeof(tag));
        std::memcpy(&out[6], &est_timeout, sizeof(est_timeout));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_RAW_DATA && len >= 6)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        size_t index = 1;
        const int8_t path_len = static_cast<int8_t>(data[index++]);
        if (path_len < 0 || (index + static_cast<size_t>(path_len) + 4U) > len)
        {
            enqueueErr(ERR_CODE_UNSUPPORTED_CMD);
            return;
        }
        const uint8_t* path = &data[index];
        index += static_cast<size_t>(path_len);
        const uint8_t* payload = &data[index];
        const size_t payload_len = len - index;
        uint32_t est_timeout = 0;
        if (!lite->sendRawData(path, static_cast<size_t>(path_len), payload, payload_len, &est_timeout))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }
        ++stats_tx_packets_;
        ++stats_tx_direct_;
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SEND_BINARY_REQ && len >= 1 + kPubKeySize + 1)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        const uint8_t* pubkey = &data[1];
        const uint8_t* payload = &data[1 + kPubKeySize];
        const size_t payload_len = len - (1 + kPubKeySize);
        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        bool sent_flood = false;
        if (!lite->sendPeerRequestPayload(pubkey,
                                          kPubKeySize,
                                          payload,
                                          payload_len,
                                          false,
                                          &tag,
                                          &est_timeout,
                                          &sent_flood))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }

        ++stats_tx_packets_;
        ++stats_tx_flood_;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = sent_flood ? 1U : 0U;
        std::memcpy(&out[2], &tag, sizeof(tag));
        std::memcpy(&out[6], &est_timeout, sizeof(est_timeout));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SEND_TELEMETRY_REQ)
    {
        if (len == 4)
        {
            uint8_t out[kMaxFrameSize] = {};
            size_t index = 0;
            out[index++] = PUSH_CODE_TELEMETRY_RESPONSE;
            out[index++] = 0;
            uint8_t prefix[kPrefixSize] = {};
            if (lite)
            {
                uint8_t pubkey[kPubKeySize] = {};
                if (lite->exportIdentityPublicKey(pubkey, sizeof(pubkey)))
                {
                    std::memcpy(prefix, pubkey, kPrefixSize);
                }
            }
            if (prefix[0] == 0)
            {
                const uint32_t self_id = ctx_.getSelfNodeId();
                std::memcpy(prefix, &self_id, std::min(sizeof(self_id), sizeof(prefix)));
            }
            std::memcpy(&out[index], prefix, sizeof(prefix));
            index += sizeof(prefix);
            const auto battery = platform::ui::device::battery_info();
            const uint8_t level = (battery.level >= 0) ? static_cast<uint8_t>(battery.level) : 0;
            const uint16_t mv = static_cast<uint16_t>(3000U + static_cast<uint16_t>(level) * 12U);
            const uint16_t lpp_val = static_cast<uint16_t>(mv / 10U);
            out[index++] = 1;
            out[index++] = 116;
            std::memcpy(&out[index], &lpp_val, sizeof(lpp_val));
            index += sizeof(lpp_val);
            enqueueFrame(out, index);
            return;
        }
        if (len >= 4 + kPubKeySize)
        {
            if (!lite)
            {
                enqueueErr(ERR_CODE_BAD_STATE);
                return;
            }
            const uint8_t* pubkey = &data[4];
            uint32_t tag = 0;
            uint32_t est_timeout = 0;
            bool sent_flood = false;
            if (!lite->sendPeerRequestType(pubkey, kPubKeySize, 0x03, &tag, &est_timeout, &sent_flood))
            {
                enqueueErr(ERR_CODE_TABLE_FULL);
                return;
            }
            ++stats_tx_packets_;
            ++stats_tx_flood_;
            uint8_t out[10] = {};
            out[0] = RESP_CODE_SENT;
            out[1] = sent_flood ? 1U : 0U;
            std::memcpy(&out[2], &tag, sizeof(tag));
            std::memcpy(&out[6], &est_timeout, sizeof(est_timeout));
            enqueueFrame(out, sizeof(out));
            return;
        }
        enqueueErr(ERR_CODE_ILLEGAL_ARG);
        return;
    }

    if (cmd == CMD_SEND_TRACE_PATH && len > 10)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        uint32_t tag = 0;
        uint32_t auth = 0;
        std::memcpy(&tag, &data[1], sizeof(tag));
        std::memcpy(&auth, &data[5], sizeof(auth));
        const uint8_t flags = data[9];
        const size_t path_len = len - 10;
        const uint8_t path_sz_bits = flags & 0x03U;
        const size_t path_stride = static_cast<size_t>(1U << path_sz_bits);
        if (path_stride == 0 || (path_len >> path_sz_bits) > kMaxPathSize || (path_len % path_stride) != 0)
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        uint32_t est_timeout = 0;
        if (!lite->sendTracePath(&data[10], path_len, tag, auth, flags, &est_timeout))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }
        ++stats_tx_packets_;
        ++stats_tx_direct_;
        uint8_t out[10] = {};
        out[0] = RESP_CODE_SENT;
        out[1] = 0;
        std::memcpy(&out[2], &tag, sizeof(tag));
        std::memcpy(&out[6], &est_timeout, sizeof(est_timeout));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_GET_ADVERT_PATH && len >= 2 + kPubKeySize)
    {
        const uint8_t* pubkey = &data[2];
        uint32_t ts = 0;
        uint8_t path_len = 0;
        uint8_t path[kMaxPathSize] = {};
        bool found = false;
        if (const auto* store = ctx_.getNodeStore())
        {
            for (const auto& entry : store->getEntries())
            {
                if (entry.node_id == 0)
                {
                    continue;
                }
                uint32_t entry_id = 0;
                std::memcpy(&entry_id, pubkey, std::min(sizeof(entry_id), kPubKeySize));
                if (entry.node_id == entry_id)
                {
                    ts = entry.last_seen;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            enqueueErr(ERR_CODE_NOT_FOUND);
            return;
        }
        uint8_t out[kMaxFrameSize] = {};
        size_t index = 0;
        out[index++] = RESP_CODE_ADVERT_PATH;
        std::memcpy(&out[index], &ts, sizeof(ts));
        index += sizeof(ts);
        out[index++] = path_len;
        if (path_len > 0)
        {
            std::memcpy(&out[index], path, path_len);
            index += path_len;
        }
        enqueueFrame(out, index);
        return;
    }

    if (cmd == CMD_REBOOT && len >= 7)
    {
        if (std::memcmp(&data[1], "reboot", 6) != 0)
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        enqueueSentOk();
        delay(100);
        NVIC_SystemReset();
        return;
    }

    if (cmd == CMD_GET_CONTACT_BY_KEY && len >= 2)
    {
        const uint8_t* key = &data[1];
        const size_t key_len = len - 1;
        if (const auto* store = ctx_.getNodeStore())
        {
            for (const auto& entry : store->getEntries())
            {
                if (entry.node_id == 0)
                {
                    continue;
                }
                if (std::memcmp(&entry.node_id, key, std::min(key_len, sizeof(entry.node_id))) == 0)
                {
                    Frame frame{};
                    if (buildContactFromNode(entry, RESP_CODE_CONTACT, frame))
                    {
                        enqueueFrame(frame.buf.data(), frame.len);
                        return;
                    }
                }
            }
        }
        enqueueErr(ERR_CODE_NOT_FOUND);
        return;
    }

    if (cmd == CMD_RESET_PATH && len >= 1 + kPubKeySize)
    {
        if (lite)
        {
            lite->setFloodScopeKey(nullptr, 0);
        }
        enqueueSentOk();
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
        const uint32_t max_len = 8 * 1024;
        std::memcpy(&out[2], &max_len, sizeof(max_len));
        enqueueFrame(out, sizeof(out));
        return;
    }

    if (cmd == CMD_SIGN_DATA && len > 1)
    {
        if (!sign_active_)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (sign_data_.size() + (len - 1) > (8 * 1024))
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        sign_data_.insert(sign_data_.end(), &data[1], &data[len]);
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SIGN_FINISH)
    {
        if (!sign_active_ || !lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        sign_active_ = false;
        uint8_t sig[chat::meshcore::kMeshCoreSignatureSize] = {};
        if (!lite->signPayload(sign_data_.data(), sign_data_.size(), sig, sizeof(sig)))
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            sign_data_.clear();
            return;
        }
        uint8_t out[1 + chat::meshcore::kMeshCoreSignatureSize] = {};
        out[0] = RESP_CODE_SIGNATURE;
        std::memcpy(&out[1], sig, sizeof(sig));
        enqueueFrame(out, sizeof(out));
        sign_data_.clear();
        return;
    }

    if (cmd == CMD_SET_CHANNEL && len >= 2 + 32 + 16)
    {
        const uint8_t channel_idx = data[1];
        if (channel_idx > 1U)
        {
            enqueueErr(ERR_CODE_NOT_FOUND);
            return;
        }
        if (channel_idx == 0U)
        {
            copyBounded(cfg.meshcore_config.meshcore_channel_name,
                        sizeof(cfg.meshcore_config.meshcore_channel_name),
                        reinterpret_cast<const char*>(&data[2]));
            std::memcpy(cfg.meshcore_config.primary_key, &data[34], 16);
        }
        else
        {
            std::memcpy(cfg.meshcore_config.secondary_key, &data[34], 16);
        }
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SEND_CONTROL_DATA && len >= 2 && (data[1] & 0x80U) != 0)
    {
        if (!lite)
        {
            enqueueErr(ERR_CODE_BAD_STATE);
            return;
        }
        if (!lite->sendControlData(&data[1], len - 1))
        {
            enqueueErr(ERR_CODE_TABLE_FULL);
            return;
        }
        ++stats_tx_packets_;
        ++stats_tx_direct_;
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_SET_FLOOD_SCOPE && len >= 2 && data[1] == 0)
    {
        if (lite)
        {
            if (len >= 18)
            {
                lite->setFloodScopeKey(&data[2], 16);
            }
            else
            {
                lite->setFloodScopeKey(nullptr, 0);
            }
        }
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_HAS_CONNECTION && len >= 1 + kPubKeySize)
    {
        const uint32_t node_id = resolveNodeIdFromPrefix(&data[1], sizeof(uint32_t));
        if (node_id != 0)
        {
            enqueueSentOk();
        }
        else
        {
            enqueueErr(ERR_CODE_NOT_FOUND);
        }
        return;
    }

    if (cmd == CMD_LOGOUT && len >= 1 + kPubKeySize)
    {
        enqueueSentOk();
        return;
    }

    if (cmd == CMD_GET_STATS && len >= 2)
    {
        const uint8_t stats_type = data[1];
        if (stats_type == STATS_TYPE_CORE)
        {
            uint8_t out[16] = {};
            size_t index = 0;
            out[index++] = RESP_CODE_STATS;
            out[index++] = STATS_TYPE_CORE;
            const uint16_t mv = 0;
            const uint32_t uptime = millis() / 1000U;
            const uint16_t err_flags = 0;
            const uint8_t queue_len = static_cast<uint8_t>(tx_queue_.size());
            std::memcpy(&out[index], &mv, sizeof(mv));
            index += sizeof(mv);
            std::memcpy(&out[index], &uptime, sizeof(uptime));
            index += sizeof(uptime);
            std::memcpy(&out[index], &err_flags, sizeof(err_flags));
            index += sizeof(err_flags);
            out[index++] = queue_len;
            enqueueFrame(out, index);
            return;
        }

        if (stats_type == STATS_TYPE_RADIO)
        {
            uint8_t out[16] = {};
            size_t index = 0;
            out[index++] = RESP_CODE_STATS;
            out[index++] = STATS_TYPE_RADIO;
            const int16_t noise_floor = 0;
            const int8_t last_rssi = 0;
            const int8_t last_snr = 0;
            const uint32_t tx_air = 0;
            const uint32_t rx_air = 0;
            std::memcpy(&out[index], &noise_floor, sizeof(noise_floor));
            index += sizeof(noise_floor);
            out[index++] = static_cast<uint8_t>(last_rssi);
            out[index++] = static_cast<uint8_t>(last_snr);
            std::memcpy(&out[index], &tx_air, sizeof(tx_air));
            index += sizeof(tx_air);
            std::memcpy(&out[index], &rx_air, sizeof(rx_air));
            index += sizeof(rx_air);
            enqueueFrame(out, index);
            return;
        }

        if (stats_type == STATS_TYPE_PACKETS)
        {
            uint8_t out[32] = {};
            size_t index = 0;
            out[index++] = RESP_CODE_STATS;
            out[index++] = STATS_TYPE_PACKETS;
            uint32_t counts[6] = {
                stats_rx_packets_,
                stats_tx_packets_,
                stats_tx_flood_,
                stats_tx_direct_,
                stats_rx_flood_,
                stats_rx_direct_,
            };
            std::memcpy(&out[index], counts, sizeof(counts));
            index += sizeof(counts);
            enqueueFrame(out, index);
            return;
        }

        enqueueErr(ERR_CODE_ILLEGAL_ARG);
        return;
    }

    if (cmd == CMD_FACTORY_RESET && len >= 6)
    {
        if (std::memcmp(&data[1], "reset", 5) != 0)
        {
            enqueueErr(ERR_CODE_ILLEGAL_ARG);
            return;
        }
        ctx_.resetMeshConfig();
        ctx_.clearNodeDb();
        ctx_.clearMessageDb();
        ctx_.setBleEnabled(true);
        tx_queue_.clear();
        sign_data_.clear();
        sign_active_ = false;
        enqueueSentOk();
        delay(100);
        NVIC_SystemReset();
        return;
    }

    enqueueErr(ERR_CODE_ILLEGAL_ARG);
}

void MeshCoreBleService::enqueueFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }
    Frame frame{};
    frame.len = std::min(len, frame.buf.size());
    std::memcpy(frame.buf.data(), data, frame.len);
    tx_queue_.push_back(frame);
}

void MeshCoreBleService::enqueueSentOk()
{
    const uint8_t code = RESP_CODE_OK;
    enqueueFrame(&code, 1);
}

void MeshCoreBleService::enqueueErr(uint8_t err)
{
    const uint8_t buf[2] = {RESP_CODE_ERR, err};
    enqueueFrame(buf, sizeof(buf));
}

void MeshCoreBleService::sendPendingNotifications()
{
    if (!active_ || !Bluefruit.connected())
    {
        return;
    }

    while (!tx_queue_.empty())
    {
        const Frame& frame = tx_queue_.front();
        if (!tx_char_.notify(frame.buf.data(), static_cast<uint16_t>(frame.len)))
        {
            break;
        }
        tx_queue_.pop_front();
    }
}

void MeshCoreBleService::enqueueRawDataPush(const chat::MeshIncomingData& msg)
{
    ++stats_rx_packets_;
    if (msg.rx_meta.direct)
    {
        ++stats_rx_direct_;
    }
    else
    {
        ++stats_rx_flood_;
    }

    Frame frame{};
    size_t index = 0;
    frame.buf[index++] = PUSH_CODE_RAW_DATA;
    const size_t payload_len = std::min(msg.payload.size(), frame.buf.size() - index);
    if (payload_len > 0)
    {
        std::memcpy(&frame.buf[index], msg.payload.data(), payload_len);
        index += payload_len;
    }
    frame.len = index;
    tx_queue_.push_back(frame);
}

uint32_t MeshCoreBleService::resolveNodeIdFromPrefix(const uint8_t* prefix, size_t len) const
{
    if (!prefix || len == 0)
    {
        return 0;
    }

    if (len >= sizeof(uint32_t))
    {
        uint32_t node_id = 0;
        std::memcpy(&node_id, prefix, sizeof(node_id));
        if (node_id != 0)
        {
            return node_id;
        }
    }

    const auto* store = ctx_.getNodeStore();
    if (!store)
    {
        return 0;
    }

    for (const auto& entry : store->getEntries())
    {
        if (entry.node_id == 0)
        {
            continue;
        }
        if (std::memcmp(&entry.node_id, prefix, std::min(len, sizeof(entry.node_id))) == 0)
        {
            return entry.node_id;
        }
    }
    return 0;
}

bool MeshCoreBleService::buildContactFromNode(const chat::contacts::NodeEntry& entry, uint8_t code, Frame& out) const
{
    size_t index = 0;
    out.buf[index++] = code;
    uint8_t pubkey[kPubKeySize] = {};
    std::memcpy(pubkey, &entry.node_id, std::min(sizeof(entry.node_id), sizeof(pubkey)));
    std::memcpy(&out.buf[index], pubkey, sizeof(pubkey));
    index += sizeof(pubkey);
    out.buf[index++] = ADV_TYPE_CHAT;
    out.buf[index++] = 0;
    out.buf[index++] = 0;
    std::memset(&out.buf[index], 0, kMaxPathSize);
    index += kMaxPathSize;

    char name[32] = {};
    copyBounded(name, sizeof(name), entry.long_name[0] != '\0' ? entry.long_name : entry.short_name);
    if (name[0] == '\0')
    {
        std::snprintf(name, sizeof(name), "%08lX", static_cast<unsigned long>(entry.node_id));
    }
    copyBounded(reinterpret_cast<char*>(&out.buf[index]), 32, name);
    index += 32;

    const uint32_t last_adv = entry.last_seen;
    std::memcpy(&out.buf[index], &last_adv, sizeof(last_adv));
    index += sizeof(last_adv);
    const int32_t lat = 0;
    const int32_t lon = 0;
    std::memcpy(&out.buf[index], &lat, sizeof(lat));
    index += sizeof(lat);
    std::memcpy(&out.buf[index], &lon, sizeof(lon));
    index += sizeof(lon);
    std::memcpy(&out.buf[index], &last_adv, sizeof(last_adv));
    index += sizeof(last_adv);

    out.len = index;
    return true;
}

} // namespace ble
