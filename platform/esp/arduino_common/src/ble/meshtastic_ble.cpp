#include "ble/meshtastic_ble.h"

#include "app/app_config.h"
#include "ble/ble_uuids.h"
#include "board/BoardBase.h"
#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_core.h"
#include "chat/domain/contact_types.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/time_utils.h"
#include "chat/usecase/contact_service.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "screen_sleep.h"
#include "ui/widgets/ble_pairing_popup.h"
#include <Arduino.h>
#include <NimBLE2904.h>
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <sys/time.h>

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "platform/esp/idf_common/tab5_rtc_runtime.h"
#endif

namespace ble
{

namespace
{
// Lightweight BLE/Meshtastic debug logging. Always uses Serial so you can
// see exchange_user_info / exchange_positions behavior in the monitor.
inline void ble_log(const char* fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[BLE] %s\n", buf);
}
constexpr size_t kMaxFromRadio = meshtastic_FromRadio_size;
constexpr size_t kMaxToRadio = meshtastic_ToRadio_size;
constexpr uint16_t kPreferredBleMtu = 517;
constexpr bool kEnableFromRadioSync = false;

const char* pairingModeName(meshtastic_Config_BluetoothConfig_PairingMode mode)
{
    switch (mode)
    {
    case meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN:
        return "random_pin";
    case meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN:
        return "fixed_pin";
    case meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN:
        return "no_pin";
    default:
        return "unknown";
    }
}

constexpr uint8_t kDefaultPskIndex = 1;
constexpr uint8_t kDefaultPsk[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

constexpr const char* kBlePrefsNs = "mt_ble";
constexpr const char* kBleEnabledKey = "enabled";
constexpr const char* kBleModeKey = "mode";
constexpr const char* kBlePinKey = "pin";

constexpr const char* kModulePrefsNs = "mt_mod";
constexpr const char* kModuleBlobKey = "cfg";

uint8_t xorHash(const uint8_t* data, size_t len)
{
    uint8_t out = 0;
    for (size_t i = 0; i < len; ++i)
    {
        out ^= data[i];
    }
    return out;
}

uint8_t computeChannelHash(const char* name, const uint8_t* key, size_t key_len)
{
    uint8_t h = xorHash(reinterpret_cast<const uint8_t*>(name), strlen(name));
    if (key && key_len > 0)
    {
        h ^= xorHash(key, key_len);
    }
    return h;
}

bool isValidBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}

bool isZeroKey(const uint8_t* key, size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0)
        {
            return false;
        }
    }
    return true;
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

void fillUserId(char* dst, size_t dst_len, uint32_t node_id)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    snprintf(dst, dst_len, "!%08lX", static_cast<unsigned long>(node_id));
}

template <typename T>
void zeroInit(T& value)
{
    memset(&value, 0, sizeof(value));
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

uint8_t channelIndexFromId(chat::ChannelId ch)
{
    return (ch == chat::ChannelId::SECONDARY) ? 1 : 0;
}

chat::ChannelId channelIdFromIndex(uint8_t idx)
{
    return (idx == 1) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;
}

meshtastic_Config_DeviceConfig_Role roleFromConfig(uint8_t role)
{
    switch (role)
    {
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Router):
        return meshtastic_Config_DeviceConfig_Role_ROUTER;
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Repeater):
        return meshtastic_Config_DeviceConfig_Role_REPEATER;
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Sensor):
        return meshtastic_Config_DeviceConfig_Role_SENSOR;
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Tracker):
        return meshtastic_Config_DeviceConfig_Role_TRACKER;
    default:
        return meshtastic_Config_DeviceConfig_Role_CLIENT;
    }
}

} // namespace

