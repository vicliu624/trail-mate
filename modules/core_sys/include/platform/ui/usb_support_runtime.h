#pragma once

namespace platform::ui::usb_support
{

struct Status
{
    bool active = false;
    bool stop_requested = false;
    const char* message = "";
};

bool is_supported();
bool start();
void stop();
Status get_status();
void prepare_mass_storage_mode();
void restore_mass_storage_mode();

} // namespace platform::ui::usb_support
