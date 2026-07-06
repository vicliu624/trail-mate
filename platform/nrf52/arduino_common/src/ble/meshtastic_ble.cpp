#include "../../include/ble/meshtastic_ble.h"

#include "ble/ble_uuids.h"
#include "ble/meshtastic_ble_observer_bridge.h"
#include "ble/meshtastic_ble_persistence_bridge.h"
#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"
#include "platform/shared/ble/app_config_phone_snapshot_bridge.h"
#include "platform/shared/ble/meshtastic_phone_runtime_bridge.h"

#include <Adafruit_LittleFS.h>
#include <Arduino.h>
#include <InternalFileSystem.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <nrf_soc.h>

extern "C" bool trailmate_nrf52_debug_check_gps_guard(const char* tag) __attribute__((weak));

namespace ble
{
namespace
{
constexpr uint32_t kDefaultBleFixedPin = 654321;
constexpr uint32_t kConfigSaveDebounceMs = 1500UL;
constexpr uint32_t kBleIdleStateLogIntervalMs = 10000UL;
constexpr uint32_t kBleConnectedSessionLogIntervalMs = 30000UL;
constexpr uint32_t kDirectedAdvertisingMs = 3000UL;
constexpr uint16_t kBleAdvertisingFastInterval = 32;
constexpr uint16_t kBleAdvertisingSlowInterval = 668;
constexpr uint16_t kBleAdvertisingFastTimeoutSec = 30;
constexpr uint32_t kPhoneSessionStaleMs = 90000UL;
constexpr uint32_t kPhoneSessionStaleLogIntervalMs = 30000UL;
constexpr uint8_t kFromRadioEmptyInactive = 1;
constexpr uint8_t kFromRadioEmptyNoFrame = 2;
constexpr uint8_t kFromRadioEmptyInvalidFrame = 3;
constexpr uint8_t kSteadyPublishedFromRadioWindow = 1;
constexpr uint32_t kFromRadioReadHoldMs = 250UL;
constexpr uint32_t kUnsetAgeMs = 0xFFFFFFFFUL;

bool usbSerialWritable(std::size_t len)
{
    return static_cast<bool>(Serial) && Serial.dtr() != 0 && Serial.availableForWrite() >= static_cast<int>(len);
}

void bleLogBoth(const char* fmt, ...)
{
    char buffer[192] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (usbSerialWritable(std::strlen(buffer) + 2U))
    {
        Serial.println(buffer);
    }
    Serial2.println(buffer);
}

void probeGpsGuard(const char* tag)
{
    if (trailmate_nrf52_debug_check_gps_guard)
    {
        (void)trailmate_nrf52_debug_check_gps_guard(tag);
    }
}

const char* bleFrameKindName(phone::meshtastic::MeshtasticBleFrameKind kind)
{
    using phone::meshtastic::MeshtasticBleFrameKind;
    switch (kind)
    {
    case MeshtasticBleFrameKind::Config:
        return "config";
    case MeshtasticBleFrameKind::Liveness:
        return "liveness";
    case MeshtasticBleFrameKind::QueueStatus:
        return "queue_status";
    case MeshtasticBleFrameKind::AdminResponse:
        return "admin_response";
    case MeshtasticBleFrameKind::NodeInfo:
        return "node_info";
    case MeshtasticBleFrameKind::Packet:
        return "packet";
    case MeshtasticBleFrameKind::MqttProxy:
        return "mqtt_proxy";
    case MeshtasticBleFrameKind::None:
    default:
        return "none";
    }
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

uint32_t generateBlePairingPasskey()
{
    uint32_t entropy = 0;
    uint8_t available = 0;
    if (sd_rand_application_bytes_available_get(&available) == NRF_SUCCESS && available >= sizeof(entropy) &&
        sd_rand_application_vector_get(reinterpret_cast<uint8_t*>(&entropy), sizeof(entropy)) == NRF_SUCCESS)
    {
        return (entropy % 900000UL) + 100000UL;
    }

    entropy = static_cast<uint32_t>(random(0, 0x7FFFFFFF)) ^ (static_cast<uint32_t>(millis()) << 16U) ^
              static_cast<uint32_t>(micros());
    return (entropy % 900000UL) + 100000UL;
}

void formatBlePasskey(uint32_t passkey, char out[7])
{
    std::snprintf(out, 7, "%06lu", static_cast<unsigned long>(passkey % 1000000UL));
}

bool clearSoftDeviceBlePasskey()
{
    ble_opt_t opt{};
    opt.gap_opt.passkey.p_passkey = nullptr;
    return sd_ble_opt_set(BLE_GAP_OPT_PASSKEY, &opt) == NRF_SUCCESS;
}

const char* gapSecurityStatusName(uint8_t status)
{
    switch (status)
    {
    case BLE_GAP_SEC_STATUS_SUCCESS:
        return "success";
    case BLE_GAP_SEC_STATUS_TIMEOUT:
        return "timeout";
    case BLE_GAP_SEC_STATUS_PASSKEY_ENTRY_FAILED:
        return "passkey_entry_failed";
    case BLE_GAP_SEC_STATUS_OOB_NOT_AVAILABLE:
        return "oob_not_available";
    case BLE_GAP_SEC_STATUS_AUTH_REQ:
        return "auth_req";
    case BLE_GAP_SEC_STATUS_CONFIRM_VALUE:
        return "confirm_value";
    case BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP:
        return "pairing_not_supported";
    case BLE_GAP_SEC_STATUS_ENC_KEY_SIZE:
        return "enc_key_size";
    case BLE_GAP_SEC_STATUS_SMP_CMD_UNSUPPORTED:
        return "smp_cmd_unsupported";
    case BLE_GAP_SEC_STATUS_UNSPECIFIED:
        return "unspecified";
    case BLE_GAP_SEC_STATUS_REPEATED_ATTEMPTS:
        return "repeated_attempts";
    case BLE_GAP_SEC_STATUS_INVALID_PARAMS:
        return "invalid_params";
    case BLE_GAP_SEC_STATUS_DHKEY_FAILURE:
        return "dhkey_failure";
    default:
        return "unknown";
    }
}

const char* bleAddressTypeName(uint8_t type)
{
    switch (type)
    {
    case BLE_GAP_ADDR_TYPE_PUBLIC:
        return "public";
    case BLE_GAP_ADDR_TYPE_RANDOM_STATIC:
        return "random_static";
    case BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE:
        return "private_resolvable";
    case BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_NON_RESOLVABLE:
        return "private_non_resolvable";
    default:
        return "unknown";
    }
}

void formatBleAddress(const ble_gap_addr_t& addr, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  addr.addr[5],
                  addr.addr[4],
                  addr.addr[3],
                  addr.addr[2],
                  addr.addr[1],
                  addr.addr[0]);
}

bool isEmptyBleAddress(const ble_gap_addr_t& addr)
{
    for (uint8_t byte : addr.addr)
    {
        if (byte != 0)
        {
            return false;
        }
    }
    return true;
}

bool isIdentityBleAddressType(uint8_t type)
{
    return type == BLE_GAP_ADDR_TYPE_PUBLIC || type == BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
}

uint32_t ageSinceOrUnset(uint32_t now_ms, uint32_t then_ms)
{
    return then_ms == 0 ? kUnsetAgeMs : now_ms - then_ms;
}

bool readProtoVarint(const uint8_t* data, size_t len, size_t* index, uint64_t* out)
{
    if (!data || !index || !out)
    {
        return false;
    }

    uint64_t value = 0;
    uint8_t shift = 0;
    while (*index < len && shift < 64)
    {
        const uint8_t byte = data[*index];
        ++(*index);
        value |= static_cast<uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0)
        {
            *out = value;
            return true;
        }
        shift = static_cast<uint8_t>(shift + 7U);
    }
    return false;
}

uint32_t peekToRadioVariant(const uint8_t* data, size_t len)
{
    size_t index = 0;
    while (index < len)
    {
        uint64_t key = 0;
        if (!readProtoVarint(data, len, &index, &key))
        {
            return 0;
        }
        const uint32_t field = static_cast<uint32_t>(key >> 3U);
        const uint8_t wire_type = static_cast<uint8_t>(key & 0x07U);
        if (field == meshtastic_ToRadio_packet_tag || field == meshtastic_ToRadio_want_config_id_tag ||
            field == meshtastic_ToRadio_disconnect_tag || field == meshtastic_ToRadio_xmodemPacket_tag ||
            field == meshtastic_ToRadio_mqttClientProxyMessage_tag || field == meshtastic_ToRadio_heartbeat_tag)
        {
            return field;
        }

        uint64_t skip_len = 0;
        switch (wire_type)
        {
        case 0:
            if (!readProtoVarint(data, len, &index, &skip_len))
            {
                return 0;
            }
            break;
        case 1:
            if (len - index < 8)
            {
                return 0;
            }
            index += 8;
            break;
        case 2:
            if (!readProtoVarint(data, len, &index, &skip_len) || skip_len > len - index)
            {
                return 0;
            }
            index += static_cast<size_t>(skip_len);
            break;
        case 5:
            if (len - index < 4)
            {
                return 0;
            }
            index += 4;
            break;
        default:
            return 0;
        }
    }
    return 0;
}

bool blePairingRequiresSecurity(const meshtastic_Config_BluetoothConfig& config)
{
    return config.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
}

SecureMode_t bleCharacteristicSecurityMode(const meshtastic_Config_BluetoothConfig& config)
{
    return blePairingRequiresSecurity(config) ? SECMODE_ENC_NO_MITM : SECMODE_OPEN;
}

SecureMode_t bleServiceSecurityMode(const meshtastic_Config_BluetoothConfig& config)
{
    return blePairingRequiresSecurity(config) ? SECMODE_ENC_WITH_MITM : SECMODE_OPEN;
}

bool loadFirstPeripheralBondPeer(ble_gap_addr_t* out)
{
    if (!out)
    {
        return false;
    }

    namespace lfs = Adafruit_LittleFS_Namespace;
    lfs::File dir(BOND_DIR_PRPH, lfs::FILE_O_READ, InternalFS);
    if (!dir)
    {
        return false;
    }

    lfs::File file(InternalFS);
    while ((file = dir.openNextFile(lfs::FILE_O_READ)))
    {
        if (file.isDirectory())
        {
            file.close();
            continue;
        }

        const int key_len = file.read();
        if (key_len == static_cast<int>(sizeof(bond_keys_t)))
        {
            bond_keys_t keys{};
            const int read_len = file.read(reinterpret_cast<uint8_t*>(&keys), sizeof(keys));
            const ble_gap_addr_t peer = keys.peer_id.id_addr_info;
            if (read_len == static_cast<int>(sizeof(keys)) && isIdentityBleAddressType(peer.addr_type) &&
                !isEmptyBleAddress(peer))
            {
                *out = peer;
                char peer_addr_text[24] = {};
                formatBleAddress(peer, peer_addr_text, sizeof(peer_addr_text));
                bleLogBoth("[BLE][nrf52][mt] bond peer recovered file=%s peer=%s type=%s(0x%02X)",
                           file.name(),
                           peer_addr_text,
                           bleAddressTypeName(peer.addr_type),
                           static_cast<unsigned>(peer.addr_type));
                file.close();
                dir.close();
                return true;
            }
        }

        file.close();
    }

    dir.close();
    return false;
}

const char* disconnectReasonName(uint8_t reason)
{
    switch (reason)
    {
    case BLE_HCI_STATUS_CODE_SUCCESS:
        return "success";
    case BLE_HCI_STATUS_CODE_UNKNOWN_CONNECTION_IDENTIFIER:
        return "unknown_connection";
    case BLE_HCI_AUTHENTICATION_FAILURE:
        return "auth_failure";
    case BLE_HCI_STATUS_CODE_PIN_OR_KEY_MISSING:
        return "pin_or_key_missing";
    case BLE_HCI_CONNECTION_TIMEOUT:
        return "connection_timeout";
    case BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION:
        return "remote_user";
    case BLE_HCI_REMOTE_DEV_TERMINATION_DUE_TO_LOW_RESOURCES:
        return "remote_low_resources";
    case BLE_HCI_REMOTE_DEV_TERMINATION_DUE_TO_POWER_OFF:
        return "remote_power_off";
    case BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION:
        return "local_host";
    case BLE_HCI_CONN_INTERVAL_UNACCEPTABLE:
        return "interval_unacceptable";
    case BLE_HCI_CONN_TERMINATED_DUE_TO_MIC_FAILURE:
        return "mic_failure";
    case BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED:
        return "failed_to_establish";
    default:
        return "unknown";
    }
}

const char* fromRadioEmptyReasonName(uint8_t reason)
{
    switch (reason)
    {
    case kFromRadioEmptyInactive:
        return "inactive";
    case kFromRadioEmptyNoFrame:
        return "no_frame";
    case kFromRadioEmptyInvalidFrame:
        return "invalid_frame";
    default:
        return "unknown";
    }
}

MeshtasticBleService* s_active_service = nullptr;

void onBleConnect(uint16_t conn_handle)
{
    if (s_active_service)
    {
        s_active_service->handleConnectEvent(conn_handle);
    }
}

void onBleDisconnect(uint16_t conn_handle, uint8_t reason)
{
    if (!s_active_service)
    {
        return;
    }
    s_active_service->handleDisconnectEvent(conn_handle, reason);
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
    Bluefruit.begin(1, 0);
    Bluefruit.setTxPower(4);
    Bluefruit.setName(device_name.c_str());
    Bluefruit.Periph.setConnectCallback(onBleConnect);
    Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);
    Bluefruit.Periph.setConnSlaveLatency(0);
    Bluefruit.Periph.setConnInterval(12, 80);
    const ble_gap_addr_t own_addr = Bluefruit.getAddr();
    char own_addr_text[24] = {};
    formatBleAddress(own_addr, own_addr_text, sizeof(own_addr_text));
    bleLogBoth("[BLE][nrf52][mt] bluefruit ready own=%s type=%s(0x%02X) tx_power=%d conn_interval=12..80 "
               "latency=0 name=%s",
               own_addr_text,
               bleAddressTypeName(own_addr.addr_type),
               static_cast<unsigned>(own_addr.addr_type),
               static_cast<int>(Bluefruit.getTxPower()),
               device_name.c_str());
}

