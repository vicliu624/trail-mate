#include "usb/usb_cdc_transport.h"

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace usb_cdc
{
namespace
{

Status s_status{};
bool s_driver_owned = false;

void refresh_connection_state()
{
    if (!s_status.started)
    {
        s_status.connected = false;
        s_status.dtr = false;
        return;
    }

    s_status.connected = usb_serial_jtag_is_connected();
    s_status.dtr = s_status.connected;
}

} // namespace

bool start()
{
    if (s_status.started)
    {
        refresh_connection_state();
        return true;
    }

    if (usb_serial_jtag_is_driver_installed())
    {
        s_driver_owned = false;
        s_status.started = true;
        refresh_connection_state();
        return true;
    }

    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    config.rx_buffer_size = 2048;
    config.tx_buffer_size = 2048;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK)
    {
        return false;
    }

    s_driver_owned = true;
    s_status.started = true;
    refresh_connection_state();
    return true;
}

void stop()
{
    if (!s_status.started)
    {
        return;
    }

    if (s_driver_owned)
    {
        (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(20));
        (void)usb_serial_jtag_driver_uninstall();
    }

    s_driver_owned = false;
    s_status = Status{};
}

size_t read(uint8_t* buffer, size_t max_len)
{
    if (!s_status.started || !buffer || max_len == 0)
    {
        return 0;
    }

    const int read_len = usb_serial_jtag_read_bytes(buffer, static_cast<uint32_t>(max_len), 0);
    refresh_connection_state();
    return read_len > 0 ? static_cast<size_t>(read_len) : 0U;
}

size_t write(const uint8_t* data, size_t len)
{
    if (!s_status.started || !data || len == 0)
    {
        return 0;
    }

    const int written = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(10));
    if (written > 0)
    {
        (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(20));
    }
    refresh_connection_state();
    return written > 0 ? static_cast<size_t>(written) : 0U;
}

bool is_connected()
{
    refresh_connection_state();
    return s_status.started && s_status.connected && s_status.dtr;
}

Status get_status()
{
    refresh_connection_state();
    return s_status;
}

} // namespace usb_cdc
