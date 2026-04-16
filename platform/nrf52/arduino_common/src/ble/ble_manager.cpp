#include "../../include/ble/ble_manager.h"

#include "../../include/ble/meshcore_ble.h"
#include "../../include/ble/meshtastic_ble.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ble
{
namespace
{

bool usbSerialWritable(std::size_t len)
{
    return static_cast<bool>(Serial) && Serial.dtr() != 0 && Serial.availableForWrite() >= static_cast<int>(len);
}

void bleManagerLog(const char* fmt, ...)
{
    char buffer[160] = {};
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

} // namespace

BleManager::BleManager(IBleRuntimeContext& ctx)
    : ctx_(ctx),
      active_protocol_(ctx.bleConfig().mesh_protocol)
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
    bleManagerLog("[BLE][nrf52] begin enabled=%u proto=%u",
                  ctx_.bleEnabled() ? 1U : 0U,
                  static_cast<unsigned>(ctx_.bleConfig().mesh_protocol));
    setEnabled(ctx_.bleEnabled());
}

void BleManager::setEnabled(bool enabled)
{
    if (enabled)
    {
        if (service_)
        {
            service_->setDeviceName(buildDeviceName(active_protocol_));
            if (!service_->isRunning())
            {
                bleManagerLog("[BLE][nrf52] setEnabled resume proto=%u",
                              static_cast<unsigned>(active_protocol_));
                service_->start();
            }
        }
        else
        {
            bleManagerLog("[BLE][nrf52] setEnabled on proto=%u",
                          static_cast<unsigned>(ctx_.bleConfig().mesh_protocol));
            restartService(ctx_.bleConfig().mesh_protocol);
        }
    }
    else
    {
        if (service_ && service_->isRunning())
        {
            bleManagerLog("[BLE][nrf52] setEnabled off");
            service_->stop();
        }
    }
}

bool BleManager::isEnabled() const
{
    return service_ && service_->isRunning();
}

void BleManager::update()
{
    const auto current_protocol = ctx_.bleConfig().mesh_protocol;
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
    bleManagerLog("[BLE][nrf52] restart begin from=%u to=%u has_service=%u",
                  static_cast<unsigned>(active_protocol_),
                  static_cast<unsigned>(protocol),
                  service_ ? 1U : 0U);
    if (service_)
    {
        bleManagerLog("[BLE][nrf52] restart stopping old service");
        service_->stop();
        service_.reset();
        bleManagerLog("[BLE][nrf52] restart old service stopped");
    }

    active_protocol_ = protocol;
    const std::string device_name = buildDeviceName(protocol);
    const char* protocol_name = chat::infra::meshProtocolSlug(active_protocol_);

    switch (active_protocol_)
    {
    case chat::MeshProtocol::MeshCore:
        service_ = std::unique_ptr<BleService>(new MeshCoreBleService(ctx_.bleAppFacade(), device_name));
        break;
    case chat::MeshProtocol::Meshtastic:
    default:
        service_ = std::unique_ptr<BleService>(new MeshtasticBleService(ctx_.bleAppFacade(), device_name));
        break;
    }

    if (service_)
    {
        bleManagerLog("[BLE][nrf52] restart starting protocol=%s", protocol_name);
        service_->start();
        bleManagerLog("[BLE][nrf52] protocol=%s name=%s service=started",
                      protocol_name,
                      device_name.c_str());
    }
    else
    {
        bleManagerLog("[BLE][nrf52] protocol=%s name=%s service=create_failed",
                      protocol_name,
                      device_name.c_str());
    }
}

std::string BleManager::buildDeviceName(chat::MeshProtocol protocol) const
{
    char long_name[32] = {};
    char short_name[16] = {};
    ctx_.bleEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));

    chat::runtime::EffectiveSelfIdentity identity{};
    identity.node_id = ctx_.bleSelfNodeId();
    std::strncpy(identity.long_name, long_name, sizeof(identity.long_name) - 1);
    identity.long_name[sizeof(identity.long_name) - 1] = '\0';
    std::strncpy(identity.short_name, short_name, sizeof(identity.short_name) - 1);
    identity.short_name[sizeof(identity.short_name) - 1] = '\0';

    char visible_name[32] = {};
    chat::runtime::buildBleVisibleName(identity, protocol, visible_name, sizeof(visible_name));
    return visible_name;
}

} // namespace ble
