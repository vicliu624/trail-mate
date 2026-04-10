#include "../../include/ble/meshtastic_ble.h"

#include "app/app_config.h"
#include "ble/ble_uuids.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ble
{
namespace
{
constexpr const char* kDefaultMqttRoot = "msh";
constexpr uint32_t kDefaultBleFixedPin = 654321;

void bleLogBoth(const char* fmt, ...)
{
    char buffer[192] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Serial.println(buffer);
    Serial2.println(buffer);
}

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

uint32_t parsePasskeyDigits(const uint8_t passkey[6])
{
    if (!passkey)
    {
        return 0;
    }

    char digits[7] = {};
    for (size_t i = 0; i < 6; ++i)
    {
        const uint8_t ch = passkey[i];
        if (ch < '0' || ch > '9')
        {
            return 0;
        }
        digits[i] = static_cast<char>(ch);
    }
    return static_cast<uint32_t>(std::strtoul(digits, nullptr, 10));
}

MeshtasticBleService* s_active_service = nullptr;

void onBleConnect(uint16_t conn_handle)
{
    if (s_active_service)
    {
        s_active_service->handleConnectEvent(conn_handle);
    }
}

void onBleDisconnect(uint16_t conn_handle, uint8_t)
{
    if (!s_active_service)
    {
        return;
    }
    s_active_service->handleDisconnectEvent(conn_handle);
}

bool onPairPasskeyDisplay(uint16_t conn_handle, uint8_t const passkey[6], bool match_request)
{
    if (s_active_service)
    {
        s_active_service->handlePairPasskeyDisplay(conn_handle, passkey, match_request);
    }
    return true;
}

void onPairComplete(uint16_t conn_handle, uint8_t auth_status)
{
    if (s_active_service)
    {
        s_active_service->handlePairComplete(conn_handle, auth_status);
    }
}

void onSecured(uint16_t conn_handle)
{
    if (s_active_service)
    {
        s_active_service->handleSecured(conn_handle);
    }
}

void prepareBluefruit(const std::string& device_name)
{
    bleLogBoth("[BLE][nrf52][mt] bluefruit begin name=%s", device_name.c_str());
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.setName(device_name.c_str());
    Bluefruit.Periph.setConnectCallback(onBleConnect);
    Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);
    bleLogBoth("[BLE][nrf52][mt] bluefruit ready");
}

void startAdvertising(::BLEService& service)
{
    (void)service;
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
    bleLogBoth("[BLE][nrf52][mt] advertising started running=%u",
               Bluefruit.Advertising.isRunning() ? 1U : 0U);
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
    (void)s_active_service->enqueueToRadio(data, len);
}

void onFromNumCccdWrite(uint16_t conn_handle, BLECharacteristic*, uint16_t value)
{
    if (s_active_service)
    {
        s_active_service->handleFromNumCccdWrite(conn_handle, value);
    }
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
        if (s_active_service && s_active_service->hasReadableFromRadio())
        {
            s_active_service->beginReadWait();
        }
        else
        {
            if (s_active_service)
            {
                if (s_active_service->shouldBlockOnRead())
                {
                    s_active_service->waitForReadableFromRadio(20, 1);
                }

                if (s_active_service->hasReadableFromRadio())
                {
                    s_active_service->beginReadWait();
                }
                else
                {
                    s_active_service->markReadableFromRadioConsumed();
                    s_active_service->endReadWait();
                }
            }
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
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
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
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    from_num_notify_enabled_ = false;
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pending_from_num_valid_ = false;
    pending_from_num_ = 0;
    pending_connect_log_ = false;
    pending_disconnect_log_ = false;
    pending_from_num_cccd_log_ = false;
    pending_pair_complete_log_ = false;
    pending_secured_log_ = false;
    pending_from_radio_read_log_ = false;
    pending_from_radio_empty_log_ = false;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    prepareBluefruit(device_name_);
    applyBleSecurity();

    service_.begin();
    bleLogBoth("[BLE][nrf52][mt] service begin");

    // Keep Meshtastic BLE characteristics open on nRF52 for compatibility.
    // The app expects to complete service discovery and config bootstrap without
    // getting stuck behind MITM-protected attribute access on Bluefruit.
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
    {
        uint8_t empty = 0;
        from_radio_.write(&empty, 0);
    }

    from_num_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    from_num_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    from_num_.setFixedLen(4);
    from_num_.write32(0);
    from_num_.setCccdWriteCallback(onFromNumCccdWrite, false);
    from_num_.begin();

    log_radio_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    log_radio_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    log_radio_.setFixedLen(0);
    log_radio_.setMaxLen(96);
    log_radio_.begin();
    bleLogBoth("[BLE][nrf52][mt] chars ready");

    ctx_.getChatService().addIncomingTextObserver(this);
    ctx_.getChatService().addOutgoingTextObserver(this);
    startAdvertising(service_);
    active_ = true;
    pending_passkey_.store(0);
    syncMqttProxySettings();
    bleLogBoth("[BLE][nrf52][mt] service active");
}

void MeshtasticBleService::stop()
{
    ctx_.getChatService().removeIncomingTextObserver(this);
    ctx_.getChatService().removeOutgoingTextObserver(this);
    disconnectAll();
    Bluefruit.Advertising.stop();
    if (phone_session_)
    {
        phone_session_->close();
    }
    active_ = false;
    connected_ = false;
    from_num_notify_enabled_ = false;
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_passkey_.store(0);
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pending_from_num_valid_ = false;
    pending_from_num_ = 0;
    pending_connect_log_ = false;
    pending_disconnect_log_ = false;
    pending_from_num_cccd_log_ = false;
    pending_pair_complete_log_ = false;
    pending_secured_log_ = false;
    pending_from_radio_read_log_ = false;
    pending_from_radio_empty_log_ = false;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
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

    processPendingPairingRequest();
    processPendingToRadio();
    if (from_radio_consume_pending_)
    {
        markReadableFromRadioConsumed();
    }
    handleToPhone();
    prepareReadableFromRadio();
    flushPendingFromNumNotify();
    logDeferredBleEvents();

    if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning())
    {
        Bluefruit.Advertising.start(0);
        bleLogBoth("[BLE][nrf52][mt] advertising restarted");
    }
}

void MeshtasticBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        phone_session_->onIncomingText(msg);
        if (phone_session_->isSendingPackets())
        {
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::onOutgoingText(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        Serial2.printf("[BLE][nrf52][mt] local text mirror id=%08lX from=%08lX to=%08lX len=%u\n",
                       static_cast<unsigned long>(msg.msg_id),
                       static_cast<unsigned long>(msg.from),
                       static_cast<unsigned long>(msg.to),
                       static_cast<unsigned>(msg.text.size()));
        phone_session_->onIncomingText(msg);
        if (phone_session_->isSendingPackets())
        {
            notifyFromNum(0);
        }
    }
}

bool MeshtasticBleService::handleToRadio(const uint8_t* data, size_t len)
{
    Serial2.printf("[BLE][nrf52][mt] handleToRadio len=%u connected=%u\n",
                   static_cast<unsigned>(len),
                   connected_ ? 1U : 0U);
    return phone_session_ ? phone_session_->handleToRadio(data, len) : false;
}

void MeshtasticBleService::handleToPhone()
{
    if (!phone_session_)
    {
        return;
    }

    const bool waiting_for_read = read_waiting_.load();
    const bool in_send_packets = phone_session_->isSendingPackets();
    const bool config_flow_active = phone_session_->isConfigFlowActive();
    const bool can_prepare = connected_ && (!in_send_packets || config_flow_active);

    // Meshtastic mobile apps often send `want_config` before they finish enabling
    // FROMNUM notifications. If we start emitting config frames too early, the
    // first bootstrap frame can be skipped and the app stays stuck on connecting.
    if (config_flow_active && !from_num_notify_enabled_)
    {
        return;
    }

    if (config_flow_active && (from_radio_preloaded_valid_ || to_phone_count_ > 0 || pending_to_phone_valid_))
    {
        return;
    }

    Frame frame{};
    if (pending_to_phone_valid_)
    {
        frame = pending_to_phone_;
    }
    else
    {
        if (!waiting_for_read && !can_prepare)
        {
            return;
        }
        if (to_phone_count_ >= kToPhoneQueueDepth)
        {
            return;
        }

        MeshtasticBleFrame session_frame{};
        if (!phone_session_->popToPhone(&session_frame))
        {
            if (waiting_for_read && in_send_packets)
            {
                read_waiting_.store(false);
            }
            return;
        }

        frame.len = session_frame.len;
        frame.from_num = session_frame.from_num;
        std::memcpy(frame.buf.data(), session_frame.buf, session_frame.len);
    }

    if (enqueueToPhoneFrame(frame))
    {
        pending_to_phone_valid_ = false;
        Serial2.printf("[BLE][nrf52][mt] to_phone enqueue from_num=%08lX len=%u q=%u\n",
                       static_cast<unsigned long>(frame.from_num),
                       static_cast<unsigned>(frame.len),
                       static_cast<unsigned>(to_phone_count_));
        if (!waiting_for_read && (in_send_packets || config_flow_active))
        {
            notifyFromNum(frame.from_num);
        }
        else if (can_prepare && !config_flow_active && to_phone_count_ < kToPhoneQueueDepth)
        {
            handleToPhone();
        }
    }
    else
    {
        pending_to_phone_ = frame;
        pending_to_phone_valid_ = true;
        Serial2.printf("[BLE][nrf52][mt] to_phone defer from_num=%08lX len=%u\n",
                       static_cast<unsigned long>(frame.from_num),
                       static_cast<unsigned>(frame.len));
    }
}

bool MeshtasticBleService::enqueueToRadio(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > meshtastic_ToRadio_size)
    {
        return false;
    }

    noInterrupts();
    if (pending_to_radio_count_ >= kPendingToRadioCapacity)
    {
        interrupts();
        Serial2.printf("[BLE][nrf52][mt] to_radio queue full len=%u\n", static_cast<unsigned>(len));
        return false;
    }

    PendingToRadioFrame& frame = pending_to_radio_[pending_to_radio_tail_];
    std::memcpy(frame.buf, data, len);
    frame.len = len;
    pending_to_radio_tail_ = static_cast<uint8_t>((pending_to_radio_tail_ + 1U) % kPendingToRadioCapacity);
    ++pending_to_radio_count_;
    interrupts();
    return true;
}