bool startAdvertising(::BLEService& service, const ble_gap_addr_t* directed_peer)
{
    const bool directed = directed_peer && isIdentityBleAddressType(directed_peer->addr_type) &&
                          !isEmptyBleAddress(*directed_peer);
    char peer_addr_text[24] = {};
    if (directed)
    {
        formatBleAddress(*directed_peer, peer_addr_text, sizeof(peer_addr_text));
    }

    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    Bluefruit.Advertising.setInterval(kBleAdvertisingFastInterval, kBleAdvertisingSlowInterval);
    Bluefruit.Advertising.setFastTimeout(kBleAdvertisingFastTimeoutSec);
    if (directed)
    {
        Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED);
        Bluefruit.Advertising.setPeerAddress(*directed_peer);
        Bluefruit.Advertising.restartOnDisconnect(false);
        const bool ok = Bluefruit.Advertising.start(kDirectedAdvertisingMs / 1000UL);
        bleLogBoth("[BLE][nrf52][mt] advertising mode=directed peer=%s type=%s(0x%02X) running=%u ok=%u window_ms=%lu",
                   peer_addr_text,
                   bleAddressTypeName(directed_peer->addr_type),
                   static_cast<unsigned>(directed_peer->addr_type),
                   Bluefruit.Advertising.isRunning() ? 1U : 0U,
                   ok ? 1U : 0U,
                   static_cast<unsigned long>(kDirectedAdvertisingMs));
        return ok;
    }

    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    const bool flags_ok = Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    const bool service_ok = Bluefruit.Advertising.addService(service);
    const bool tx_power_ok = Bluefruit.ScanResponse.addTxPower();
    const bool name_ok = Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    const bool ok = Bluefruit.Advertising.start(0);
    bleLogBoth("[BLE][nrf52][mt] advertising mode=undirected running=%u ok=%u restart=1 interval=%u..%u "
               "fast_timeout_s=%u adv_len=%u scan_len=%u flags=%u service=%u scan_tx_power=%u scan_name=%u",
               Bluefruit.Advertising.isRunning() ? 1U : 0U,
               ok ? 1U : 0U,
               static_cast<unsigned>(kBleAdvertisingFastInterval),
               static_cast<unsigned>(kBleAdvertisingSlowInterval),
               static_cast<unsigned>(kBleAdvertisingFastTimeoutSec),
               static_cast<unsigned>(Bluefruit.Advertising.count()),
               static_cast<unsigned>(Bluefruit.ScanResponse.count()),
               flags_ok ? 1U : 0U,
               service_ok ? 1U : 0U,
               tx_power_ok ? 1U : 0U,
               name_ok ? 1U : 0U);
    return ok;
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

    if (request->offset != 0)
    {
        authorizeRead(conn_handle);
        return;
    }

    if (s_active_service)
    {
        s_active_service->handleFromRadioReadRequest(
            conn_handle, request->offset, reinterpret_cast<uintptr_t>(chr));
        return;
    }

    authorizeRead(conn_handle);
}

} // namespace

MeshtasticBleService::MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : phone_facade_(ctx, ble_config_, module_config_, this),
      observer_bridge_(new MeshtasticBleObserverBridge(ctx, *this)),
      device_name_(device_name),
      service_(::BLEUuid(MESH_SERVICE_UUID_128_LE)),
      to_radio_(::BLEUuid(TORADIO_UUID_128_LE)),
      from_radio_(::BLEUuid(FROMRADIO_UUID_128_LE)),
      from_num_(::BLEUuid(FROMNUM_UUID_128_LE)),
      log_radio_(::BLEUuid(LOGRADIO_UUID_128_LE))
{
    ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    ble_config_.enabled = phone_facade_.isBleEnabled();
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    ble_config_.fixed_pin = 0;

    meshtastic_config_bridge::PersistedState persisted{};
    const bool persisted_ok = loadMeshtasticBlePersistedState(&persisted);

    bleLogBoth("[BLE][nrf52][mt] persisted load ok=%u has_bt=%u has_mod=%u",
               persisted_ok ? 1U : 0U,
               persisted.has_bluetooth ? 1U : 0U,
               persisted.has_module ? 1U : 0U);

    logMeshtasticBlePersistenceStatus();

    meshtastic_config_bridge::initializeConfigState(
        persisted, phone_facade_.isBleEnabled(), phone_facade_.getSelfNodeId(), &ble_config_, &module_config_);

    if (persisted.has_bluetooth)
    {
        bleLogBoth("[BLE][nrf52][mt] loaded bluetooth cfg mode=%u pin=%06lu",
                   static_cast<unsigned>(ble_config_.mode),
                   static_cast<unsigned long>(ble_config_.fixed_pin));
    }
    if (persisted.has_module)
    {
        bleLogBoth("[BLE][nrf52][mt] loaded module cfg mqtt enabled=%u proxy=%u root=%s",
                   module_config_.has_mqtt && module_config_.mqtt.enabled ? 1U : 0U,
                   module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled ? 1U : 0U,
                   module_config_.mqtt.root);
    }

    phone_session_.reset(new phone::meshtastic::MeshtasticPhoneSession(phone_facade_,
                                                                       *this,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_));
}

MeshtasticBleService::~MeshtasticBleService()
{
    stop();
}

void MeshtasticBleService::logFromRadioState(const char* tag) const
{
    const uint32_t head_from_num =
        published_from_radio_count_ > 0 ? published_from_radio_[published_from_radio_head_].frame.from_num : 0;
    bleLogBoth("[BLE][nrf52][mt] fromRadio tag=%s publish_req=%u published=%u head=%u tail=%u "
               "head_from_num=%08lX notify_enabled=%d connected=%d "
               "config_active=%d send_packets=%d",
               tag ? tag : "?",
               from_radio_publish_requested_ ? 1U : 0U,
               static_cast<unsigned>(published_from_radio_count_),
               static_cast<unsigned>(published_from_radio_head_),
               static_cast<unsigned>(published_from_radio_tail_),
               static_cast<unsigned long>(head_from_num),
               from_num_notify_enabled_ ? 1 : 0,
               connected_ ? 1 : 0,
               (phone_session_ && phone_session_->isConfigFlowActive()) ? 1 : 0,
               (phone_session_ && phone_session_->isSendingPackets()) ? 1 : 0);
}

