#pragma once

class AppScreen;
namespace ui::map
{
struct MapMarkerBinding;
}

namespace trailmate::apps::esp32_lvgl::arduino_agenda
{
// Target-owned composition, initialized before building the shell catalog.
// No domain services are exposed to the shell or placed in AppContext.
void initialize();
AppScreen* application();
const ui::map::MapMarkerBinding* mapMarkers();
void tick();
} // namespace trailmate::apps::esp32_lvgl::arduino_agenda