void MeshtasticBleService::processPendingToRadio()
{
    PendingToRadioFrame frame{};
    while (true)
    {
        noInterrupts();
        if (pending_to_radio_count_ == 0)
        {
            interrupts();
            return;
        }

        frame = pending_to_radio_[pending_to_radio_head_];
        pending_to_radio_head_ = static_cast<uint8_t>((pending_to_radio_head_ + 1U) % kPendingToRadioCapacity);
        --pending_to_radio_count_;
        interrupts();

        (void)handleToRadio(frame.buf, frame.len);
    }
}

void MeshtasticBleService::processPendingPairingRequest()
{
    if (!pairing_request_pending_)
    {
        return;
    }

    const uint16_t conn_handle = pending_pairing_conn_handle_;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    requestPairingIfNeeded(conn_handle);
}

bool MeshtasticBleService::popToPhone(MeshtasticBleFrame* out)
{
    return phone_session_ ? phone_session_->popToPhone(out) : false;
}

bool MeshtasticBleService::shouldBlockOnRead() const
{
    if (!connected_ || !phone_session_)
    {
        return false;
    }
    return phone_session_->isSendingPackets() || phone_session_->isConfigFlowActive();
}

void MeshtasticBleService::beginReadWait()
{
    read_waiting_.store(true);
    if (from_radio_preloaded_valid_)
    {
        from_radio_consume_pending_ = true;
    }
}

