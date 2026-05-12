#pragma once

#include "phone/meshtastic/meshtastic_phone_core.h"

namespace ble
{

using MeshtasticBleFrame = ::phone::meshtastic::MeshtasticBleFrame;
using MeshtasticGpsFix = ::phone::meshtastic::MeshtasticGpsFix;
using MeshtasticPhoneTransport = ::phone::meshtastic::MeshtasticPhoneTransport;
using MeshtasticPhoneBluetoothConfigHooks = ::phone::meshtastic::MeshtasticPhoneBluetoothConfigHooks;
using MeshtasticPhoneModuleConfigHooks = ::phone::meshtastic::MeshtasticPhoneModuleConfigHooks;
using MeshtasticPhoneConfigLifecycleHooks = ::phone::meshtastic::MeshtasticPhoneConfigLifecycleHooks;
using MeshtasticPhoneStatusHooks = ::phone::meshtastic::MeshtasticPhoneStatusHooks;
using MeshtasticPhoneMqttHooks = ::phone::meshtastic::MeshtasticPhoneMqttHooks;
using MeshtasticPhoneDeviceRuntimeHooks = ::phone::meshtastic::MeshtasticPhoneDeviceRuntimeHooks;
using MeshtasticPhoneCore = ::phone::meshtastic::MeshtasticPhoneCore;

} // namespace ble
