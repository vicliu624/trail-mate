#include "esp32_lvgl_arduino_agenda.h"
#include <cassert>

int main()
{
    // Unsupported targets link the inert adapter, without LVGL, storage,
    // the clock adapter or the Agenda composition as dependencies.
    namespace agenda = trailmate::apps::esp32_lvgl::arduino_agenda;
    assert(!agenda::application());
    agenda::initialize();
    agenda::tick();
    assert(!agenda::application());
}
