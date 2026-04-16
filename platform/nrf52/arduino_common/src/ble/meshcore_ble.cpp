#include "../../include/ble/meshcore_ble.h"

#include "ble/ble_uuids.h"
#include "ble/bluefruit_runtime.h"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

namespace ble
{
namespace
{

constexpr size_t kMaxFrameSize = 172;
MeshCoreBleService* s_active_service = nullptr;

void disconnectAll()
{
    uint16_t handles[BLE_MAX_CONNECTION] = {};
    const uint8_t count = Bluefruit.getConnectedHandles(handles, BLE_MAX_CONNECTION);
    for (uint8_t i = 0; i < count; ++i)
    {
        Bluefruit.disconnect(handles[i]);
    }
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
    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

bool parseBoolValue(const char* value, bool* out)
{
    if (!value || !out)
    {
        return false;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "on") == 0 || std::strcmp(value, "yes") == 0)
    {
        *out = true;
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "off") == 0 || std::strcmp(value, "no") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

void appendCustomVar(std::string& out, const char* key, const char* value)
{
    if (!key || !value)
    {
        return;
    }
    if (!out.empty())
    {
        out.push_back(',');
    }
    out += key;
    out.push_back(':');
    out += value;
}

void prepareBluefruit(const std::string& device_name)
{
    ::ble::bluefruit_runtime::ensureInitialized(device_name.c_str());
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

} // namespace

MeshCoreBleService::MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name),
      service_(::BLEUuid(NUS_SERVICE_UUID)),
      rx_char_(::BLEUuid(NUS_CHAR_RX_UUID)),
      tx_char_(::BLEUuid(NUS_CHAR_TX_UUID)),
      core_(new MeshCorePhoneCore(ctx, device_name, this))
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

    if (!gatt_initialized_)
    {
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
        gatt_initialized_ = true;
    }

    if (!observer_registered_)
    {
        ctx_.getChatService().addIncomingTextObserver(this);
        observer_registered_ = true;
    }
    startAdvertising(service_);
    active_ = true;
}

void MeshCoreBleService::stop()
{
    if (observer_registered_)
    {
        ctx_.getChatService().removeIncomingTextObserver(this);
        observer_registered_ = false;
    }
    disconnectAll();
    Bluefruit.Advertising.stop();
    if (core_)
    {
        core_->reset();
    }
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

    if (core_)
    {
        core_->pumpIncomingAppData();
    }
    sendPendingNotifications();

    if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning())
    {
        Bluefruit.Advertising.start(0);
    }
}

void MeshCoreBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    if (core_)
    {
        core_->onIncomingText(msg);
    }
}

bool MeshCoreBleService::handleRxFrame(const uint8_t* data, size_t len)
{
    return core_ ? core_->handleRxFrame(data, len) : false;
}

bool MeshCoreBleService::popTxFrame(uint8_t* out, size_t* out_len)
{
    return core_ ? core_->popTxFrame(out, out_len) : false;
}

void MeshCoreBleService::sendPendingNotifications()
{
    if (!active_ || !Bluefruit.connected())
    {
        return;
    }

    uint8_t out[kMaxFrameSize] = {};
    size_t out_len = 0;
    while (core_ && core_->popTxFrame(out, &out_len))
    {
        if (!tx_char_.notify(out, static_cast<uint16_t>(out_len)))
        {
            break;
        }
    }
}

bool MeshCoreBleService::isRunning() const
{
    return active_ && (Bluefruit.connected() || Bluefruit.Advertising.isRunning());
}

void MeshCoreBleService::setDeviceName(const std::string& name)
{
    device_name_ = name;
}

bool MeshCoreBleService::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }

    *out = BlePairingStatus{};
    out->available = active_;
    out->is_connected = Bluefruit.connected();
    return true;
}

bool MeshCoreBleService::getCustomVars(std::string* out) const
{
    if (!out)
    {
        return false;
    }

    out->clear();

    const auto& cfg = ctx_.getConfig();
    appendCustomVar(*out, "node_name", cfg.node_name);
    appendCustomVar(*out, "channel_name", cfg.meshcore_config.meshcore_channel_name);
    appendCustomVar(*out, "multi_acks", cfg.meshcore_config.meshcore_multi_acks ? "1" : "0");
    appendCustomVar(*out, "gps", cfg.gps_strategy == 2 ? "0" : "1");

    // Keep the variable surface compatible with the MeshCore app even if
    // nRF52 doesn't implement every extended option yet.
    appendCustomVar(*out, "manual_add_contacts", "0");
    appendCustomVar(*out, "telemetry_base", "0");
    appendCustomVar(*out, "telemetry_loc", "0");
    appendCustomVar(*out, "telemetry_env", "0");
    appendCustomVar(*out, "advert_loc_policy", "0");
    appendCustomVar(*out, "ble_pin", "0");

    return true;
}

bool MeshCoreBleService::setCustomVar(const char* key, const char* value)
{
    if (!key || !value)
    {
        return false;
    }

    auto& cfg = ctx_.getConfig();
    bool changed = false;

    if (std::strcmp(key, "node_name") == 0)
    {
        char next[sizeof(cfg.node_name)] = {};
        copyBounded(next, sizeof(next), value);
        if (std::strcmp(cfg.node_name, next) != 0)
        {
            copyBounded(cfg.node_name, sizeof(cfg.node_name), next);
            changed = true;
        }
    }
    else if (std::strcmp(key, "channel_name") == 0)
    {
        char next[sizeof(cfg.meshcore_config.meshcore_channel_name)] = {};
        copyBounded(next, sizeof(next), value);
        if (std::strcmp(cfg.meshcore_config.meshcore_channel_name, next) != 0)
        {
            copyBounded(cfg.meshcore_config.meshcore_channel_name,
                        sizeof(cfg.meshcore_config.meshcore_channel_name), next);
            changed = true;
        }
    }
    else if (std::strcmp(key, "multi_acks") == 0)
    {
        bool parsed = false;
        if (!parseBoolValue(value, &parsed))
        {
            return false;
        }
        if (cfg.meshcore_config.meshcore_multi_acks != parsed)
        {
            cfg.meshcore_config.meshcore_multi_acks = parsed;
            changed = true;
        }
    }
    else if (std::strcmp(key, "gps") == 0)
    {
        bool parsed = false;
        if (!parseBoolValue(value, &parsed))
        {
            return false;
        }
        const uint8_t next_strategy = parsed ? (cfg.gps_strategy == 2 ? 0 : cfg.gps_strategy) : 2;
        if (cfg.gps_strategy != next_strategy)
        {
            cfg.gps_strategy = next_strategy;
            changed = true;
        }
    }
    else if (std::strcmp(key, "manual_add_contacts") == 0 ||
             std::strcmp(key, "telemetry_base") == 0 ||
             std::strcmp(key, "telemetry_loc") == 0 ||
             std::strcmp(key, "telemetry_env") == 0 ||
             std::strcmp(key, "advert_loc_policy") == 0 ||
             std::strcmp(key, "ble_pin") == 0)
    {
        // Report success for currently unsupported extended variables so the
        // app can keep using the basic settings surface without hard failure.
        return true;
    }
    else
    {
        return false;
    }

    if (changed)
    {
        ctx_.saveConfig();
    }
    return true;
}

} // namespace ble