void MeshtasticBleService::logSessionState(const char* tag, uint32_t detail)
{
    const uint32_t now_ms = millis();
    const bool gap_connected = Bluefruit.connected();
    bool bonded = false;
    bool secured = false;
    uint16_t mtu = 0;
    uint16_t interval = 0;
    uint16_t latency = 0;
    uint16_t timeout = 0;
    if (conn_handle_ != BLE_CONN_HANDLE_INVALID)
    {
        BLEConnection* connection = Bluefruit.Connection(conn_handle_);
        if (connection)
        {
            bonded = connection->bonded();
            secured = connection->secured();
            mtu = connection->getMtu();
            interval = connection->getConnectionInterval();
            latency = connection->getSlaveLatency();
            timeout = connection->getSupervisionTimeout();
        }
    }

    bleLogBoth("[BLE][nrf52][mt][session] seq=%lu tag=%s detail=%08lX age_ms=%lu connected=%u gap=%u "
               "secured=%u bonded=%u notify=%u cccd=0x%04X cfg=%u send=%u published=%u head=%u tail=%u "
               "pending_to=%u publish_req=%u read_wait=%u mtu=%u ci=%u lat=%u sto=%u "
               "last_sec=%lu last_cccd=%lu last_to=%lu last_hb=%lu last_cfg=%lu last_read=%lu last_notify=%lu",
               static_cast<unsigned long>(ble_session_seq_),
               tag ? tag : "?",
               static_cast<unsigned long>(detail),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, session_started_ms_)),
               connected_ ? 1U : 0U,
               gap_connected ? 1U : 0U,
               secured ? 1U : 0U,
               bonded ? 1U : 0U,
               from_num_notify_enabled_ ? 1U : 0U,
               conn_handle_ != BLE_CONN_HANDLE_INVALID ? static_cast<unsigned>(from_num_.getCccd(conn_handle_)) : 0U,
               (phone_session_ && phone_session_->isConfigFlowActive()) ? 1U : 0U,
               (phone_session_ && phone_session_->isSendingPackets()) ? 1U : 0U,
               static_cast<unsigned>(published_from_radio_count_),
               static_cast<unsigned>(published_from_radio_head_),
               static_cast<unsigned>(published_from_radio_tail_),
               static_cast<unsigned>(pending_to_radio_count_),
               from_radio_publish_requested_ ? 1U : 0U,
               pending_from_radio_read_authorize_ ? 1U : 0U,
               static_cast<unsigned>(mtu),
               static_cast<unsigned>(interval),
               static_cast<unsigned>(latency),
               static_cast<unsigned>(timeout),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_secured_ms_)),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_from_num_cccd_ms_)),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_to_radio_ms_)),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_heartbeat_ms_)),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_want_config_ms_)),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_from_radio_read_ms_)),
               static_cast<unsigned long>(ageSinceOrUnset(now_ms, last_from_num_notify_ms_)));
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
    pending_connect_log_ = false;
    pending_disconnect_log_ = false;
    pending_from_num_cccd_log_ = false;
    pending_pair_complete_log_ = false;
    pending_secured_log_ = false;
    pending_from_radio_auth_log_ = false;
    pending_from_radio_read_log_ = false;
    pending_from_radio_empty_log_ = false;
    clearPendingFromRadioReadAuthorize();
    pending_from_radio_auth_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_from_radio_auth_offset_ = 0;
    pending_from_radio_auth_chr_ = 0;
    pending_from_radio_auth_svc_ = 0;
    pending_from_radio_empty_reason_ = 0;
    pending_from_radio_read_from_num_ = 0;
    pending_from_radio_read_due_ms_ = 0;
    pending_from_radio_read_len_ = 0;
    pending_disconnect_reason_ = 0;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_phone_disconnect_request_ = false;
    directed_advertising_active_ = false;
    directed_advertising_attempted_ = false;
    directed_advertising_until_ms_ = 0;
    last_ble_activity_ms_ = millis();
    ble_session_seq_ = 0;
    session_started_ms_ = 0;
    last_secured_ms_ = 0;
    last_from_num_cccd_ms_ = 0;
    last_to_radio_ms_ = 0;
    last_heartbeat_ms_ = 0;
    last_want_config_ms_ = 0;
    last_from_radio_read_ms_ = 0;
    last_from_num_notify_ms_ = 0;
    next_connected_session_log_ms_ = 0;
    next_ble_idle_log_ms_ = last_ble_activity_ms_ + 3000UL;
    next_liveness_log_ms_ = 0;

    prepareBluefruit(device_name_);
    loadRememberedPhonePeer();
    applyBleSecurity();

    const SecureMode_t service_sec_mode = bleServiceSecurityMode(ble_config_);
    const SecureMode_t char_sec_mode = bleCharacteristicSecurityMode(ble_config_);
    service_.setPermission(service_sec_mode, service_sec_mode);
    service_.begin();
    bleLogBoth("[BLE][nrf52][mt] service begin sec_mode=0x%02X char_sec_mode=0x%02X",
               static_cast<unsigned>(service_sec_mode),
               static_cast<unsigned>(char_sec_mode));

    to_radio_.setProperties(CHR_PROPS_WRITE);
    to_radio_.setPermission(char_sec_mode, char_sec_mode);
    to_radio_.setFixedLen(0);
    to_radio_.setMaxLen(meshtastic_ToRadio_size);
    to_radio_.setWriteCallback(onToRadioWrite, false);
    to_radio_.begin();

    from_radio_.setProperties(CHR_PROPS_READ);
    from_radio_.setPermission(char_sec_mode, SECMODE_NO_ACCESS);
    from_radio_.setFixedLen(0);
    from_radio_.setMaxLen(meshtastic_FromRadio_size);
    from_radio_.setReadAuthorizeCallback(onFromRadioAuthorize, false);
    from_radio_.begin();
    {
        uint8_t empty = 0;
        from_radio_.write(&empty, 0);
    }

    from_num_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    from_num_.setPermission(char_sec_mode, SECMODE_NO_ACCESS);
    from_num_.setFixedLen(4);
    from_num_.write32(0);
    from_num_.setCccdWriteCallback(onFromNumCccdWrite, false);
    from_num_.begin();

    log_radio_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    log_radio_.setPermission(char_sec_mode, SECMODE_NO_ACCESS);
    log_radio_.setFixedLen(0);
    log_radio_.setMaxLen(96);
    log_radio_.begin();
    bleLogBoth("[BLE][nrf52][mt] chars ready");
    bleLogBoth("[BLE][nrf52][mt][mem] svc=%p pop_scratch=%p buf=%p published=%p slots=%u to_radio=%p "
               "from_radio=%p from_num=%p phone=%p",
               static_cast<void*>(this),
               static_cast<void*>(&session_frame_scratch_),
               static_cast<void*>(session_frame_scratch_.buf),
               static_cast<void*>(published_from_radio_),
               static_cast<unsigned>(kPublishedFromRadioCapacity),
               static_cast<void*>(&to_radio_),
               static_cast<void*>(&from_radio_),
               static_cast<void*>(&from_num_),
               static_cast<void*>(phone_session_.get()));
    if (phone_session_)
    {
        phone_session_->debugLogMemoryLayout("ble_start");
    }
    probeGpsGuard("ble_start_after_chars");

    if (observer_bridge_)
    {
        observer_bridge_->registerObservers();
    }

    startPhoneAdvertising(true);
    active_ = true;
    pending_passkey_.store(0);
    syncMqttProxySettings();
    bleLogBoth("[BLE][nrf52][mt] service active");
    logFromRadioState("start_done");
}

void MeshtasticBleService::stop()
{
    if (observer_bridge_)
    {
        observer_bridge_->unregisterObservers();
    }

    disconnectAll();
    Bluefruit.Advertising.stop();
    flushPendingConfigSaves(true);
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
    pending_connect_log_ = false;
    pending_disconnect_log_ = false;
    pending_from_num_cccd_log_ = false;
    pending_pair_complete_log_ = false;
    pending_secured_log_ = false;
    pending_from_radio_auth_log_ = false;
    pending_from_radio_read_log_ = false;
    pending_from_radio_empty_log_ = false;
    clearPendingFromRadioReadAuthorize();
    pending_from_radio_auth_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_from_radio_auth_offset_ = 0;
    pending_from_radio_auth_chr_ = 0;
    pending_from_radio_auth_svc_ = 0;
    pending_from_radio_empty_reason_ = 0;
    pending_from_radio_read_from_num_ = 0;
    pending_from_radio_read_due_ms_ = 0;
    pending_from_radio_read_len_ = 0;
    pending_disconnect_reason_ = 0;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_phone_disconnect_request_ = false;
    directed_advertising_active_ = false;
    directed_advertising_attempted_ = false;
    directed_advertising_until_ms_ = 0;
    session_started_ms_ = 0;
    last_secured_ms_ = 0;
    last_from_num_cccd_ms_ = 0;
    last_to_radio_ms_ = 0;
    last_heartbeat_ms_ = 0;
    last_want_config_ms_ = 0;
    last_from_radio_read_ms_ = 0;
    last_from_num_notify_ms_ = 0;
    next_connected_session_log_ms_ = 0;
    next_ble_idle_log_ms_ = 0;
    next_liveness_log_ms_ = 0;
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

    processPendingGapEvents();
    syncMqttProxySettings();

    if (phone_session_)
    {
        phone_session_->pumpIncomingAppData();
    }

    processPendingPairingRequest();
    processPendingToRadio();
    if (processPendingPhoneDisconnect())
    {
        return;
    }
    flushPendingFromRadioReadAuthorize();
    flushPendingFromNumNotify();
    logDeferredBleEvents();
    flushPendingConfigSaves(false);

    const uint32_t now_ms = millis();
    if (!connected_ && remembered_phone_peer_valid_ && !directed_advertising_attempted_)
    {
        startPhoneAdvertising(true);
    }

    if (directed_advertising_active_ && !connected_ &&
        static_cast<int32_t>(now_ms - directed_advertising_until_ms_) >= 0)
    {
        bleLogBoth("[BLE][nrf52][mt] directed advertising window elapsed; fallback to undirected");
        startPhoneAdvertising(false);
    }

    if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning())
    {
        startPhoneAdvertising(!directed_advertising_attempted_);
    }

    if (!connected_ && static_cast<int32_t>(now_ms - next_ble_idle_log_ms_) >= 0)
    {
        const ble_gap_addr_t own_addr = Bluefruit.getAddr();
        char own_addr_text[24] = {};
        formatBleAddress(own_addr, own_addr_text, sizeof(own_addr_text));
        char peer_addr_text[24] = {};
        if (remembered_phone_peer_valid_)
        {
            formatBleAddress(remembered_phone_peer_, peer_addr_text, sizeof(peer_addr_text));
        }
        bleLogBoth("[BLE][nrf52][mt] idle connected=%u adv=%u directed=%u directed_tried=%u own=%s type=%s peer=%s peer_type=%s peer_bonded=%u notify=%u cfg=%u send=%u name=%s",
                   connected_ ? 1U : 0U,
                   Bluefruit.Advertising.isRunning() ? 1U : 0U,
                   directed_advertising_active_ ? 1U : 0U,
                   directed_advertising_attempted_ ? 1U : 0U,
                   own_addr_text,
                   bleAddressTypeName(own_addr.addr_type),
                   remembered_phone_peer_valid_ ? peer_addr_text : "-",
                   remembered_phone_peer_valid_ ? bleAddressTypeName(remembered_phone_peer_.addr_type) : "-",
                   remembered_phone_peer_bonded_ ? 1U : 0U,
                   from_num_notify_enabled_ ? 1U : 0U,
                   phone_session_ && phone_session_->isConfigFlowActive() ? 1U : 0U,
                   phone_session_ && phone_session_->isSendingPackets() ? 1U : 0U,
                   device_name_.c_str());
        next_ble_idle_log_ms_ = now_ms + kBleIdleStateLogIntervalMs;
    }

    if (connected_ && static_cast<int32_t>(now_ms - next_connected_session_log_ms_) >= 0)
    {
        logSessionState("periodic");
        next_connected_session_log_ms_ = now_ms + kBleConnectedSessionLogIntervalMs;
    }
    checkPhoneSessionLiveness(now_ms);
}

