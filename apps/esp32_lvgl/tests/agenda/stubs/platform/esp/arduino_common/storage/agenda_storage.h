#pragma once
#include <cstdint>

// Only replace the mounted path, not the production file-store implementation.
namespace platform::esp::arduino_common::storage
{
bool prepare_agenda_storage();
bool agenda_storage_available();
uint32_t agenda_storage_session();
constexpr const char* kAgendaFile = "agenda-root-test.dat";
constexpr const char* kAgendaInitializationFile = "agenda-root-test.init";
} // namespace platform::esp::arduino_common::storage