bool MeshtasticBleService::isReadWaiting() const
{
    return read_waiting_.load();
}

void MeshtasticBleService::endReadWait()
{
    read_waiting_.store(false);
}

bool MeshtasticBleService::waitForReadableFromRadio(uint8_t max_tries, uint8_t delay_ms)
{
    beginReadWait();
    uint8_t tries = 0;
    while (!hasReadableFromRadio() && isReadWaiting() && tries < max_tries)
    {
        handleToPhone();
        prepareReadableFromRadio();
        if (hasReadableFromRadio())
        {
            break;
        }
        delay(delay_ms);
        ++tries;
    }
    return hasReadableFromRadio();
}

bool MeshtasticBleService::enqueueToPhoneFrame(const Frame& frame)
{
    if (frame.len == 0 || frame.len > meshtastic_FromRadio_size)
    {
        return false;
    }

    noInterrupts();
    if (to_phone_count_ >= kToPhoneQueueDepth)
    {
        interrupts();
        return false;
    }

    to_phone_queue_[to_phone_tail_] = frame;
    to_phone_tail_ = static_cast<uint8_t>((to_phone_tail_ + 1U) % kToPhoneQueueDepth);
    ++to_phone_count_;
    interrupts();
    return true;
}

bool MeshtasticBleService::popQueuedToPhoneFrame(Frame* out)
{
    if (!out)
    {
        return false;
    }

    noInterrupts();
    if (to_phone_count_ == 0)
    {
        interrupts();
        return false;
    }

    *out = to_phone_queue_[to_phone_head_];
    to_phone_head_ = static_cast<uint8_t>((to_phone_head_ + 1U) % kToPhoneQueueDepth);
    --to_phone_count_;
    interrupts();
    return true;
}