void MeshtasticBleService::handleIncomingTextFromApp(const chat::MeshIncomingText& msg)
{
    if (!phone_session_ || !connected_)
    {
        bleLogBoth("[BLE][nrf52][mt] phone projection skip kind=text reason=not-connected from=%08lX id=%08lX",
                   static_cast<unsigned long>(msg.from),
                   static_cast<unsigned long>(msg.msg_id));
        return;
    }

    if (phone_session_)
    {
        phone_session_->onIncomingText(msg);
    }
}

void MeshtasticBleService::handleOutgoingTextFromApp(const chat::MeshIncomingText& msg)
{
    if (!phone_session_ || !connected_)
    {
        bleLogBoth("[BLE][nrf52][mt] phone projection skip kind=local_text reason=not-connected from=%08lX id=%08lX",
                   static_cast<unsigned long>(msg.from),
                   static_cast<unsigned long>(msg.msg_id));
        return;
    }

    if (phone_session_)
    {
        Serial2.printf("[BLE][nrf52][mt] local text mirror id=%08lX from=%08lX to=%08lX len=%u\n",
                       static_cast<unsigned long>(msg.msg_id),
                       static_cast<unsigned long>(msg.from),
                       static_cast<unsigned long>(msg.to),
                       static_cast<unsigned>(msg.text.size()));
        phone_session_->onIncomingText(msg);
    }
}

void MeshtasticBleService::handleIncomingDataFromApp(const chat::MeshIncomingData& msg)
{
    if (!phone_session_ || !connected_)
    {
        bleLogBoth("[BLE][nrf52][mt] phone projection skip kind=data reason=not-connected from=%08lX to=%08lX pkt=%08lX port=%u len=%u",
                   static_cast<unsigned long>(msg.from),
                   static_cast<unsigned long>(msg.to),
                   static_cast<unsigned long>(msg.packet_id),
                   static_cast<unsigned>(msg.portnum),
                   static_cast<unsigned>(msg.payload.size()));
        return;
    }

    if (phone_session_)
    {
        bleLogBoth("[BLE][nrf52][mt] onIncomingData from=%08lX to=%08lX pkt=%08lX port=%u len=%u",
                   static_cast<unsigned long>(msg.from),
                   static_cast<unsigned long>(msg.to),
                   static_cast<unsigned long>(msg.packet_id),
                   static_cast<unsigned>(msg.portnum),
                   static_cast<unsigned>(msg.payload.size()));
        probeGpsGuard("ble_incoming_data_pre_session");
        phone_session_->onIncomingData(msg);
        probeGpsGuard("ble_incoming_data_post_session");
    }
}

bool MeshtasticBleService::isRunning() const
{
    return active_ && (Bluefruit.connected() || Bluefruit.Advertising.isRunning());
}

void MeshtasticBleService::setDeviceName(const std::string& name)
{
    device_name_ = name;
}

bool MeshtasticBleService::handleToRadio(const uint8_t* data, size_t len)
{
    last_ble_activity_ms_ = millis();
    last_to_radio_ms_ = last_ble_activity_ms_;
    const uint32_t to_radio_variant = peekToRadioVariant(data, len);
    if (to_radio_variant == meshtastic_ToRadio_heartbeat_tag)
    {
        last_heartbeat_ms_ = last_ble_activity_ms_;
    }
    else if (to_radio_variant == meshtastic_ToRadio_want_config_id_tag)
    {
        last_want_config_ms_ = last_ble_activity_ms_;
    }
    const bool config_before = phone_session_ && phone_session_->isConfigFlowActive();
    const bool send_before = phone_session_ && phone_session_->isSendingPackets();
    probeGpsGuard("ble_to_radio_pre_handle");
    const bool ok = phone_session_ ? phone_session_->handleToRadio(data, len) : false;
    probeGpsGuard("ble_to_radio_post_handle");
    Serial2.printf("[BLE][nrf52][mt] handleToRadio variant=%lu len=%u ok=%u connected=%u notify=%u config=%u->%u send=%u->%u\n",
                   static_cast<unsigned long>(to_radio_variant),
                   static_cast<unsigned>(len),
                   ok ? 1U : 0U,
                   connected_ ? 1U : 0U,
                   from_num_notify_enabled_ ? 1U : 0U,
                   config_before ? 1U : 0U,
                   (phone_session_ && phone_session_->isConfigFlowActive()) ? 1U : 0U,
                   send_before ? 1U : 0U,
                   (phone_session_ && phone_session_->isSendingPackets()) ? 1U : 0U);
    const bool config_after = phone_session_ && phone_session_->isConfigFlowActive();
    const bool send_after = phone_session_ && phone_session_->isSendingPackets();
    if (to_radio_variant == meshtastic_ToRadio_heartbeat_tag)
    {
        logSessionState("heartbeat", to_radio_variant);
    }
    else if (to_radio_variant == meshtastic_ToRadio_want_config_id_tag)
    {
        logSessionState("want_config", to_radio_variant);
    }
    else if (to_radio_variant == meshtastic_ToRadio_disconnect_tag)
    {
        logSessionState("phone_disconnect", to_radio_variant);
    }
    else if (config_before != config_after || send_before != send_after)
    {
        logSessionState("phase_change", to_radio_variant);
    }
    return ok;
}

void MeshtasticBleService::handleFromRadioReadRequest(uint16_t conn_handle,
                                                      uint16_t offset,
                                                      uintptr_t chr_ptr)
{
    pending_from_radio_auth_conn_handle_ = conn_handle;
    pending_from_radio_auth_offset_ = offset;
    pending_from_radio_auth_chr_ = chr_ptr;
    pending_from_radio_auth_svc_ = reinterpret_cast<uintptr_t>(this);
    pending_from_radio_auth_log_ = true;

    if (offset != 0)
    {
        authorizeRead(conn_handle);
        return;
    }

    last_ble_activity_ms_ = millis();
    if (published_from_radio_count_ > 0)
    {
        const bool consumed = writePublishedFromRadioForRead(conn_handle);
        authorizeRead(conn_handle);
        if (consumed)
        {
            last_from_radio_read_ms_ = last_ble_activity_ms_;
            releasePublishedFromRadioHead();
        }
        return;
    }

    const bool publish_requested_before_read = from_radio_publish_requested_;
    const bool should_hold = active_ && connected_ && phone_session_ &&
                             (pending_to_radio_count_ > 0 || publish_requested_before_read ||
                              phone_session_->isConfigFlowActive());
    if (should_hold)
    {
        pending_from_radio_read_authorize_ = true;
        pending_from_radio_read_conn_handle_ = conn_handle;
        pending_from_radio_read_due_ms_ = last_ble_activity_ms_ + kFromRadioReadHoldMs;
        from_radio_publish_requested_ = true;
        bleLogBoth("[BLE][nrf52][mt][flow] from_radio read wait conn=%u pending_to=%u publish_req=%u cfg=%u due_ms=%lu",
                   static_cast<unsigned>(conn_handle),
                   static_cast<unsigned>(pending_to_radio_count_),
                   publish_requested_before_read ? 1U : 0U,
                   phone_session_->isConfigFlowActive() ? 1U : 0U,
                   static_cast<unsigned long>(kFromRadioReadHoldMs));
        return;
    }

    writeEmptyFromRadioRead(active_ && connected_ ? kFromRadioEmptyNoFrame : kFromRadioEmptyInactive);
    authorizeRead(conn_handle);
}

