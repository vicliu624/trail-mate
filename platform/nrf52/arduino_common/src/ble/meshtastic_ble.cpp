#include "../../include/ble/meshtastic_ble.h"

#include "app/app_config.h"
#include "ble/ble_uuids.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"

#include <Arduino.h>
#include <cstring>

namespace ble
{
namespace
{
constexpr const char* kDefaultMqttRoot = "msh";

platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter* getMeshtasticBackend(app::IAppBleFacade& ctx)
{
    auto* adapter = ctx.getMeshAdapter();
    if (!adapter || adapter->backendProtocol() != chat::MeshProtocol::Meshtastic)
    {
        return nullptr;
    }

    auto* backend = adapter->backendForProtocol(chat::MeshProtocol::Meshtastic);
    return static_cast<platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter*>(backend);
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

std::string channelDisplayName(const app::IAppBleFacade& ctx, uint8_t idx)
{
    const auto& cfg = ctx.getConfig();
    if (idx == 1)
    {
        return "Secondary";
    }

    auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset);
    return cfg.meshtastic_config.use_preset
               ? std::string(chat::meshtastic::presetDisplayName(preset))
               : std::string("Custom");
}

MeshtasticBleService* s_active_service = nullptr;

void onBleConnect(uint16_t)
{
    if (s_active_service)
    {
        s_active_service->handleConnectEvent();
    }
}

void onBleDisconnect(uint16_t, uint8_t)
{
    if (!s_active_service)
    {
        return;
    }
    s_active_service->handleDisconnectEvent();
}

void prepareBluefruit(const std::string& device_name)
{
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.setName(device_name.c_str());
    Bluefruit.Periph.setConnectCallback(onBleConnect);
    Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);
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

void disconnectAll()
{
    for (uint8_t index = 0; index < BLE_MAX_CONNECTION; ++index)
    {
        if (Bluefruit.connected(index))
        {
            Bluefruit.disconnect(index);
        }
    }
}

void authorizeRead(uint16_t conn_handle)
{
    ble_gatts_rw_authorize_reply_params_t reply = {.type = BLE_GATTS_AUTHORIZE_TYPE_READ};
    reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
    sd_ble_gatts_rw_authorize_reply(conn_handle, &reply);
}

void onToRadioWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len)
{
    if (!s_active_service || !data || len == 0)
    {
        return;
    }
    (void)s_active_service->handleToRadio(data, len);
}

void onFromRadioAuthorize(uint16_t conn_handle, BLECharacteristic* chr, ble_gatts_evt_read_t* request)
{
    if (!chr || !request)
    {
        authorizeRead(conn_handle);
        return;
    }

    if (request->offset == 0)
    {
        MeshtasticBleFrame frame{};
        if (s_active_service && s_active_service->popToPhone(&frame))
        {
            chr->write(frame.buf, frame.len);
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

MeshtasticBleService::MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name),
      service_(::BLEUuid(MESH_SERVICE_UUID)),
      to_radio_(::BLEUuid(TORADIO_UUID)),
      from_radio_(::BLEUuid(FROMRADIO_UUID)),
      from_num_(::BLEUuid(FROMNUM_UUID)),
      log_radio_(::BLEUuid(LOGRADIO_UUID)),
      phone_session_(new MeshtasticPhoneSession(ctx, *this, this))
{
    ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    ble_config_.enabled = ctx_.isBleEnabled();
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    ble_config_.fixed_pin = 0;

    std::memset(&module_config_, 0, sizeof(module_config_));
    module_config_.has_mqtt = true;
    module_config_.mqtt.enabled = false;
    module_config_.mqtt.proxy_to_client_enabled = false;
    module_config_.mqtt.encryption_enabled = true;
    copyBounded(module_config_.mqtt.root, sizeof(module_config_.mqtt.root), kDefaultMqttRoot);
}

MeshtasticBleService::~MeshtasticBleService()
{
    stop();
}

void MeshtasticBleService::start()
{
    s_active_service = this;
    prepareBluefruit(device_name_);

    service_.begin();

    to_radio_.setProperties(CHR_PROPS_WRITE);
    to_radio_.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    to_radio_.setFixedLen(0);
    to_radio_.setMaxLen(meshtastic_ToRadio_size);
    to_radio_.setWriteCallback(onToRadioWrite, false);
    to_radio_.begin();

    from_radio_.setProperties(CHR_PROPS_READ);
    from_radio_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    from_radio_.setFixedLen(0);
    from_radio_.setMaxLen(meshtastic_FromRadio_size);
    from_radio_.setReadAuthorizeCallback(onFromRadioAuthorize, false);
    from_radio_.begin();

    from_num_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    from_num_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    from_num_.setFixedLen(4);
    from_num_.write32(0);
    from_num_.begin();

    log_radio_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    log_radio_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    log_radio_.setFixedLen(0);
    log_radio_.setMaxLen(96);
    log_radio_.begin();

    ctx_.getChatService().addIncomingTextObserver(this);
    startAdvertising(service_);
    active_ = true;
    syncMqttProxySettings();
}

void MeshtasticBleService::stop()
{
    ctx_.getChatService().removeIncomingTextObserver(this);
    disconnectAll();
    Bluefruit.Advertising.stop();
    if (phone_session_)
    {
        phone_session_->close();
    }
    active_ = false;
    connected_ = false;
    if (s_active_service == this)
    {
        s_active_service = nullptr;
    }
}

void MeshtasticBleService::update()
{
    if (!active_)
    {
        return;
    }

    syncMqttProxySettings();

    if (phone_session_)
    {
        phone_session_->pumpIncomingAppData();
    }

    if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning())
    {
        Bluefruit.Advertising.start(0);
    }
}

void MeshtasticBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        phone_session_->onIncomingText(msg);
    }
}

