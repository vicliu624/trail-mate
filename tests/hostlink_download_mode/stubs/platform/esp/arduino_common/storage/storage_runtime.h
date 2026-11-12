#pragma once
namespace platform::esp::arduino_common::storage
{
bool deferred_storage_stopped();
void stop_deferred_storage();
void resume_deferred_storage();
} // namespace platform::esp::arduino_common::storage