void MeshtasticBleService::fillPublishedFromRadioSlots()
{
    if (!active_ || !connected_ || !phone_session_)
    {
        from_radio_publish_requested_ = false;
        return;
    }

    if (!from_radio_publish_requested_)
    {
        return;
    }

    const uint8_t target_capacity = phone_session_->isConfigFlowActive()
                                        ? kPublishedFromRadioCapacity
                                        : kSteadyPublishedFromRadioWindow;

    while (published_from_radio_count_ < target_capacity)
    {
        auto& session_frame = session_frame_scratch_;
        std::memset(&session_frame, 0, sizeof(session_frame));
        probeGpsGuard("ble_from_radio_pre_pop");
        if (!phone_session_->popToPhone(&session_frame))
        {
            probeGpsGuard("ble_from_radio_post_pop_empty");
            from_radio_publish_requested_ = false;
            return;
        }
        probeGpsGuard("ble_from_radio_post_pop_frame");

        if (session_frame.len == 0 || session_frame.len > meshtastic_FromRadio_size)
        {
            bleLogBoth("[BLE][nrf52][mt] drop invalid published from_radio frame from_num=%08lX len=%u max=%u kind=%s pri=%u",
                       static_cast<unsigned long>(session_frame.from_num),
                       static_cast<unsigned>(session_frame.len),
                       static_cast<unsigned>(meshtastic_FromRadio_size),
                       bleFrameKindName(session_frame.kind),
                       static_cast<unsigned>(session_frame.priority));
            continue;
        }

        const uint8_t slot_index = published_from_radio_tail_;
        PublishedFromRadioSlot& slot = published_from_radio_[slot_index];
        slot.frame = session_frame;
        slot.notified = false;
        bleLogBoth("[BLE][nrf52][mt][flow] publish slot=%u from_num=%08lX len=%u kind=%s pri=%u q=%u",
                   static_cast<unsigned>(slot_index),
                   static_cast<unsigned long>(slot.frame.from_num),
                   static_cast<unsigned>(slot.frame.len),
                   bleFrameKindName(slot.frame.kind),
                   static_cast<unsigned>(slot.frame.priority),
                   static_cast<unsigned>(published_from_radio_count_));

        noInterrupts();
        published_from_radio_tail_ =
            static_cast<uint8_t>((published_from_radio_tail_ + 1U) % kPublishedFromRadioCapacity);
        ++published_from_radio_count_;
        interrupts();
    }

    from_radio_publish_requested_ = true;
}

void MeshtasticBleService::flushPendingFromRadioReadAuthorize()
{
    if (!pending_from_radio_read_authorize_)
    {
        return;
    }

    fillPublishedFromRadioSlots();

    const uint16_t conn_handle = pending_from_radio_read_conn_handle_;
    if (conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        clearPendingFromRadioReadAuthorize();
        return;
    }

    const uint32_t now_ms = millis();
    if (published_from_radio_count_ > 0)
    {
        const bool consumed = writePublishedFromRadioForRead(conn_handle);
        authorizeRead(conn_handle);
        clearPendingFromRadioReadAuthorize();
        if (consumed)
        {
            last_from_radio_read_ms_ = now_ms;
            releasePublishedFromRadioHead();
        }
        return;
    }

    if (!active_ || !connected_ || static_cast<int32_t>(now_ms - pending_from_radio_read_due_ms_) >= 0)
    {
        writeEmptyFromRadioRead(active_ && connected_ ? kFromRadioEmptyNoFrame : kFromRadioEmptyInactive);
        authorizeRead(conn_handle);
        bleLogBoth("[BLE][nrf52][mt][flow] from_radio read wait timeout conn=%u active=%u connected=%u pending_to=%u publish_req=%u",
                   static_cast<unsigned>(conn_handle),
                   active_ ? 1U : 0U,
                   connected_ ? 1U : 0U,
                   static_cast<unsigned>(pending_to_radio_count_),
                   from_radio_publish_requested_ ? 1U : 0U);
        clearPendingFromRadioReadAuthorize();
    }
}

void MeshtasticBleService::clearPendingFromRadioReadAuthorize()
{
    pending_from_radio_read_authorize_ = false;
    pending_from_radio_read_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_from_radio_read_due_ms_ = 0;
}

void MeshtasticBleService::writeEmptyFromRadioRead(uint8_t reason)
{
    uint8_t empty = 0;
    switch (reason)
    {
    case kFromRadioEmptyInactive:
        probeGpsGuard("ble_from_radio_pre_write_empty_inactive");
        from_radio_.write(&empty, 0);
        probeGpsGuard("ble_from_radio_post_write_empty_inactive");
        break;
    case kFromRadioEmptyInvalidFrame:
        probeGpsGuard("ble_from_radio_pre_write_empty_invalid");
        from_radio_.write(&empty, 0);
        probeGpsGuard("ble_from_radio_post_write_empty_invalid");
        break;
    case kFromRadioEmptyNoFrame:
    default:
        probeGpsGuard("ble_from_radio_pre_write_empty_no_frame");
        from_radio_.write(&empty, 0);
        probeGpsGuard("ble_from_radio_post_write_empty_no_frame");
        reason = kFromRadioEmptyNoFrame;
        break;
    }
    pending_from_radio_empty_log_ = true;
    pending_from_radio_empty_reason_ = reason;
}

bool MeshtasticBleService::writePublishedFromRadioForRead(uint16_t conn_handle)
{
    if (!active_ || !connected_ || !phone_session_)
    {
        writeEmptyFromRadioRead(kFromRadioEmptyInactive);
        return false;
    }

    if (published_from_radio_count_ == 0)
    {
        from_radio_publish_requested_ = true;
        writeEmptyFromRadioRead(kFromRadioEmptyNoFrame);
        return false;
    }

    const uint8_t slot_index = published_from_radio_head_;
    PublishedFromRadioSlot& slot = published_from_radio_[slot_index];
    if (slot.frame.len == 0 || slot.frame.len > meshtastic_FromRadio_size)
    {
        bleLogBoth("[BLE][nrf52][mt] drop invalid published from_radio read slot=%u from_num=%08lX len=%u max=%u kind=%s pri=%u",
                   static_cast<unsigned>(slot_index),
                   static_cast<unsigned long>(slot.frame.from_num),
                   static_cast<unsigned>(slot.frame.len),
                   static_cast<unsigned>(meshtastic_FromRadio_size),
                   bleFrameKindName(slot.frame.kind),
                   static_cast<unsigned>(slot.frame.priority));
        releasePublishedFromRadioHead();
        writeEmptyFromRadioRead(kFromRadioEmptyInvalidFrame);
        from_radio_publish_requested_ = true;
        return false;
    }

    probeGpsGuard("ble_from_radio_pre_write_frame");
    from_radio_.write(slot.frame.buf, slot.frame.len);
    probeGpsGuard("ble_from_radio_post_write_frame");
    pending_from_radio_read_len_ = static_cast<uint16_t>(slot.frame.len);
    pending_from_radio_read_from_num_ = slot.frame.from_num;
    pending_from_radio_read_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_radio read slot=%u from_num=%08lX len=%u kind=%s pri=%u",
               static_cast<unsigned>(slot_index),
               static_cast<unsigned long>(slot.frame.from_num),
               static_cast<unsigned>(slot.frame.len),
               bleFrameKindName(slot.frame.kind),
               static_cast<unsigned>(slot.frame.priority));
    (void)conn_handle;
    return true;
}