class MeshtasticServerCallbacks : public NimBLEServerCallbacks
{
  public:
    explicit MeshtasticServerCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    uint32_t onPassKeyDisplay() override
    {
        uint32_t passkey = owner_.ble_config_.fixed_pin;
        if (owner_.ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN ||
            !isValidBlePin(passkey))
        {
            passkey = static_cast<uint32_t>(random(100000, 1000000));
        }
        owner_.pending_passkey_.store(passkey);
        ble_log("pairing passkey=%06lu", static_cast<unsigned long>(passkey));
        return passkey;
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
    {
        ble_log("auth complete bonded=%u enc=%u auth=%u handle=%u",
                connInfo.isBonded() ? 1U : 0U,
                connInfo.isEncrypted() ? 1U : 0U,
                connInfo.isAuthenticated() ? 1U : 0U,
                static_cast<unsigned>(connInfo.getConnHandle()));
        owner_.pending_passkey_.store(0);
    }

    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override
    {
        (void)server;
        owner_.connected_ = true;
        owner_.conn_handle_ = connInfo.getConnHandle();
        owner_.conn_handle_valid_ = true;
        owner_.read_waiting_.store(false);
        owner_.from_num_subscribed_ = false;
        owner_.from_radio_sync_subscribed_ = false;
        owner_.from_radio_sync_sub_value_ = 0;
        owner_.last_to_radio_len_ = 0;
        memset(owner_.last_to_radio_.data(), 0, owner_.last_to_radio_.size());
        owner_.closePhoneSession();
        if (owner_.server_)
        {
            owner_.server_->updateConnParams(owner_.conn_handle_, 6, 12, 0, 200);
        }
        owner_.refreshBatteryLevel(false);
        ble_log("connected; uuid=%s addr=%s mtu=%u bonded=%u",
                MESH_SERVICE_UUID,
                connInfo.getAddress().toString().c_str(),
                static_cast<unsigned>(connInfo.getMTU()),
                connInfo.isBonded() ? 1U : 0U);
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        owner_.connected_ = false;
        owner_.conn_handle_valid_ = false;
        owner_.conn_handle_ = 0;
        owner_.read_waiting_.store(false);
        owner_.from_num_subscribed_ = false;
        owner_.from_radio_sync_subscribed_ = false;
        owner_.from_radio_sync_sub_value_ = 0;
        owner_.pending_passkey_.store(0);
        owner_.last_to_radio_len_ = 0;
        memset(owner_.last_to_radio_.data(), 0, owner_.last_to_radio_.size());
        owner_.closePhoneSession();
        owner_.clearQueues();
        owner_.startAdvertising();
        ble_log("disconnected reason=%d; advertising restarted uuid=%s", reason, MESH_SERVICE_UUID);
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticToRadioCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshtasticToRadioCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
        if (!characteristic)
        {
            return;
        }
        NimBLEAttValue val = characteristic->getValue();
        if (val.length() == 0 || val.length() > kMaxToRadio)
        {
            return;
        }
        if (owner_.last_to_radio_len_ == val.length() &&
            memcmp(owner_.last_to_radio_.data(), val.data(), val.length()) == 0)
        {
            ble_log("fromPhone drop duplicate len=%u", static_cast<unsigned>(val.length()));
            return;
        }
        if (owner_.from_phone_mutex_ &&
            xSemaphoreTake(owner_.from_phone_mutex_, portMAX_DELAY) == pdTRUE)
        {
            if (owner_.from_phone_len_ < MeshtasticBleService::kFromPhoneQueueDepth)
            {
                owner_.from_phone_queue_[owner_.from_phone_len_] = val;
                owner_.from_phone_len_++;
                owner_.last_to_radio_len_ = val.length();
                memcpy(owner_.last_to_radio_.data(), val.data(), val.length());
            }
            else
            {
                ble_log("fromPhone drop len=%u (queue full)", static_cast<unsigned>(val.length()));
            }
            xSemaphoreGive(owner_.from_phone_mutex_);
        }
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticFromRadioCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshtasticFromRadioCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
        if (!characteristic)
        {
            return;
        }
        MeshtasticBleService::Frame frame;
        bool has_frame = false;

        auto tryPopFrame = [&]() -> bool
        {
            if (owner_.to_phone_mutex_ &&
                xSemaphoreTake(owner_.to_phone_mutex_, pdMS_TO_TICKS(20)) == pdTRUE)
            {
                if (owner_.to_phone_len_ > 0)
                {
                    frame = owner_.to_phone_queue_[0];
                    for (size_t i = 1; i < owner_.to_phone_len_; ++i)
                    {
                        owner_.to_phone_queue_[i - 1] = owner_.to_phone_queue_[i];
                    }
                    owner_.to_phone_len_--;
                    has_frame = true;
                }
                xSemaphoreGive(owner_.to_phone_mutex_);
            }
            return has_frame;
        };

        if (!tryPopFrame() && owner_.shouldBlockOnRead())
        {
            owner_.read_waiting_.store(true);
            int tries = 0;
            while (!has_frame && owner_.read_waiting_.load() && tries < 4000)
            {
                delay((tries < 20) ? 1 : 5);
                tryPopFrame();
                tries++;
            }
            owner_.read_waiting_.store(false);
            if (!has_frame)
            {
                ble_log("fromRadio wait timeout; return empty");
            }
        }

        if (has_frame)
        {
            characteristic->setValue(frame.buf.data(), frame.len);
            ble_log("fromRadio read len=%u", static_cast<unsigned>(frame.len));
        }
        else
        {
            // Important: clear characteristic value when queue is empty.
            // The mobile app drains by repeated READ until it gets zero-length data.
            characteristic->setValue(reinterpret_cast<const uint8_t*>(""), 0);
            ble_log("fromRadio read empty");
        }
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticNotifyStateCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshtasticNotifyStateCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onSubscribe(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override
    {
        (void)connInfo;
        if (characteristic == owner_.from_num_)
        {
            owner_.from_num_subscribed_ = (subValue != 0);
            ble_log("fromNum subscribe sub=0x%X", static_cast<unsigned>(subValue));
            return;
        }
        if (characteristic == owner_.from_radio_sync_)
        {
            owner_.from_radio_sync_subscribed_ = (subValue != 0);
            owner_.from_radio_sync_sub_value_ = subValue;
            ble_log("fromRadioSync subscribe sub=0x%X", static_cast<unsigned>(subValue));
        }
    }

  private:
    MeshtasticBleService& owner_;
};

MeshtasticBleService::MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name)
{
}

MeshtasticBleService::~MeshtasticBleService()
{
    stop();
}

void MeshtasticBleService::start()
{
    loadBleConfig();
    loadModuleConfig();
    ble_log("security enabled=%u mode=%s fixed_pin=%06lu",
            ble_config_.enabled ? 1U : 0U,
            pairingModeName(ble_config_.mode),
            static_cast<unsigned long>(ble_config_.fixed_pin));
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(kPreferredBleMtu);
    applyBleSecurity();

    from_phone_mutex_ = xSemaphoreCreateMutex();
    to_phone_mutex_ = xSemaphoreCreateMutex();

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(new MeshtasticServerCallbacks(*this));

    setupService();
    startAdvertising();

    ctx_.getChatService().addIncomingTextObserver(this);
    ctx_.getChatService().addOutgoingTextObserver(this);
    if (auto* team = ctx_.getTeamService())
    {
        team->addIncomingDataObserver(this);
    }

    phone_session_.reset(new MeshtasticPhoneSession(ctx_, *this, this));
}

void MeshtasticBleService::stop()
{
    ctx_.getChatService().removeIncomingTextObserver(this);
    ctx_.getChatService().removeOutgoingTextObserver(this);
    if (auto* team = ctx_.getTeamService())
    {
        team->removeIncomingDataObserver(this);
    }

    if (server_)
    {
        server_->getAdvertising()->stop();
    }

    if (from_phone_mutex_)
    {
        vSemaphoreDelete(from_phone_mutex_);
        from_phone_mutex_ = nullptr;
    }
    if (to_phone_mutex_)
    {
        vSemaphoreDelete(to_phone_mutex_);
        to_phone_mutex_ = nullptr;
    }

    clearQueues();
    service_ = nullptr;
    battery_service_ = nullptr;
    server_ = nullptr;
    to_radio_ = nullptr;
    from_radio_ = nullptr;
    from_radio_sync_ = nullptr;
    from_num_ = nullptr;
    log_radio_ = nullptr;
    battery_level_ = nullptr;
    last_battery_level_ = -2;
    ::ui::BlePairingPopup::hide();
    pending_passkey_.store(0);
    connected_ = false;
    conn_handle_valid_ = false;
    conn_handle_ = 0;
    read_waiting_.store(false);
    from_num_subscribed_ = false;
    from_radio_sync_subscribed_ = false;
    from_radio_sync_sub_value_ = 0;
    last_to_radio_len_ = 0;
    memset(last_to_radio_.data(), 0, last_to_radio_.size());
    phone_session_.reset();
}

void MeshtasticBleService::update()
{
    const bool show_pin = (ble_config_.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN);
    const uint32_t passkey = show_pin ? pending_passkey_.load() : 0;
    ::ui::BlePairingPopup::update(
        passkey,
        ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN,
        device_name_.c_str());
    refreshBatteryLevel(true);
    syncMqttProxySettings();
    if (phone_session_)
    {
        // Drain mesh adapter app-data events (including synthetic ROUTING_APP
        // ACK/NAK results) into the phone session before BLE reads them.
        phone_session_->pumpIncomingAppData();
    }
    handleFromPhone();
    handleToPhone();
}

void MeshtasticBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        phone_session_->onIncomingText(msg);
        if (phone_session_->isSendingPackets())
        {
            // Match Meshtastic BLE flow: in steady state, notify phone when new data becomes available.
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::onOutgoingText(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        ble_log("local text mirror id=%lu from=%08lX to=%08lX len=%u",
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

void MeshtasticBleService::onIncomingData(const chat::MeshIncomingData& msg)
{
    if (phone_session_)
    {
        phone_session_->onIncomingData(msg);
        if (phone_session_->isSendingPackets())
        {
            // Match Meshtastic BLE flow: in steady state, notify phone when new data becomes available.
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::setupService()
{
    service_ = server_->createService(MESH_SERVICE_UUID);
    const bool no_pin = (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN);
    if (no_pin)
    {
        to_radio_ = service_->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE);
        from_radio_ = service_->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
        from_num_ = service_->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        log_radio_ =
            service_->createCharacteristic(LOGRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 512U);
    }
    else
    {
        to_radio_ = service_->createCharacteristic(
            TORADIO_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
        from_radio_ = service_->createCharacteristic(
            FROMRADIO_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
        from_num_ = service_->createCharacteristic(
            FROMNUM_UUID,
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN |
                NIMBLE_PROPERTY::READ_ENC);
        log_radio_ = service_->createCharacteristic(
            LOGRADIO_UUID,
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN |
                NIMBLE_PROPERTY::READ_ENC,
            512U);
    }
    if (kEnableFromRadioSync)
    {
        from_radio_sync_ =
            service_->createCharacteristic(FROMRADIOSYNC_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    }
    else
    {
        from_radio_sync_ = nullptr;
    }

    to_radio_->setCallbacks(new MeshtasticToRadioCallbacks(*this));
    from_radio_->setCallbacks(new MeshtasticFromRadioCallbacks(*this));
    from_num_->setCallbacks(new MeshtasticNotifyStateCallbacks(*this));
    if (from_radio_sync_)
    {
        from_radio_sync_->setCallbacks(new MeshtasticNotifyStateCallbacks(*this));
    }

    service_->start();

    battery_service_ = server_->createService(NimBLEUUID((uint16_t)0x180F));
    battery_level_ = battery_service_->createCharacteristic(
        (uint16_t)0x2A19,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
        1);
    if (auto* desc = battery_level_->create2904())
    {
        desc->setFormat(NimBLE2904::FORMAT_UINT8);
        desc->setNamespace(1);
        desc->setUnit(0x27AD);
    }
    battery_service_->start();
    refreshBatteryLevel(false);
}

void MeshtasticBleService::startAdvertising()
{
    if (!server_)
    {
        return;
    }
    NimBLEAdvertising* adv = server_->getAdvertising();
    adv->reset();
    const bool mesh_service_ok = adv->addServiceUUID(MESH_SERVICE_UUID);
    const bool battery_service_ok = adv->addServiceUUID(NimBLEUUID((uint16_t)0x180F));
    adv->enableScanResponse(true);
    adv->setMinInterval(500);
    adv->setMaxInterval(1000);
    const bool name_ok = adv->setName(device_name_);
    const bool start_ok = adv->start();
    ble_log("advertising uuid=%s batt=%u name=%s mesh_ok=%u name_ok=%u start_ok=%u",
            MESH_SERVICE_UUID,
            battery_service_ok ? 1U : 0U,
            device_name_.c_str(),
            mesh_service_ok ? 1U : 0U,
            name_ok ? 1U : 0U,
            start_ok ? 1U : 0U);
}

void MeshtasticBleService::requestHighThroughputConnection()
{
    if (!server_ || !conn_handle_valid_)
    {
        return;
    }
    server_->updateConnParams(conn_handle_, 6, 12, 0, 600);
    ble_log("request conn high-throughput handle=%u", static_cast<unsigned>(conn_handle_));
}

void MeshtasticBleService::requestLowerPowerConnection()
{
    if (!server_ || !conn_handle_valid_)
    {
        return;
    }
    server_->updateConnParams(conn_handle_, 24, 40, 2, 600);
    ble_log("request conn low-power handle=%u", static_cast<unsigned>(conn_handle_));
}

void MeshtasticBleService::handleFromPhone()
{
    if (!phone_session_)
    {
        return;
    }
    if (!from_phone_mutex_)
    {
        return;
    }

    while (from_phone_len_ > 0)
    {
        NimBLEAttValue val;
        if (xSemaphoreTake(from_phone_mutex_, 0) == pdTRUE)
        {
            val = from_phone_queue_[0];
            for (size_t i = 1; i < from_phone_len_; ++i)
            {
                from_phone_queue_[i - 1] = from_phone_queue_[i];
            }
            from_phone_len_--;
            xSemaphoreGive(from_phone_mutex_);
        }
        else
        {
            break;
        }
        phone_session_->handleToRadio(reinterpret_cast<const uint8_t*>(val.data()), val.length());
    }
}

void MeshtasticBleService::handleToPhone()
{
    if (!phone_session_ || !to_phone_mutex_)
    {
        return;
    }
    const bool sync_active = connected_ && from_radio_sync_ && from_radio_sync_subscribed_;
    const bool waiting_for_read = read_waiting_.load();
    const bool in_send_packets = phone_session_->isSendingPackets();
    const bool can_preload = connected_ && !in_send_packets;

    Frame frame;
    uint32_t from_num = 0;

    if (pending_to_phone_valid_)
    {
        frame = pending_to_phone_;
        from_num = pending_to_phone_from_num_;
    }
    else
    {
        if (!waiting_for_read && !can_preload)
        {
            return;
        }
        if (!sync_active && to_phone_len_ >= kToPhoneQueueDepth)
        {
            return;
        }
        MeshtasticBleFrame session_frame{};
        if (!phone_session_->popToPhone(&session_frame))
        {
            if (waiting_for_read && in_send_packets)
            {
                // In STATE_SEND_PACKETS, a 0-byte FromRadio read is valid and is
                // preferable to stalling the client until a long timeout expires.
                read_waiting_.store(false);
            }
            return;
        }
        frame.len = session_frame.len;
        std::memcpy(frame.buf.data(), session_frame.buf, session_frame.len);
        from_num = session_frame.from_num;
    }

    if (sync_active)
    {
        bool sent = false;
        // subValue bit 1 => notify, bit 2 => indicate.
        if ((from_radio_sync_sub_value_ & 0x0002U) != 0U)
        {
            sent = from_radio_sync_->indicate(frame.buf.data(), frame.len);
        }
        if (!sent)
        {
            sent = from_radio_sync_->notify(frame.buf.data(), frame.len);
        }
        if (sent)
        {
            pending_to_phone_valid_ = false;
            ble_log("fromRadioSync tx from_num=%lu len=%u",
                    static_cast<unsigned long>(from_num),
                    static_cast<unsigned>(frame.len));
            return;
        }

        // If the app is using FromRadioSync it will not drain legacy FromRadio reads.
        // Keep the frame pending and retry sync on the next update tick.
        pending_to_phone_ = frame;
        pending_to_phone_from_num_ = from_num;
        pending_to_phone_valid_ = true;
        ble_log("fromRadioSync tx busy; defer from_num=%lu len=%u",
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(frame.len));
        return;
    }

    bool queued = false;
    if (xSemaphoreTake(to_phone_mutex_, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        if (to_phone_len_ < kToPhoneQueueDepth)
        {
            to_phone_queue_[to_phone_len_++] = frame;
            queued = true;
        }
        xSemaphoreGive(to_phone_mutex_);
    }

    if (queued)
    {
        pending_to_phone_valid_ = false;
        ble_log("toPhone enqueue from_num=%lu len=%u q=%u",
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(frame.len),
                static_cast<unsigned>(to_phone_len_));
        if (in_send_packets && !waiting_for_read)
        {
            notifyFromNum(from_num);
        }
        else if (can_preload && !sync_active && to_phone_len_ < kToPhoneQueueDepth)
        {
            // Match official Meshtastic BLE behavior during config/setup:
            // keep preloading until the queue is full so client reads don't see
            // transient empty responses between config packets.
            handleToPhone();
        }
    }
    else
    {
        pending_to_phone_ = frame;
        pending_to_phone_from_num_ = from_num;
        pending_to_phone_valid_ = true;
        ble_log("toPhone enqueue defer from_num=%lu len=%u (queue full/busy)",
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(frame.len));
    }
}

bool MeshtasticBleService::shouldBlockOnRead() const
{
    if (!connected_ || !phone_session_)
    {
        return false;
    }
    // Match official Meshtastic BLE behavior:
    // - during steady-state STATE_SEND_PACKETS, the client reads after FROMNUM notify,
    //   so onRead should wait for the main loop to produce the next FromRadio frame
    // - during config/setup, iOS drains FROMRADIO until it sees a 0-byte read; if we
    //   return empty before CONFIG_COMPLETE the app can stop draining and appear stuck
    //   in a "not connected" state. Therefore config flow must also block briefly.
    return phone_session_->isSendingPackets() || phone_session_->isConfigFlowActive();
}

void MeshtasticBleService::notifyFromNum(uint32_t value)
{
    if (!from_num_)
    {
        return;
    }
    if (!from_num_subscribed_)
    {
        ble_log("fromNum skip notify value=%lu (not subscribed)", static_cast<unsigned long>(value));
        return;
    }
    const uint32_t notify_value = value;
    uint8_t val[4] = {
        static_cast<uint8_t>(notify_value & 0xFFU),
        static_cast<uint8_t>((notify_value >> 8) & 0xFFU),
        static_cast<uint8_t>((notify_value >> 16) & 0xFFU),
        static_cast<uint8_t>((notify_value >> 24) & 0xFFU),
    };
    from_num_->setValue(val, sizeof(val));
    bool has_connection = connected_;
    if (server_)
    {
        has_connection = (server_->getConnectedCount() > 0);
    }
    if (has_connection)
    {
        ble_log("fromNum notify value=%lu", static_cast<unsigned long>(notify_value));
        from_num_->notify();
    }
    else
    {
        ble_log("fromNum skip notify value=%lu (no connection)", static_cast<unsigned long>(notify_value));
    }
}

void MeshtasticBleService::clearQueues()
{
    read_waiting_.store(false);
    pending_to_phone_valid_ = false;
    pending_to_phone_from_num_ = 0;

    if (from_phone_mutex_)
    {
        if (xSemaphoreTake(from_phone_mutex_, 0) == pdTRUE)
        {
            from_phone_len_ = 0;
            xSemaphoreGive(from_phone_mutex_);
        }
    }
    if (to_phone_mutex_)
    {
        if (xSemaphoreTake(to_phone_mutex_, 0) == pdTRUE)
        {
            to_phone_len_ = 0;
            xSemaphoreGive(to_phone_mutex_);
        }
    }
}

void MeshtasticBleService::closePhoneSession()
{
    if (phone_session_)
    {
        phone_session_->close();
    }
}

} // namespace ble
