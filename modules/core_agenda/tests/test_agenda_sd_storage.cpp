#include "platform/esp/arduino_common/storage/agenda_storage.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <cassert>
#include <cstring>

namespace
{
bool ready = false;
bool external = false;
bool directory = false;
bool writable = true;
unsigned creates = 0;
unsigned recoveries = 0;
auto media_status = platform::esp::arduino_common::storage::SdMediaStatus::Ready;
} // namespace
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return ready; }
uint32_t sd_media_session() { return 1; }
SdMediaStatus sd_probe_media() { return media_status; }
bool sd_recover_media()
{
    ++recoveries;
    return false;
}
bool sd_external_block_owner_active() { return external; }
bool sd_is_directory(const char* path)
{
    assert(!std::strcmp(path, "/agenda"));
    return directory;
}
bool sd_mkdir(const char* path)
{
    assert(!std::strcmp(path, "/agenda"));
    ++creates;
    if (!writable) return false;
    directory = true;
    return true;
}
} // namespace platform::esp::arduino_common::storage
int main()
{
    using platform::esp::arduino_common::storage::prepare_agenda_storage;
    assert(!prepare_agenda_storage() && creates == 0);
    ready = true;
    external = true;
    assert(!prepare_agenda_storage() && creates == 0);
    external = false;
    writable = false;
    assert(!prepare_agenda_storage() && creates == 1);
    writable = true;
    assert(prepare_agenda_storage() && creates == 2);
    assert(prepare_agenda_storage() && creates == 2);
    ready = false;
    assert(!prepare_agenda_storage() && creates == 2);
    ready = true;
    assert(prepare_agenda_storage() && creates == 2);
    using namespace platform::esp::arduino_common::storage;
    assert(agenda_storage_available());
    media_status = SdMediaStatus::Busy;
    assert(agenda_storage_available()); // A busy transport is not card removal.
    assert(recoveries == 0);
    media_status = SdMediaStatus::IoError;
    assert(!agenda_storage_available());
    media_status = SdMediaStatus::Unavailable;
    assert(!agenda_storage_available());
    assert(recoveries == 2);
    external = true;
    assert(!agenda_storage_available() && recoveries == 2);
    external = false;
    ready = false;
    assert(!agenda_storage_available() && recoveries == 3);
}