void MeshtasticBleService::releasePublishedFromRadioHead()
{
    if (published_from_radio_count_ == 0)
    {
        return;
    }

    PublishedFromRadioSlot& slot = published_from_radio_[published_from_radio_head_];
    slot.frame.len = 0;
    slot.frame.kind = phone::meshtastic::MeshtasticBleFrameKind::None;
    slot.frame.priority = phone::meshtastic::MeshtasticBleFramePriority::P3;
    slot.frame.from_num = 0;
    slot.notified = false;
    noInterrupts();
    published_from_radio_head_ =
        static_cast<uint8_t>((published_from_radio_head_ + 1U) % kPublishedFromRadioCapacity);
    --published_from_radio_count_;
    interrupts();
    from_radio_publish_requested_ = true;
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
    while (true)
    {
        noInterrupts();
        if (pending_to_radio_count_ == 0)
        {
            interrupts();
            return;
        }

        PendingToRadioFrame& frame = pending_to_radio_[pending_to_radio_head_];
        pending_to_radio_work_.len = frame.len;
        std::memcpy(pending_to_radio_work_.buf, frame.buf, frame.len);
        pending_to_radio_head_ = static_cast<uint8_t>((pending_to_radio_head_ + 1U) % kPendingToRadioCapacity);
        --pending_to_radio_count_;
        interrupts();

        (void)handleToRadio(pending_to_radio_work_.buf, pending_to_radio_work_.len);
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

void MeshtasticBleService::clearToPhoneQueue()
{
    clearPendingFromRadioReadAuthorize();
    session_frame_scratch_ = phone::meshtastic::MeshtasticBleFrame{};
    for (uint8_t index = 0; index < kPublishedFromRadioCapacity; ++index)
    {
        published_from_radio_[index] = PublishedFromRadioSlot{};
    }
    published_from_radio_head_ = 0;
    published_from_radio_tail_ = 0;
    published_from_radio_count_ = 0;
    from_radio_publish_requested_ = false;
}

void MeshtasticBleService::requestFromRadioPublish(const char* reason)
{
    if (!phone_session_ || (!phone_session_->isSendingPackets() && !phone_session_->isConfigFlowActive()))
    {
        return;
    }

    from_radio_publish_requested_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_radio publish kick reason=%s published=%u head=%u tail=%u",
               reason ? reason : "?",
               static_cast<unsigned>(published_from_radio_count_),
               static_cast<unsigned>(published_from_radio_head_),
               static_cast<unsigned>(published_from_radio_tail_));
}

uint32_t MeshtasticBleService::nextFromNumNotifyValue()
{
    uint32_t value = ++from_num_notify_counter_;
    if (value == 0)
    {
        value = ++from_num_notify_counter_;
    }
    return value;
}

void MeshtasticBleService::loadRememberedPhonePeer()
{
    MeshtasticBlePeerIdentity stored{};
    bool from_bond_store = false;
    if (!loadMeshtasticBlePeerIdentity(&stored))
    {
        ble_gap_addr_t bonded_peer{};
        if (!loadFirstPeripheralBondPeer(&bonded_peer))
        {
            bleLogBoth("[BLE][nrf52][mt] remembered peer load none");
            return;
        }

        stored.addr_type = bonded_peer.addr_type;
        stored.bonded = 1;
        std::memcpy(stored.addr, bonded_peer.addr, sizeof(stored.addr));
        from_bond_store = true;
    }

    ble_gap_addr_t peer{};
    peer.addr_type = stored.addr_type;
    std::memcpy(peer.addr, stored.addr, sizeof(peer.addr));
    if (!isIdentityBleAddressType(peer.addr_type) || isEmptyBleAddress(peer))
    {
        char peer_addr_text[24] = {};
        formatBleAddress(peer, peer_addr_text, sizeof(peer_addr_text));
        bleLogBoth("[BLE][nrf52][mt] remembered peer ignored peer=%s type=%s(0x%02X)",
                   peer_addr_text,
                   bleAddressTypeName(peer.addr_type),
                   static_cast<unsigned>(peer.addr_type));
        return;
    }

    remembered_phone_peer_ = peer;
    remembered_phone_peer_valid_ = true;
    remembered_phone_peer_bonded_ = stored.bonded != 0;
    char peer_addr_text[24] = {};
    formatBleAddress(remembered_phone_peer_, peer_addr_text, sizeof(peer_addr_text));
    if (from_bond_store)
    {
        const bool saved = saveMeshtasticBlePeerIdentity(stored);
        bleLogBoth("[BLE][nrf52][mt] remembered peer persisted from bond saved=%u",
                   saved ? 1U : 0U);
    }
    bleLogBoth("[BLE][nrf52][mt] remembered peer loaded source=%s peer=%s type=%s(0x%02X) bonded=%u",
               from_bond_store ? "bond" : "settings",
               peer_addr_text,
               bleAddressTypeName(remembered_phone_peer_.addr_type),
               static_cast<unsigned>(remembered_phone_peer_.addr_type),
               remembered_phone_peer_bonded_ ? 1U : 0U);
}

void MeshtasticBleService::rememberPhonePeer(uint16_t conn_handle, const char* reason)
{
    BLEConnection* connection = Bluefruit.Connection(conn_handle);
    if (!connection)
    {
        bleLogBoth("[BLE][nrf52][mt] remember peer skipped reason=%s conn=%u unavailable",
                   reason ? reason : "?",
                   static_cast<unsigned>(conn_handle));
        return;
    }

    const ble_gap_addr_t peer = connection->getPeerAddr();
    char peer_addr_text[24] = {};
    formatBleAddress(peer, peer_addr_text, sizeof(peer_addr_text));
    if (!isIdentityBleAddressType(peer.addr_type) || isEmptyBleAddress(peer))
    {
        bleLogBoth("[BLE][nrf52][mt] remember peer skipped reason=%s peer=%s type=%s(0x%02X) bonded=%u secured=%u",
                   reason ? reason : "?",
                   peer_addr_text,
                   bleAddressTypeName(peer.addr_type),
                   static_cast<unsigned>(peer.addr_type),
                   connection->bonded() ? 1U : 0U,
                   connection->secured() ? 1U : 0U);
        return;
    }

    const bool bonded = connection->bonded();
    const bool unchanged = remembered_phone_peer_valid_ &&
                           remembered_phone_peer_.addr_type == peer.addr_type &&
                           std::memcmp(remembered_phone_peer_.addr, peer.addr, sizeof(peer.addr)) == 0 &&
                           remembered_phone_peer_bonded_ == bonded;

    remembered_phone_peer_ = peer;
    remembered_phone_peer_valid_ = true;
    remembered_phone_peer_bonded_ = bonded;
    if (unchanged)
    {
        bleLogBoth("[BLE][nrf52][mt] remembered peer unchanged reason=%s peer=%s type=%s(0x%02X) bonded=%u",
                   reason ? reason : "?",
                   peer_addr_text,
                   bleAddressTypeName(peer.addr_type),
                   static_cast<unsigned>(peer.addr_type),
                   bonded ? 1U : 0U);
        return;
    }

    MeshtasticBlePeerIdentity stored{};
    stored.addr_type = peer.addr_type;
    stored.bonded = bonded ? 1U : 0U;
    std::memcpy(stored.addr, peer.addr, sizeof(stored.addr));
    const bool saved = saveMeshtasticBlePeerIdentity(stored);
    bleLogBoth("[BLE][nrf52][mt] remembered peer saved=%u reason=%s peer=%s type=%s(0x%02X) bonded=%u secured=%u",
               saved ? 1U : 0U,
               reason ? reason : "?",
               peer_addr_text,
               bleAddressTypeName(peer.addr_type),
               static_cast<unsigned>(peer.addr_type),
               bonded ? 1U : 0U,
               connection->secured() ? 1U : 0U);
}

void MeshtasticBleService::startPhoneAdvertising(bool prefer_directed)
{
    const uint32_t now_ms = millis();
    if (prefer_directed && remembered_phone_peer_valid_ && !directed_advertising_attempted_)
    {
        directed_advertising_attempted_ = true;
        if (startAdvertising(service_, &remembered_phone_peer_))
        {
            directed_advertising_active_ = true;
            directed_advertising_until_ms_ = now_ms + kDirectedAdvertisingMs;
            return;
        }

        bleLogBoth("[BLE][nrf52][mt] directed advertising failed; fallback to undirected");
    }

    directed_advertising_active_ = false;
    directed_advertising_until_ms_ = 0;
    (void)startAdvertising(service_, nullptr);
}

void MeshtasticBleService::handleConnectEvent(uint16_t conn_handle)
{
    enqueueGapEvent(PendingGapEventType::Connect, conn_handle, 0);
}

void MeshtasticBleService::handleDisconnectEvent(uint16_t conn_handle, uint8_t reason)
{
    enqueueGapEvent(PendingGapEventType::Disconnect, conn_handle, reason);
}

void MeshtasticBleService::enqueueGapEvent(PendingGapEventType type, uint16_t conn_handle, uint8_t reason)
{
    noInterrupts();
    if (pending_gap_event_count_ >= kPendingGapEventCapacity)
    {
        pending_gap_event_head_ = static_cast<uint8_t>((pending_gap_event_head_ + 1U) % kPendingGapEventCapacity);
        --pending_gap_event_count_;
        if (pending_gap_event_drop_count_ != 0xFFU)
        {
            ++pending_gap_event_drop_count_;
        }
    }

    PendingGapEvent& event = pending_gap_events_[pending_gap_event_tail_];
    event.type = type;
    event.conn_handle = conn_handle;
    event.reason = reason;
    pending_gap_event_tail_ = static_cast<uint8_t>((pending_gap_event_tail_ + 1U) % kPendingGapEventCapacity);
    ++pending_gap_event_count_;
    interrupts();
}

void MeshtasticBleService::processPendingGapEvents()
{
    uint8_t dropped = 0;
    noInterrupts();
    if (pending_gap_event_drop_count_ != 0)
    {
        dropped = pending_gap_event_drop_count_;
        pending_gap_event_drop_count_ = 0;
    }
    interrupts();
    if (dropped != 0)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] gap event queue dropped=%u", static_cast<unsigned>(dropped));
    }

    while (true)
    {
        PendingGapEvent event{};
        noInterrupts();
        if (pending_gap_event_count_ == 0)
        {
            interrupts();
            return;
        }

        event = pending_gap_events_[pending_gap_event_head_];
        pending_gap_event_head_ =
            static_cast<uint8_t>((pending_gap_event_head_ + 1U) % kPendingGapEventCapacity);
        --pending_gap_event_count_;
        interrupts();

        if (event.type == PendingGapEventType::Connect)
        {
            applyConnectEvent(event.conn_handle);
        }
        else
        {
            applyDisconnectEvent(event.conn_handle, event.reason);
        }
    }
}

void MeshtasticBleService::applyConnectEvent(uint16_t conn_handle)
{
    connected_ = true;
    conn_handle_ = conn_handle;
    ++ble_session_seq_;
    directed_advertising_active_ = false;
    directed_advertising_attempted_ = false;
    directed_advertising_until_ms_ = 0;
    last_ble_activity_ms_ = millis();
    session_started_ms_ = last_ble_activity_ms_;
    last_secured_ms_ = 0;
    last_from_num_cccd_ms_ = 0;
    last_to_radio_ms_ = 0;
    last_heartbeat_ms_ = 0;
    last_want_config_ms_ = 0;
    last_from_radio_read_ms_ = 0;
    last_from_num_notify_ms_ = 0;
    next_connected_session_log_ms_ = last_ble_activity_ms_ + kBleConnectedSessionLogIntervalMs;
    next_liveness_log_ms_ = 0;
    from_num_notify_enabled_ = false;
    from_num_notify_counter_ = 0;
    pending_phone_disconnect_request_ = false;
    // GAP reconnects are transport churn. Keep the phone core queue alive so MQTT
    // downlinks and mesh projections can drain after the app resubscribes.
    clearToPhoneQueue();
    requestFromRadioPublish("link_up");
    pairing_request_pending_ = true;
    pending_pairing_conn_handle_ = conn_handle;
    pending_connect_conn_handle_ = conn_handle;
    pending_connect_log_ = true;

    char peer_addr_text[24] = {};
    BLEConnection* connection = Bluefruit.Connection(conn_handle);
    if (connection)
    {
        const ble_gap_addr_t peer_addr = connection->getPeerAddr();
        formatBleAddress(peer_addr, peer_addr_text, sizeof(peer_addr_text));
        bleLogBoth("[BLE][nrf52][mt][flow] link-up conn=%u adv=%u peer=%s type=%s(0x%02X) bonded=%u secured=%u mtu=%u interval=%u latency=%u timeout=%u",
                   static_cast<unsigned>(conn_handle),
                   Bluefruit.Advertising.isRunning() ? 1U : 0U,
                   peer_addr_text,
                   bleAddressTypeName(peer_addr.addr_type),
                   static_cast<unsigned>(peer_addr.addr_type),
                   connection->bonded() ? 1U : 0U,
                   connection->secured() ? 1U : 0U,
                   static_cast<unsigned>(connection->getMtu()),
                   static_cast<unsigned>(connection->getConnectionInterval()),
                   static_cast<unsigned>(connection->getSlaveLatency()),
                   static_cast<unsigned>(connection->getSupervisionTimeout()));
    }
    else
    {
        bleLogBoth("[BLE][nrf52][mt][flow] link-up conn=%u adv=%u peer=unavailable",
                   static_cast<unsigned>(conn_handle),
                   Bluefruit.Advertising.isRunning() ? 1U : 0U);
    }
    logSessionState("link_up");
    rememberPhonePeer(conn_handle, "connect");
    if (from_num_.getCccd(conn_handle) != 0U)
    {
        from_num_notify_enabled_ = true;
        requestFromRadioPublish("link_up_cccd_restore");
    }
}

