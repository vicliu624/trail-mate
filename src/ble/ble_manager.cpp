#include "ble_manager.h"

#include "../app/app_context.h"
#include "ble_uuids.h"
#include "meshcore_ble.h"
#include "meshtastic_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace ble
{

BleManager::BleManager(app::AppContext& ctx)
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
        const char* protocol_name = (active_protocol_ == chat::MeshProtocol::MeshCore)
                                        ? "meshcore"
                                        : "meshtastic";
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
    const auto& cfg = ctx_.getConfig();
    std::string name;
    if (cfg.node_name[0] != '\0')
    {
        name = std::string(cfg.node_name);
    }
    else
    {
        char buf[32];
        uint32_t suffix = static_cast<uint32_t>(ctx_.getSelfNodeId() & 0xFFFF);
        snprintf(buf, sizeof(buf), "lilygo-%04X", static_cast<unsigned>(suffix));
        name = std::string(buf);
    }

    // BLE advertised name should not include TrailMate branding prefix.
    // Keep the suffix part so the user can still identify the node.
    static const std::string kTrailMatePrefix = "TrailMate-";
    if (name.rfind(kTrailMatePrefix, 0) == 0)
    {
        name.erase(0, kTrailMatePrefix.size());
        if (name.empty())
        {
            char buf[16];
            uint32_t suffix = static_cast<uint32_t>(ctx_.getSelfNodeId() & 0xFFFF);
            snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(suffix));
            name = std::string(buf);
        }
    }

    if (protocol == chat::MeshProtocol::MeshCore)
    {
        static const std::string kMeshCorePrefix = "MeshCore-";
        if (name.rfind(kMeshCorePrefix, 0) != 0)
        {
            name = kMeshCorePrefix + name;
        }
    }
    else if (protocol == chat::MeshProtocol::Meshtastic)
    {
        char meshtastic_name[32];
        snprintf(meshtastic_name, sizeof(meshtastic_name), "Meshtastic_%04X",
                 static_cast<unsigned>(ctx_.getSelfNodeId() & 0xFFFF));
        name = meshtastic_name;
    }

    return name;
}

} // namespace ble
