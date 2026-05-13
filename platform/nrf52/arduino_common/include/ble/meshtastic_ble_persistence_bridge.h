#pragma once

#include "chat/ble/meshtastic_phone_config_bridge.h"

namespace ble
{

bool loadMeshtasticBlePersistedState(meshtastic_config_bridge::PersistedState* out);
bool saveMeshtasticBlePersistedState(const meshtastic_Config_BluetoothConfig& bluetooth,
                                     const meshtastic_LocalModuleConfig& module);
void logMeshtasticBlePersistenceStatus();

} // namespace ble
