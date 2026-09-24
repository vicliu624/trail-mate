#pragma once
namespace platform::esp::arduino_common::storage
{
// Independent record file on the same required SD card as Agenda.
constexpr const char* kWaypointFile = "/agenda/waypoints.dat";
constexpr const char* kWaypointInitializationFile = "/agenda/waypoints.init";
} // namespace platform::esp::arduino_common::storage
