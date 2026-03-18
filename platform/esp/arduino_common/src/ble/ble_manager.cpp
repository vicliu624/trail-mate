#include "ble/ble_manager.h"

#include "app/app_config.h"
#include "ble/ble_uuids.h"
#include "ble/meshcore_ble.h"
#include "ble/meshtastic_ble.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>

namespace ble
{

BleManager::BleManager(app::IAppBleFacade& ctx)
    : ctx_(ctx),
      active_protocol_(ctx.getConfig().mesh_protocol)
{
}

BleManager::~BleManager()
{
    if (service_)
    {
        service_->stop();
        service_.reset();
    }
    shutdownNimble();
}

void BleManager::begin()
{
    setEnabled(true);
}

void BleManager::update()
{
    const auto current_protocol = ctx_.getConfig().mesh_protocol;
    if (current_protocol != active_protocol_)
    {
        applyProtocol(current_protocol);
    }

    if (service_)
    {
        service_->update();
    }
}

void BleManager::applyProtocol(chat::MeshProtocol protocol)
{
    if (protocol != active_protocol_)
    {
        restartService(protocol);
    }
}

void BleManager::setEnabled(bool enabled)
{
    if (enabled)
    {
        if (!service_)
        {
            restartService(ctx_.getConfig().mesh_protocol);
        }
    }
    else
    {
        if (service_)
        {
            service_->stop();
            service_.reset();
        }
        // NOTE: Do not deinit NimBLE on toggle-off, to avoid tearing down the
        // host task while it's still running (can cause InstrFetchProhibited).
        // Leaving NimBLE initialized but without services/advertising is enough
        // to consider Bluetooth "off" from the app's perspective.
    }
}

void BleManager::restartService(chat::MeshProtocol protocol)
{
    if (service_)
    {
        service_->stop();
        service_.reset();
    }

    shutdownNimble();

    const std::string device_name = buildDeviceName(protocol);

    NimBLEDevice::init(device_name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    nimble_initialized_ = true;

    if (protocol == chat::MeshProtocol::MeshCore)
    {
        service_.reset(new MeshCoreBleService(ctx_, device_name));
    }
    else
    {
        service_.reset(new MeshtasticBleService(ctx_, device_name));
        protocol = chat::MeshProtocol::Meshtastic;
    }

    active_protocol_ = protocol;
    if (service_)
    {
        const char* protocol_name = chat::infra::meshProtocolSlug(active_protocol_);
        const char* service_uuid = (active_protocol_ == chat::MeshProtocol::MeshCore)
                                       ? NUS_SERVICE_UUID
                                       : MESH_SERVICE_UUID;
        Serial.printf("[BLE] starting protocol=%s uuid=%s name=%s\n",
                      protocol_name,
                      service_uuid,
                      device_name.c_str());
        service_->start();
    }
}

void BleManager::shutdownNimble()
{
    if (!nimble_initialized_)
    {
        return;
    }
    NimBLEDevice::deinit(true);
    nimble_initialized_ = false;
}

std::string BleManager::buildDeviceName(chat::MeshProtocol protocol) const
{
    char long_name[32] = {};
    char short_name[16] = {};
    ctx_.getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));

    chat::runtime::EffectiveSelfIdentity identity{};
    identity.node_id = ctx_.getSelfNodeId();
    std::strncpy(identity.long_name, long_name, sizeof(identity.long_name) - 1);
    identity.long_name[sizeof(identity.long_name) - 1] = '\0';
    std::strncpy(identity.short_name, short_name, sizeof(identity.short_name) - 1);
    identity.short_name[sizeof(identity.short_name) - 1] = '\0';

    char visible_name[32] = {};
    chat::runtime::buildBleVisibleName(identity, protocol, visible_name, sizeof(visible_name));
    return visible_name;
}

} // namespace ble
