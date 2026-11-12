#pragma once
namespace platform::esp::arduino_common::storage
{
inline bool external_owner = false;
inline bool sd_external_block_owner_active() { return external_owner; }
} // namespace platform::esp::arduino_common::storage
