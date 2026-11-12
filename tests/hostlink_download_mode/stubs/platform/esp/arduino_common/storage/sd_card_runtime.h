#pragma once
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready();
bool sd_external_block_owner_active();
bool sd_set_external_block_owner_active(bool);
} // namespace platform::esp::arduino_common::storage
