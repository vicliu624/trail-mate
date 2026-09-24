#pragma once

#include <SdFatConfig.h>
#include <new>

namespace platform::esp::arduino_common::storage
{
// SdFat keeps FatFile/ExFatFile state inline. Abandoning a stale handle must
// release that state without syncing its old directory entry to a new medium.
template <typename File>
void abandon_sd_file(File& file)
{
    static_assert(DESTRUCTOR_CLOSES_FILE == 0, "Abandoning stale files must not write to a new medium");
    file.~File();
    new (&file) File;
}
} // namespace platform::esp::arduino_common::storage
