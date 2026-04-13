#pragma once

#include <InternalFileSystem.h>

#include <cstdint>

namespace platform::nrf52::arduino_common::internal_fs
{

using Adafruit_LittleFS_Namespace::File;

bool ensureMounted(bool allow_format_recovery = false, const char* log_tag = nullptr);
void removeIfExists(const char* path);
bool openForOverwrite(const char* path,
                      File* out,
                      bool allow_format_recovery = false,
                      const char* log_tag = nullptr);
bool rewindForOverwrite(File& file);
bool truncateAfterWrite(File& file, uint32_t final_size);
uint32_t accumulateBytes(File dir);

} // namespace platform::nrf52::arduino_common::internal_fs
