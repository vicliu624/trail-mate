#include "../../include/ble/meshcore_ble.h"

#include "ble/ble_uuids.h"

#include <Arduino.h>

namespace ble
{
namespace
{

constexpr size_t kMaxFrameSize = 172;
MeshCoreBleService* s_active_service = nullptr;

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

} // namespace

MeshCoreBleService::MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name),
      service_(::BLEUuid(NUS_SERVICE_UUID)),
      rx_char_(::BLEUuid(NUS_CHAR_RX_UUID)),
      tx_char_(::BLEUuid(NUS_CHAR_TX_UUID)),
      core_(new MeshCorePhoneCore(ctx, device_name))
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

} // namespace ble
