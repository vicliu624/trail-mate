#pragma once
#include <cstdint>

namespace platform::esp::arduino_common::storage
{
// Requires application-owned SD storage already mounted by the shared runtime.
// Creates only the record directory; never mounts, formats or uses internal flash.
bool prepare_agenda_storage();
// Logical mount/ownership gate; physical media health is owned by SD runtime.
bool agenda_storage_available();
uint32_t agenda_storage_session();
constexpr const char* kAgendaFile = "/agenda/events.dat";
constexpr const char* kAgendaInitializationFile = "/agenda/events.init";
} // namespace platform::esp::arduino_common::storage
