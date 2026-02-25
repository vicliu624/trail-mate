#include "ble_manager.h"

#include "meshcore_ble.h"
#include "meshtastic_ble.h"
#include "../app/app_context.h"
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
    restartService(active_protocol_);
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

void BleManager::restartService(chat::MeshProtocol protocol)
{
    if (service_)
    {
        service_->stop();
        service_.reset();
    }

    shutdownNimble();

    const std::string device_name = buildDeviceName();

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

std::string BleManager::buildDeviceName() const
{
    const auto& cfg = ctx_.getConfig();
    if (cfg.node_name[0] != '\0')
    {
        return std::string(cfg.node_name);
    }

    char buf[32];
    uint32_t suffix = static_cast<uint32_t>(ctx_.getSelfNodeId() & 0xFFFF);
    snprintf(buf, sizeof(buf), "lilygo-%04X", static_cast<unsigned>(suffix));
    return std::string(buf);
}

} // namespace ble
