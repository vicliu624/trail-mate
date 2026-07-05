#pragma once

#include "app/app_config.h"
#include "platform/nrf52/arduino_common/settings_file_store.h"

#include <cstdint>

namespace boards::t_impulse_plus::settings_store
{

using StoreStatus = ::platform::nrf52::arduino_common::settings_file::StoreStatus;

void normalizeConfig(app::AppConfig& config);
bool loadAppConfig(app::AppConfig& config);
void cacheAppConfig(const app::AppConfig& config);
bool saveAppConfig(const app::AppConfig& config);
void queueSaveAppConfig(const app::AppConfig& config);
uint8_t loadMessageToneVolume();
bool saveMessageToneVolume(uint8_t volume);
void queueSaveMessageToneVolume(uint8_t volume);
bool tickDeferredSave();
bool hasDeferredSavePending();
StoreStatus lastLoadStatus();
StoreStatus lastSaveStatus();
const char* statusLabel(StoreStatus status);

} // namespace boards::t_impulse_plus::settings_store
