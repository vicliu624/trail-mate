#pragma once

#include <cstddef>
#include <cstdint>

namespace usb_cdc
{

struct Status
{
    bool started = false;
    bool connected = false;
    bool dtr = false;
};

bool start();
void stop();
size_t read(uint8_t* buffer, size_t max_len);
size_t write(const uint8_t* data, size_t len);
bool is_connected();
Status get_status();

} // namespace usb_cdc