bool MeshtasticBleService::handleToRadio(const uint8_t* data, size_t len)
{
    return phone_session_ ? phone_session_->handleToRadio(data, len) : false;
}

bool MeshtasticBleService::popToPhone(MeshtasticBleFrame* out)
{
    return phone_session_ ? phone_session_->popToPhone(out) : false;
}

void MeshtasticBleService::handleConnectEvent()
{
    connected_ = true;
}

void MeshtasticBleService::handleDisconnectEvent()
{
    connected_ = false;
    if (phone_session_)
    {
        phone_session_->close();
    }
}

bool MeshtasticBleService::isBleConnected() const
{
    return connected_ && Bluefruit.connected();
}

void MeshtasticBleService::notifyFromNum(uint32_t from_num)
{
    if (active_ && Bluefruit.connected())
    {
        from_num_.notify32(from_num);
    }
}

bool MeshtasticBleService::loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const
{
    if (!out)
    {
        return false;
    }
    *out = ble_config_;
    return true;
}

bool MeshtasticBleService::loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const
{
    if (!out)
    {
        return false;
    }
    meshtastic_DeviceConnectionStatus status = meshtastic_DeviceConnectionStatus_init_zero;
    *out = status;
    out->has_bluetooth = true;
    out->bluetooth.pin = 0;
    out->bluetooth.rssi = 0;
    out->bluetooth.is_connected = isBleConnected();
    return true;
}

bool MeshtasticBleService::loadModuleConfig(meshtastic_LocalModuleConfig* out) const
{
    if (!out)
    {
        return false;
    }

    *out = module_config_;
    return true;
}

void MeshtasticBleService::saveModuleConfig(const meshtastic_LocalModuleConfig& config)
{
    module_config_ = config;
    syncMqttProxySettings();
}

bool MeshtasticBleService::handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg)
{
    auto* mt = getMeshtasticBackend(ctx_);
    return mt ? mt->handleMqttProxyMessage(msg) : false;
}

bool MeshtasticBleService::pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out)
{
    auto* mt = getMeshtasticBackend(ctx_);
    return (mt && out) ? mt->pollMqttProxyMessage(out) : false;
}

void MeshtasticBleService::syncMqttProxySettings()
{
    auto* mt = getMeshtasticBackend(ctx_);
    if (!mt)
    {
        return;
    }

    const auto& cfg = ctx_.getConfig();
    platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter::MqttProxySettings settings;
    settings.enabled = module_config_.has_mqtt && module_config_.mqtt.enabled;
    settings.proxy_to_client_enabled = module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled;
    settings.encryption_enabled = module_config_.has_mqtt ? module_config_.mqtt.encryption_enabled : true;
    settings.primary_uplink_enabled = cfg.primary_uplink_enabled;
    settings.primary_downlink_enabled = cfg.primary_downlink_enabled;
    settings.secondary_uplink_enabled = cfg.secondary_uplink_enabled;
    settings.secondary_downlink_enabled = cfg.secondary_downlink_enabled;
    settings.root = module_config_.mqtt.root[0] ? module_config_.mqtt.root : kDefaultMqttRoot;
    settings.primary_channel_id = channelDisplayName(ctx_, 0);
    settings.secondary_channel_id = channelDisplayName(ctx_, 1);
    mt->setMqttProxySettings(settings);
}

} // namespace ble
