#include "chat/ble/meshtastic_phone_session.h"

namespace ble
{

MeshtasticPhoneSession::MeshtasticPhoneSession(IPhoneRuntimeContext& ctx, MeshtasticPhoneTransport& transport,
                                               MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks,
                                               MeshtasticPhoneModuleConfigHooks* module_config_hooks,
                                               MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks,
                                               MeshtasticPhoneStatusHooks* status_hooks,
                                               MeshtasticPhoneMqttHooks* mqtt_hooks,
                                               MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks)
    : core_(ctx,
            transport,
            bluetooth_config_hooks,
            module_config_hooks,
            config_lifecycle_hooks,
            status_hooks,
            mqtt_hooks,
            device_runtime_hooks)
{
}

void MeshtasticPhoneSession::close()
{
    core_.reset();
}

void MeshtasticPhoneSession::pumpIncomingAppData()
{
    core_.pumpIncomingAppData();
}

bool MeshtasticPhoneSession::isSendingPackets() const
{
    return core_.isSendingPackets();
}

bool MeshtasticPhoneSession::isConfigFlowActive() const
{
    return core_.isConfigFlowActive();
}

bool MeshtasticPhoneSession::handleToRadio(const uint8_t* buf, size_t len)
{
    return core_.handleToRadio(buf, len);
}

bool MeshtasticPhoneSession::popToPhone(MeshtasticBleFrame* out)
{
    return core_.popToPhone(out);
}

void MeshtasticPhoneSession::onIncomingText(const chat::MeshIncomingText& msg)
{
    core_.onIncomingText(msg);
}

void MeshtasticPhoneSession::onIncomingData(const chat::MeshIncomingData& msg)
{
    core_.onIncomingData(msg);
}

} // namespace ble