void MeshtasticBleService::applyDisconnectEvent(uint16_t conn_handle, uint8_t reason)
{
    connected_ = false;
    from_num_notify_enabled_ = false;
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    last_ble_activity_ms_ = millis();
    next_connected_session_log_ms_ = 0;
    // Do not close phone_session_ on a transient GAP disconnect; explicit
    // ToRadio.disconnect is the protocol-level reset point.
    pending_passkey_.store(0);
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_disconnect_conn_handle_ = conn_handle;
    pending_disconnect_reason_ = reason;
    pending_disconnect_log_ = true;
    pending_phone_disconnect_request_ = false;
    next_liveness_log_ms_ = 0;
    directed_advertising_active_ = false;
    directed_advertising_attempted_ = false;
    directed_advertising_until_ms_ = 0;
    bleLogBoth("[BLE][nrf52][mt][flow] link-down conn=%u reason=0x%02X(%s) adv=%u",
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned>(reason),
               disconnectReasonName(reason),
               Bluefruit.Advertising.isRunning() ? 1U : 0U);
    logSessionState("link_down", reason);
    flushPendingConfigSaves(true);
}

void MeshtasticBleService::handleFromNumCccdWrite(uint16_t conn_handle, uint16_t value)
{
    conn_handle_ = conn_handle;
    last_ble_activity_ms_ = millis();
    last_from_num_cccd_ms_ = last_ble_activity_ms_;
    from_num_notify_enabled_ = (value != 0U);
    pending_from_num_cccd_conn_handle_ = conn_handle;
    pending_from_num_cccd_value_ = value;
    pending_from_num_cccd_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_num subscribed=%u conn=%u value=0x%04X",
               from_num_notify_enabled_ ? 1U : 0U,
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned>(value));
    logSessionState(from_num_notify_enabled_ ? "from_num_cccd_on" : "from_num_cccd_off", value);
    if (from_num_notify_enabled_)
    {
        requestFromRadioPublish("from_num_cccd_on");
    }
}

void MeshtasticBleService::handlePairPasskeyDisplay(uint16_t conn_handle, const uint8_t passkey[6], bool match_request)
{
    const uint32_t parsed = parsePasskeyDigits(passkey);
    pending_passkey_.store(parsed);
    const uint32_t configured = configured_passkey_.load();
    bleLogBoth("[BLE][nrf52][mt][flow] passkey-display conn=%u passkey=%06lu configured=%06lu match=%u consistent=%u",
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned long>(parsed),
               static_cast<unsigned long>(configured),
               match_request ? 1U : 0U,
               (configured == 0 || configured == parsed) ? 1U : 0U);
}

void MeshtasticBleService::handlePairComplete(uint16_t conn_handle, uint8_t auth_status)
{
    pending_passkey_.store(0);
    pending_pair_complete_conn_handle_ = conn_handle;
    pending_pair_complete_status_ = auth_status;
    pending_pair_complete_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] pair-complete conn=%u status=0x%02X(%s)",
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned>(auth_status),
               gapSecurityStatusName(auth_status));
    if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS)
    {
        rememberPhonePeer(conn_handle, "pair");
    }
}

void MeshtasticBleService::handleSecured(uint16_t conn_handle)
{
    pending_passkey_.store(0);
    pending_secured_conn_handle_ = conn_handle;
    pending_secured_log_ = true;
    last_secured_ms_ = millis();
    BLEConnection* connection = Bluefruit.Connection(conn_handle);
    bleLogBoth("[BLE][nrf52][mt][flow] secured conn=%u bonded=%u secured=%u mtu=%u",
               static_cast<unsigned>(conn_handle),
               connection && connection->bonded() ? 1U : 0U,
               connection && connection->secured() ? 1U : 0U,
               connection ? static_cast<unsigned>(connection->getMtu()) : 0U);
    logSessionState("secured");
    rememberPhonePeer(conn_handle, "secured");
    if (from_num_.getCccd(conn_handle) != 0U)
    {
        from_num_notify_enabled_ = true;
        requestFromRadioPublish("secured_cccd_restore");
    }
}

bool MeshtasticBleService::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }

    *out = BlePairingStatus{};
    out->available = phone_facade_.isBleEnabled();
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

bool MeshtasticBleService::isPhoneBleConnected() const
{
    return isBleConnected();
}

uint32_t MeshtasticBleService::pendingPhoneBlePasskey() const
{
    return pending_passkey_.load();
}

void MeshtasticBleService::requestPhoneHighThroughputConnection()
{
}

void MeshtasticBleService::requestPhoneLowerPowerConnection()
{
}

void MeshtasticBleService::requestPhoneDisconnect()
{
    pending_phone_disconnect_request_ = true;
}

bool MeshtasticBleService::processPendingPhoneDisconnect()
{
    if (!pending_phone_disconnect_request_)
    {
        return false;
    }

    pending_phone_disconnect_request_ = false;
    const uint16_t handle = conn_handle_;
    const bool handle_valid = handle != BLE_CONN_HANDLE_INVALID;
    const bool gap_connected = handle_valid && Bluefruit.connected(handle);
    bleLogBoth("[BLE][nrf52][mt][flow] phone disconnect request connected=%u gap=%u handle=%u",
               connected_ ? 1U : 0U,
               gap_connected ? 1U : 0U,
               handle_valid ? static_cast<unsigned>(handle) : 0xFFFFU);

    clearPendingFromRadioReadAuthorize();
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    from_num_notify_enabled_ = false;
    pending_passkey_.store(0);

    if (!connected_ || !gap_connected)
    {
        if (!Bluefruit.Advertising.isRunning())
        {
            startPhoneAdvertising(!directed_advertising_attempted_);
        }
        return false;
    }

    const bool ok = Bluefruit.disconnect(handle);
    bleLogBoth("[BLE][nrf52][mt][flow] phone disconnect gap_request handle=%u ok=%u",
               static_cast<unsigned>(handle),
               ok ? 1U : 0U);
    if (!ok)
    {
        pending_phone_disconnect_request_ = true;
    }
    return true;
}

void MeshtasticBleService::checkPhoneSessionLiveness(uint32_t now_ms)
{
    if (!active_ || !connected_ || conn_handle_ == BLE_CONN_HANDLE_INVALID)
    {
        return;
    }

    uint32_t last_phone_activity_ms = last_ble_activity_ms_;
    if (last_heartbeat_ms_ > last_phone_activity_ms)
    {
        last_phone_activity_ms = last_heartbeat_ms_;
    }
    if (last_from_radio_read_ms_ > last_phone_activity_ms)
    {
        last_phone_activity_ms = last_from_radio_read_ms_;
    }
    if (last_to_radio_ms_ > last_phone_activity_ms)
    {
        last_phone_activity_ms = last_to_radio_ms_;
    }

    if (last_phone_activity_ms == 0 ||
        static_cast<uint32_t>(now_ms - last_phone_activity_ms) < kPhoneSessionStaleMs)
    {
        return;
    }

    if (next_liveness_log_ms_ == 0 || static_cast<int32_t>(now_ms - next_liveness_log_ms_) >= 0)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] phone session stale age_ms=%lu limit_ms=%lu connected=%u gap=%u "
                   "notify=%u cfg=%u send=%u",
                   static_cast<unsigned long>(now_ms - last_phone_activity_ms),
                   static_cast<unsigned long>(kPhoneSessionStaleMs),
                   connected_ ? 1U : 0U,
                   Bluefruit.connected(conn_handle_) ? 1U : 0U,
                   from_num_notify_enabled_ ? 1U : 0U,
                   (phone_session_ && phone_session_->isConfigFlowActive()) ? 1U : 0U,
                   (phone_session_ && phone_session_->isSendingPackets()) ? 1U : 0U);
        next_liveness_log_ms_ = now_ms + kPhoneSessionStaleLogIntervalMs;
    }

    pending_phone_disconnect_request_ = true;
    (void)processPendingPhoneDisconnect();
}

void MeshtasticBleService::onPhoneBluetoothConfigChanged()
{
    markConfigSavePending(true, false);
    bleLogBoth("[BLE][nrf52][mt] saveBluetoothConfig requested mode=%u pin=%06lu enabled=%u",
               static_cast<unsigned>(ble_config_.mode),
               static_cast<unsigned long>(ble_config_.fixed_pin),
               ble_config_.enabled ? 1U : 0U);
    flushPendingConfigSaves(true);
}

void MeshtasticBleService::onPhoneModuleConfigChanged()
{
    markConfigSavePending(false, true);
    syncMqttProxySettings();
    bleLogBoth("[BLE][nrf52][mt] saveModuleConfig requested");
    flushPendingConfigSaves(true);
}

void MeshtasticBleService::notifyFromNum(uint32_t from_num)
{
    if (!active_ || !connected_ || !phone_session_)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] from_radio publish request source=%08lX skip inactive active=%u connected=%u",
                   static_cast<unsigned long>(from_num),
                   active_ ? 1U : 0U,
                   connected_ ? 1U : 0U);
        return;
    }

    from_radio_publish_requested_ = true;

    bleLogBoth("[BLE][nrf52][mt][flow] from_radio publish request source=%08lX published=%u head=%u tail=%u",
               static_cast<unsigned long>(from_num),
               static_cast<unsigned>(published_from_radio_count_),
               static_cast<unsigned>(published_from_radio_head_),
               static_cast<unsigned>(published_from_radio_tail_));
}

