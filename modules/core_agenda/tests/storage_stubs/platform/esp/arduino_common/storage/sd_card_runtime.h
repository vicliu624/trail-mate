#pragma once
#include <cstdint>
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready();
uint32_t sd_media_session();
enum class SdMediaStatus : uint8_t
{
    Ready,
    Busy,
    Unavailable,
    IoError
};
SdMediaStatus sd_probe_media();
bool sd_recover_media();
bool sd_external_block_owner_active();
bool sd_is_directory(const char* path);
bool sd_mkdir(const char* path);
} // namespace platform::esp::arduino_common::storage
