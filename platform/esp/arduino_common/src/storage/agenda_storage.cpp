#include "platform/esp/arduino_common/storage/agenda_storage.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

namespace platform::esp::arduino_common::storage
{
uint32_t agenda_storage_session()
{
    return sd_media_session();
}
bool agenda_storage_available()
{
    if (sd_external_block_owner_active()) return false;
    if (!sd_card_ready())
    {
        sd_recover_media();
        return false; // Observe unavailable before validating a recovered store.
    }
    const auto status = sd_probe_media();
    if (status == SdMediaStatus::IoError || status == SdMediaStatus::Unavailable)
    {
        sd_recover_media();
        return false;
    }
    // Contention is not a media lifetime boundary. Individual record operations
    // retain their own locking/error results and cannot claim an unwritten save.
    return status == SdMediaStatus::Ready || status == SdMediaStatus::Busy;
}
bool prepare_agenda_storage()
{
    if (!sd_card_ready() || sd_external_block_owner_active()) return false;
    if (sd_is_directory("/agenda")) return true;
    // The shared runtime owns mounting and SPI/SDMMC access policy.
    // An I/O failure must never cause formatting or an internal-flash fallback.
    return sd_mkdir("/agenda") && sd_is_directory("/agenda");
}
} // namespace platform::esp::arduino_common::storage