void MeshtasticBleService::flushPendingFromNumNotify()
{
    fillPublishedFromRadioSlots();

    if (!active_ || !connected_)
    {
        if (published_from_radio_count_ > 0 || from_radio_publish_requested_)
        {
            bleLogBoth("[BLE][nrf52][mt][flow] from_num skip reason=inactive active=%u connected=%u published=%u",
                       active_ ? 1U : 0U,
                       connected_ ? 1U : 0U,
                       static_cast<unsigned>(published_from_radio_count_));
        }
        clearToPhoneQueue();
        return;
    }

    if (published_from_radio_count_ == 0)
    {
        return;
    }

    if (!from_num_notify_enabled_ || conn_handle_ == BLE_CONN_HANDLE_INVALID)
    {
        return;
    }

    const uint8_t slot_index = published_from_radio_head_;
    PublishedFromRadioSlot& slot = published_from_radio_[slot_index];
    if (slot.notified)
    {
        return;
    }

    const uint32_t source_from_num = slot.frame.from_num;
    if (slot.frame.len == 0 || slot.frame.len > meshtastic_FromRadio_size)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] from_num drop invalid published slot=%u from_num=%08lX len=%u kind=%s pri=%u",
                   static_cast<unsigned>(slot_index),
                   static_cast<unsigned long>(source_from_num),
                   static_cast<unsigned>(slot.frame.len),
                   bleFrameKindName(slot.frame.kind),
                   static_cast<unsigned>(slot.frame.priority));
        slot.frame.len = 0;
        slot.frame.kind = phone::meshtastic::MeshtasticBleFrameKind::None;
        slot.frame.priority = phone::meshtastic::MeshtasticBleFramePriority::P3;
        slot.frame.from_num = 0;
        slot.notified = false;
        noInterrupts();
        published_from_radio_head_ =
            static_cast<uint8_t>((published_from_radio_head_ + 1U) % kPublishedFromRadioCapacity);
        --published_from_radio_count_;
        interrupts();
        from_radio_publish_requested_ = true;
        return;
    }

    const uint32_t notify_value = nextFromNumNotifyValue();
    probeGpsGuard("ble_from_num_pre_write");
    from_num_.write32(notify_value);
    probeGpsGuard("ble_from_num_post_write");
    probeGpsGuard("ble_from_num_pre_notify");
    const bool ok = from_num_.notify32(conn_handle_, notify_value);
    probeGpsGuard("ble_from_num_post_notify");
    bleLogBoth("[BLE][nrf52][mt][flow] from_num notify slot=%u value=%08lX source=%08lX conn=%u ok=%u cccd=0x%04X q=%u kind=%s pri=%u",
               static_cast<unsigned>(slot_index),
               static_cast<unsigned long>(notify_value),
               static_cast<unsigned long>(source_from_num),
               static_cast<unsigned>(conn_handle_),
               ok ? 1U : 0U,
               static_cast<unsigned>(from_num_.getCccd(conn_handle_)),
               static_cast<unsigned>(published_from_radio_count_),
               bleFrameKindName(slot.frame.kind),
               static_cast<unsigned>(slot.frame.priority));
    if (ok)
    {
        last_from_num_notify_ms_ = millis();
        slot.notified = true;
        return;
    }
    if (!ok && Bluefruit.connected())
    {
        probeGpsGuard("ble_from_num_pre_notify_fallback");
        const bool fallback_ok = from_num_.notify32(notify_value);
        probeGpsGuard("ble_from_num_post_notify_fallback");
        bleLogBoth("[BLE][nrf52][mt][flow] from_num notify fallback slot=%u value=%08lX source=%08lX ok=%u",
                   static_cast<unsigned>(slot_index),
                   static_cast<unsigned long>(notify_value),
                   static_cast<unsigned long>(source_from_num),
                   fallback_ok ? 1U : 0U);
        if (fallback_ok)
        {
            last_from_num_notify_ms_ = millis();
            slot.notified = true;
        }
    }
}

void MeshtasticBleService::markConfigSavePending(bool bluetooth_changed, bool module_changed)
{
    if (bluetooth_changed)
    {
        bluetooth_config_save_pending_ = true;
    }
    if (module_changed)
    {
        module_config_save_pending_ = true;
    }
    config_save_due_ms_ = millis() + kConfigSaveDebounceMs;
}

void MeshtasticBleService::flushPendingConfigSaves(bool force)
{
    if (!bluetooth_config_save_pending_ && !module_config_save_pending_)
    {
        return;
    }

    const uint32_t now_ms = millis();
    if (!force)
    {
        if (phone_session_ && phone_session_->isConfigFlowActive())
        {
            return;
        }
        if (static_cast<int32_t>(now_ms - config_save_due_ms_) < 0)
        {
            return;
        }
        if (connected_ && (now_ms - last_ble_activity_ms_) < kConfigSaveDebounceMs)
        {
            return;
        }
    }

    meshtastic_Config_BluetoothConfig persisted_bluetooth = ble_config_;
    meshtastic_config_bridge::normalizeBluetoothConfig(&persisted_bluetooth);
    persisted_bluetooth.enabled = phone_facade_.isBleEnabled();
    meshtastic_LocalModuleConfig persisted_module = module_config_;
    meshtastic_config_bridge::normalizeModuleConfig(&persisted_module);
    const bool needs_save = bluetooth_config_save_pending_ || module_config_save_pending_;
    const bool persisted = needs_save ? saveMeshtasticBlePersistedState(persisted_bluetooth, persisted_module) : true;
    ble_config_.enabled = persisted_bluetooth.enabled;

    bleLogBoth("[BLE][nrf52][mt] current mem persisted=%u bluetooth_config_save_pending_=%u module_config_save_pending_=%u needs_save=%u",
               persisted ? 1U : 0U,
               bluetooth_config_save_pending_ ? 1U : 0U,
               module_config_save_pending_ ? 1U : 0U,
               needs_save ? 1U : 0U);

    if (bluetooth_config_save_pending_)
    {
        bleLogBoth("[BLE][nrf52][mt] flush bluetooth cfg persisted=%u mode=%u pin=%06lu enabled=%u",
                   persisted ? 1U : 0U,
                   static_cast<unsigned>(persisted_bluetooth.mode),
                   static_cast<unsigned long>(persisted_bluetooth.fixed_pin),
                   persisted_bluetooth.enabled ? 1U : 0U);
        if (persisted)
        {
            bluetooth_config_save_pending_ = false;
        }
    }

    if (module_config_save_pending_)
    {
        bleLogBoth("[BLE][nrf52][mt] flush module cfg persisted=%u enabled=%u proxy=%u root=%s",
                   persisted ? 1U : 0U,
                   module_config_.has_mqtt && module_config_.mqtt.enabled ? 1U : 0U,
                   module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled ? 1U : 0U,
                   module_config_.mqtt.root);
        if (persisted)
        {
            module_config_save_pending_ = false;
        }
    }

    if (!persisted)
    {
        config_save_due_ms_ = millis() + kConfigSaveDebounceMs;
    }
}

void MeshtasticBleService::syncMqttProxySettings()
{
    phone_facade_.syncMeshtasticMqttProxySettings(module_config_);
}

void MeshtasticBleService::applyBleSecurity()
{
    pending_passkey_.store(0);
    configured_passkey_.store(0);

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        const bool clear_pin_ok = clearSoftDeviceBlePasskey();
        Bluefruit.Security.setMITM(false);
        Bluefruit.Security.setIOCaps(false, false, false);
        Bluefruit.Security.setPairPasskeyCallback(nullptr);
        Bluefruit.Security.setPairCompleteCallback(nullptr);
        Bluefruit.Security.setSecuredCallback(nullptr);
        bleLogBoth("[BLE][nrf52][mt] security mode=no_pin clear_pin=%u", clear_pin_ok ? 1U : 0U);
        return;
    }

    uint32_t pairing_pin = 0;
    const char* mode_name = "random_pin";
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
    {
        pairing_pin = ble_config_.fixed_pin != 0 ? ble_config_.fixed_pin : kDefaultBleFixedPin;
        ble_config_.fixed_pin = pairing_pin;
        mode_name = "fixed";
    }
    else
    {
        pairing_pin = generateBlePairingPasskey();
    }

    char digits[7] = {};
    formatBlePasskey(pairing_pin, digits);
    configured_passkey_.store(pairing_pin);
    const bool set_pin_ok = Bluefruit.Security.setPIN(digits);
    Bluefruit.Security.setIOCaps(true, false, false);
    Bluefruit.Security.setMITM(true);
    Bluefruit.Security.setPairPasskeyCallback(onPairPasskeyDisplay);
    Bluefruit.Security.setPairCompleteCallback(onPairComplete);
    Bluefruit.Security.setSecuredCallback(onSecured);
    bleLogBoth("[BLE][nrf52][mt] security mode=%s pin=%s set_pin=%u",
               mode_name,
               digits,
               set_pin_ok ? 1U : 0U);
}

void MeshtasticBleService::requestPairingIfNeeded(uint16_t conn_handle)
{
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        return;
    }
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
        bleLogBoth("[BLE][nrf52][mt] disconnected conn=%u reason=0x%02X(%s)",
                   static_cast<unsigned>(pending_disconnect_conn_handle_),
                   static_cast<unsigned>(pending_disconnect_reason_),
                   disconnectReasonName(pending_disconnect_reason_));
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
        bleLogBoth("[BLE][nrf52][mt] pair complete status=0x%02X(%s) conn=%u",
                   static_cast<unsigned>(pending_pair_complete_status_),
                   disconnectReasonName(pending_pair_complete_status_),
                   static_cast<unsigned>(pending_pair_complete_conn_handle_));
    }

    if (pending_secured_log_)
    {
        pending_secured_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] secured conn=%u",
                   static_cast<unsigned>(pending_secured_conn_handle_));
    }

    if (pending_from_radio_auth_log_)
    {
        pending_from_radio_auth_log_ = false;
        bleLogBoth("[BLE][nrf52][mt][auth] conn=%u offset=%u chr=%p svc=%p",
                   static_cast<unsigned>(pending_from_radio_auth_conn_handle_),
                   static_cast<unsigned>(pending_from_radio_auth_offset_),
                   reinterpret_cast<void*>(static_cast<uintptr_t>(pending_from_radio_auth_chr_)),
                   reinterpret_cast<void*>(static_cast<uintptr_t>(pending_from_radio_auth_svc_)));
    }

    if (pending_from_radio_read_log_)
    {
        pending_from_radio_read_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_radio read seq=%lu len=%u from_num=%08lX",
                   static_cast<unsigned long>(ble_session_seq_),
                   static_cast<unsigned>(pending_from_radio_read_len_),
                   static_cast<unsigned long>(pending_from_radio_read_from_num_));
    }

    if (pending_from_radio_empty_log_)
    {
        pending_from_radio_empty_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_radio read empty seq=%lu reason=%s config_active=%u send_packets=%u connected=%u",
                   static_cast<unsigned long>(ble_session_seq_),
                   fromRadioEmptyReasonName(pending_from_radio_empty_reason_),
                   (phone_session_ && phone_session_->isConfigFlowActive()) ? 1U : 0U,
                   (phone_session_ && phone_session_->isSendingPackets()) ? 1U : 0U,
                   connected_ ? 1U : 0U);
    }
}

uint32_t MeshtasticBleService::effectivePasskey() const
{
    return pending_passkey_.load();
}

} // namespace ble