void MeshtasticBleService::clearToPhoneQueue()
{
    read_waiting_.store(false);
    pending_to_phone_valid_ = false;
    pending_to_phone_ = Frame{};
    from_radio_preloaded_valid_ = false;
    from_radio_preloaded_ = Frame{};
    from_radio_consume_pending_ = false;
    noInterrupts();
    to_phone_head_ = 0;
    to_phone_tail_ = 0;
    to_phone_count_ = 0;
    interrupts();
}

void MeshtasticBleService::prepareReadableFromRadio()
{
    if (from_radio_preloaded_valid_)
    {
        return;
    }

    Frame frame{};
    if (!popQueuedToPhoneFrame(&frame))
    {
        return;
    }

    from_radio_.write(frame.buf.data(), frame.len);
    from_radio_preloaded_ = frame;
    from_radio_preloaded_valid_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] preload from_num=%08lX len=%u q=%u",
               static_cast<unsigned long>(frame.from_num),
               static_cast<unsigned>(frame.len),
               static_cast<unsigned>(to_phone_count_));
}

bool MeshtasticBleService::hasReadableFromRadio() const
{
    return from_radio_preloaded_valid_;
}

void MeshtasticBleService::markReadableFromRadioConsumed()
{
    from_radio_consume_pending_ = false;

    if (!from_radio_preloaded_valid_)
    {
        pending_from_radio_empty_log_ = true;
        return;
    }

    pending_from_radio_read_len_ = static_cast<uint16_t>(from_radio_preloaded_.len);
    pending_from_radio_read_from_num_ = from_radio_preloaded_.from_num;
    pending_from_radio_read_log_ = true;
    from_radio_preloaded_valid_ = false;
    from_radio_preloaded_ = Frame{};
    uint8_t empty = 0;
    from_radio_.write(&empty, 0);
}

void MeshtasticBleService::handleConnectEvent(uint16_t conn_handle)
{
    connected_ = true;
    conn_handle_ = conn_handle;
    from_num_notify_enabled_ = false;
    clearToPhoneQueue();
    pairing_request_pending_ = true;
    pending_pairing_conn_handle_ = conn_handle;
    pending_connect_conn_handle_ = conn_handle;
    pending_connect_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] link-up conn=%u adv=%u",
               static_cast<unsigned>(conn_handle),
               Bluefruit.Advertising.isRunning() ? 1U : 0U);
}

void MeshtasticBleService::handleDisconnectEvent(uint16_t conn_handle)
{
    connected_ = false;
    from_num_notify_enabled_ = false;
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    if (phone_session_)
    {
        phone_session_->close();
    }
    pending_passkey_.store(0);
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_disconnect_conn_handle_ = conn_handle;
    pending_disconnect_log_ = true;
}

