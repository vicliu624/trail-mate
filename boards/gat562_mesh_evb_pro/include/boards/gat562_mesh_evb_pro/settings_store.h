#pragma once

#include "app/app_config.h"

#include <cstdint>

namespace boards::gat562_mesh_evb_pro::settings_store
{

enum class StoreStatus : uint8_t
{
    Ok = 0,
    NotFound,
    FsInitFailed,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    FlushFailed,
    HeaderInvalid,
    VersionMismatch,
    PayloadSizeMismatch,
    CrcMismatch,
    RenameFailed,
    BackupFailed,
};

void normalizeConfig(app::AppConfig& config);
bool loadAppConfig(app::AppConfig& config);
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

} // namespace boards::gat562_mesh_evb_pro::settings_store
