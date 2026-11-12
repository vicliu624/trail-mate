#pragma once
enum restart_type_t
{
    RESTART_BOOTLOADER
};
void usb_persist_restart(restart_type_t);