void MeshtasticBleService::handleFromNumCccdWrite(uint16_t conn_handle, uint16_t value)
{
    conn_handle_ = conn_handle;
    from_num_notify_enabled_ = (value != 0U);
    pending_from_num_cccd_conn_handle_ = conn_handle;
    pending_from_num_cccd_value_ = value;
    pending_from_num_cccd_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_num subscribed=%u conn=%u value=0x%04X",
               from_num_notify_enabled_ ? 1U : 0U,
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned>(value));
}

void MeshtasticBleService::handlePairPasskeyDisplay(uint16_t conn_handle, const uint8_t passkey[6], bool match_request)
{
    const uint32_t parsed = parsePasskeyDigits(passkey);
    pending_passkey_.store(parsed);
    (void)match_request;
    (void)conn_handle;
}

void MeshtasticBleService::handlePairComplete(uint16_t conn_handle, uint8_t auth_status)
{
    pending_passkey_.store(0);
    pending_pair_complete_conn_handle_ = conn_handle;
    pending_pair_complete_status_ = auth_status;
    pending_pair_complete_log_ = true;
}

void MeshtasticBleService::handleSecured(uint16_t conn_handle)
{
    pending_passkey_.store(0);
    pending_secured_conn_handle_ = conn_handle;
    pending_secured_log_ = true;
}

bool MeshtasticBleService::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }

    *out = BlePairingStatus{};
    out->available = ble_config_.enabled;
    out->requires_passkey = ble_config_.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    out->is_fixed_pin = ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    out->is_connected = isBleConnected();
    out->passkey = effectivePasskey();
    out->is_pairing_active = out->requires_passkey && out->passkey != 0;
    return true;
}

bool MeshtasticBleService::isBleConnected() const
{
    return connected_ && Bluefruit.connected();
}

void MeshtasticBleService::notifyFromNum(uint32_t from_num)
{
    pending_from_num_ = from_num;
    pending_from_num_valid_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_num pending=%08lX", static_cast<unsigned long>(from_num));
}

void MeshtasticBleService::flushPendingFromNumNotify()
{
    if (!pending_from_num_valid_)
    {
        return;
    }

    const uint32_t from_num = pending_from_num_;
    pending_from_num_valid_ = false;

    if (!active_ || !connected_)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] from_num skip=%08lX reason=inactive active=%u connected=%u",
                   static_cast<unsigned long>(from_num),
                   active_ ? 1U : 0U,
                   connected_ ? 1U : 0U);
        return;
    }

    if (!from_num_notify_enabled_ || conn_handle_ == BLE_CONN_HANDLE_INVALID)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] from_num skip=%08lX reason=not-subscribed notify=%u conn=%u",
                   static_cast<unsigned long>(from_num),
                   from_num_notify_enabled_ ? 1U : 0U,
                   static_cast<unsigned>(conn_handle_));
        return;
    }

    from_num_.write32(from_num);
    const bool ok = from_num_.notify32(conn_handle_, from_num);
    bleLogBoth("[BLE][nrf52][mt][flow] from_num notify=%08lX conn=%u ok=%u cccd=0x%04X",
               static_cast<unsigned long>(from_num),
               static_cast<unsigned>(conn_handle_),
               ok ? 1U : 0U,
               static_cast<unsigned>(from_num_.getCccd(conn_handle_)));
    if (!ok && Bluefruit.connected())
    {
        const bool fallback_ok = from_num_.notify32(from_num);
        bleLogBoth("[BLE][nrf52][mt][flow] from_num notify fallback ok=%u", fallback_ok ? 1U : 0U);
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
    out->bluetooth.pin = effectivePasskey();
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
    Serial2.printf("[BLE][nrf52][mt] saveModuleConfig mqtt enabled=%u proxy=%u enc=%u root=%s\n",
                   config.has_mqtt && config.mqtt.enabled ? 1U : 0U,
                   config.has_mqtt && config.mqtt.proxy_to_client_enabled ? 1U : 0U,
                   config.has_mqtt ? (config.mqtt.encryption_enabled ? 1U : 0U) : 1U,
                   config.has_mqtt ? config.mqtt.root : "");
    module_config_ = config;
    syncMqttProxySettings();
    Serial2.printf("[BLE][nrf52][mt] saveModuleConfig done\n");
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

void MeshtasticBleService::applyBleSecurity()
{
    pending_passkey_.store(0);

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        Bluefruit.Security.setMITM(false);
        Bluefruit.Security.setIOCaps(false, false, false);
        Bluefruit.Security.setPairPasskeyCallback(nullptr);
        Bluefruit.Security.setPairCompleteCallback(nullptr);
        Bluefruit.Security.setSecuredCallback(nullptr);
        Serial2.printf("[BLE][nrf52][mt] security mode=no_pin\n");
        return;
    }

    Bluefruit.Security.setIOCaps(true, false, false);
    Bluefruit.Security.setMITM(true);
    Bluefruit.Security.setPairPasskeyCallback(onPairPasskeyDisplay);
    Bluefruit.Security.setPairCompleteCallback(onPairComplete);
    Bluefruit.Security.setSecuredCallback(onSecured);

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
    {
        const uint32_t fixed_pin = ble_config_.fixed_pin != 0 ? ble_config_.fixed_pin : kDefaultBleFixedPin;
        ble_config_.fixed_pin = fixed_pin;
        char digits[7] = {};
        std::snprintf(digits, sizeof(digits), "%06lu", static_cast<unsigned long>(fixed_pin));
        (void)Bluefruit.Security.setPIN(digits);
        Serial2.printf("[BLE][nrf52][mt] security mode=fixed pin=%s\n", digits);
        return;
    }

    Serial2.printf("[BLE][nrf52][mt] security mode=random_pin\n");
}

