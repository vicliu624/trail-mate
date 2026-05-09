#include "ble/ble_manager.h"

#include "ble/ble_uuids.h"
#include "ble/meshcore_ble.h"
#include "ble/meshtastic_ble.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace ble
{
namespace
{
constexpr std::size_t kBleMinInternalFreeBytes = 48 * 1024;
constexpr std::size_t kBleMinInternalLargestBlockBytes = 20 * 1024;

std::size_t ble_internal_free_bytes()
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

std::size_t ble_internal_largest_block_bytes()
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void log_ble_heap_snapshot(const char* stage)
{
    Serial.printf("[BLE][MEM] %s ram_free=%u ram_largest=%u psram_free=%u psram_largest=%u\n",
                  stage ? stage : "state",
                  static_cast<unsigned>(ble_internal_free_bytes()),
                  static_cast<unsigned>(ble_internal_largest_block_bytes()),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

bool has_ble_startup_headroom()
{
    return ble_internal_free_bytes() >= kBleMinInternalFreeBytes &&
           ble_internal_largest_block_bytes() >= kBleMinInternalLargestBlockBytes;
}
} // namespace

BleManager::BleManager(app::IAppBleFacade& ctx)
    : ctx_(ctx),
      active_protocol_(ctx.getMeshProtocol())
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
    const auto current_protocol = ctx_.getMeshProtocol();
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

void BleManager::setEnabled(bool enabled)
{
    if (enabled)
    {
        if (!service_)
        {
            restartService(ctx_.getMeshProtocol());
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

    if (protocol == chat::MeshProtocol::RNode || protocol == chat::MeshProtocol::LXMF)
    {
        active_protocol_ = protocol;
        Serial.printf("[BLE] protocol=%s has no BLE service yet\n",
                      chat::infra::meshProtocolSlug(active_protocol_));
        return;
    }

    const std::string device_name = buildDeviceName(protocol);

    log_ble_heap_snapshot("before init");
    if (!has_ble_startup_headroom())
    {
        active_protocol_ = protocol;
        Serial.printf("[BLE] skip start protocol=%s reason=low_internal_ram free=%u largest=%u min_free=%u min_largest=%u\n",
                      chat::infra::meshProtocolSlug(protocol),
                      static_cast<unsigned>(ble_internal_free_bytes()),
                      static_cast<unsigned>(ble_internal_largest_block_bytes()),
                      static_cast<unsigned>(kBleMinInternalFreeBytes),
                      static_cast<unsigned>(kBleMinInternalLargestBlockBytes));
        return;
    }

    if (!NimBLEDevice::init(device_name))
    {
        active_protocol_ = protocol;
        Serial.printf("[BLE] init failed protocol=%s name=%s\n",
                      chat::infra::meshProtocolSlug(protocol),
                      device_name.c_str());
        log_ble_heap_snapshot("init failed");
        return;
    }

    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    nimble_initialized_ = true;
    log_ble_heap_snapshot("after init");

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
        if (!service_->start())
        {
            Serial.printf("[BLE] service start failed protocol=%s name=%s\n",
                          protocol_name,
                          device_name.c_str());
            service_->stop();
            service_.reset();
            shutdownNimble();
            log_ble_heap_snapshot("service start failed");
        }
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
