#include "platform/esp/arduino_common/storage/agenda_storage.h"

#include <FFat.h>
#include <esp_partition.h>
#include <sys/stat.h>

#include <cassert>
#include <cerrno>
#include <cstring>

namespace
{
bool mounted = false;
bool agenda_exists = false;
bool present = true;
bool mountable = false;
bool readable = true;
bool writable = true;
unsigned mounts = 0;
unsigned formats = 0;
unsigned reads = 0;
uint8_t flash[1024];
esp_partition_t partition{sizeof(flash)};

void reset()
{
    mounted = agenda_exists = mountable = false;
    present = readable = writable = true;
    mounts = formats = reads = 0;
    std::memset(flash, 0xff, sizeof(flash));
}
} // namespace

FakeFat FFat;
void yield() {}
bool FakeFat::begin(bool format, const char* path, uint8_t files, const char* label)
{
    assert(std::strcmp(path, "/fs") == 0 && files == 10 && std::strcmp(label, "ffat") == 0);
    ++mounts;
    if (format) ++formats;
    // Simulate wear-level metadata written by the first mount attempt.
    flash[0] = 0;
    mounted = mountable || (format && writable);
    return mounted;
}
const esp_partition_t* esp_partition_find_first(int type, int subtype, const char* label)
{
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_FAT);
    assert(std::strcmp(label, "ffat") == 0);
    return present ? &partition : nullptr;
}
int esp_partition_read(const esp_partition_t* source, std::size_t offset, void* out, std::size_t size)
{
    ++reads;
    assert(source == &partition && offset + size <= sizeof(flash));
    if (!readable) return -1;
    std::memcpy(out, flash + offset, size);
    return ESP_OK;
}
int stat(const char* path, struct stat* info)
{
    const bool exists = std::strcmp(path, "/fs") == 0 ? mounted : agenda_exists;
    info->st_mode = exists ? 040000 : 0;
    return exists ? 0 : -1;
}
int mkdir(const char* path, unsigned mode)
{
    assert(std::strcmp(path, "/fs/agenda") == 0 && mode == 0700);
    if (!mounted || !writable)
    {
        errno = EACCES;
        return -1;
    }
    agenda_exists = true;
    return 0;
}

int main()
{
    using platform::esp::arduino_common::storage::prepare_agenda_storage;
    reset();
    assert(prepare_agenda_storage());
    assert(mounts == 2 && formats == 1 && reads == 8 && agenda_exists);
    assert(prepare_agenda_storage() && mounts == 2); // Shared existing mount.

    reset();
    flash[1023] = 0; // Must inspect the whole partition, not just its header.
    assert(!prepare_agenda_storage() && formats == 0);
    reset();
    readable = false;
    assert(!prepare_agenda_storage() && formats == 0);
    reset();
    present = false;
    assert(!prepare_agenda_storage() && mounts == 0);
    reset();
    flash[0] = 0;
    mountable = true;
    assert(prepare_agenda_storage() && formats == 0);
    reset();
    mounted = true;
    writable = false;
    assert(!prepare_agenda_storage() && mounts == 0 && formats == 0);
}