void MeshtasticBleService::requestPairingIfNeeded(uint16_t conn_handle)
{
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        return;
    }
    // Bluefruit on nRF52 is more stable if we let the central initiate pairing
    // implicitly when it touches MITM-protected characteristics, instead of
    // forcing requestPairing() immediately after connect.
    Serial2.printf("[BLE][nrf52][mt] pairing wait-for-central conn=%u mode=%u\n",
                   static_cast<unsigned>(conn_handle),
                   static_cast<unsigned>(ble_config_.mode));
}

void MeshtasticBleService::logDeferredBleEvents()
{
    if (pending_connect_log_)
    {
        pending_connect_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] connected conn=%u mode=%u",
                   static_cast<unsigned>(pending_connect_conn_handle_),
                   static_cast<unsigned>(ble_config_.mode));
    }

    if (pending_disconnect_log_)
    {
        pending_disconnect_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] disconnected conn=%u",
                   static_cast<unsigned>(pending_disconnect_conn_handle_));
    }

    if (pending_from_num_cccd_log_)
    {
        pending_from_num_cccd_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_num cccd conn=%u value=0x%04X enabled=%u",
                   static_cast<unsigned>(pending_from_num_cccd_conn_handle_),
                   static_cast<unsigned>(pending_from_num_cccd_value_),
                   from_num_notify_enabled_ ? 1U : 0U);
    }

    if (pending_pair_complete_log_)
    {
        pending_pair_complete_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] pair complete status=%u conn=%u",
                   static_cast<unsigned>(pending_pair_complete_status_),
                   static_cast<unsigned>(pending_pair_complete_conn_handle_));
    }

    if (pending_secured_log_)
    {
        pending_secured_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] secured conn=%u",
                   static_cast<unsigned>(pending_secured_conn_handle_));
    }

    if (pending_from_radio_read_log_)
    {
        pending_from_radio_read_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_radio read len=%u from_num=%08lX",
                   static_cast<unsigned>(pending_from_radio_read_len_),
                   static_cast<unsigned long>(pending_from_radio_read_from_num_));
    }

    if (pending_from_radio_empty_log_)
    {
        pending_from_radio_empty_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_radio read empty");
    }
}

uint32_t MeshtasticBleService::effectivePasskey() const
{
    const uint32_t pending = pending_passkey_.load();
    if (pending != 0)
    {
        return pending;
    }
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
    {
        return ble_config_.fixed_pin;
    }
    return 0;
}

} // namespace ble
