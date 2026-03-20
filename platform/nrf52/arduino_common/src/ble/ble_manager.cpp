#include "../../include/ble/ble_manager.h"

#include "../../include/ble/meshcore_ble.h"
#include "../../include/ble/meshtastic_ble.h"
#include "app/app_config.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"

#include <Arduino.h>
#include <cstdio>
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
}

void BleManager::begin()
{
    setEnabled(true);
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
    }
}

bool BleManager::isEnabled() const
{
    return ctx_.isBleEnabled();
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

bool BleManager::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }
    *out = BlePairingStatus{};
    return service_ ? service_->getPairingStatus(out) : false;
}

void BleManager::restartService(chat::MeshProtocol protocol)
{
    if (service_)
    {
        service_->stop();
        service_.reset();
    }

    active_protocol_ = protocol;
    const std::string device_name = buildDeviceName(protocol);
    const char* protocol_name = chat::infra::meshProtocolSlug(active_protocol_);

    switch (active_protocol_)
    {
    case chat::MeshProtocol::MeshCore:
        service_ = std::unique_ptr<BleService>(new MeshCoreBleService(ctx_, device_name));
        break;
    case chat::MeshProtocol::Meshtastic:
    default:
        service_ = std::unique_ptr<BleService>(new MeshtasticBleService(ctx_, device_name));
        break;
    }

    if (service_)
    {
        service_->start();
        Serial2.printf("[BLE][nrf52] protocol=%s name=%s service=started\n",
                       protocol_name,
                       device_name.c_str());
    }
    else
    {
        Serial2.printf("[BLE][nrf52] protocol=%s name=%s service=create_failed\n",
                       protocol_name,
                       device_name.c_str());
    }
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
