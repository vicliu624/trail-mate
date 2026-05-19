#pragma once

namespace platform::ui::settings_backup
{

struct Status
{
    bool supported = false;
    bool sd_present = false;
    bool has_backup = false;
    bool busy = false;
    char message[96] = {};
    char detail[128] = {};
};

bool is_supported();
Status status();
bool backup();
bool restore();
bool remove();

} // namespace platform::ui::settings_backup
