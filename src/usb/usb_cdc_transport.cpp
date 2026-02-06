#include "usb_cdc_transport.h"

#include "sdkconfig.h"
#include <Arduino.h>
#include <USB.h>
#include <USBCDC.h>
#include <esp_event.h>

namespace usb_cdc
{

namespace
{
Status s_status{};

#if CONFIG_TINYUSB_CDC_ENABLED
USBCDC s_cdc;

void cdc_event_cb(void* /*arg*/, esp_event_base_t /*event_base*/, int32_t event_id, void* event_data)
{
    if (event_id == ARDUINO_USB_CDC_CONNECTED_EVENT)
    {
        s_status.connected = true;
        return;
    }
    if (event_id == ARDUINO_USB_CDC_DISCONNECTED_EVENT)
    {
        s_status.connected = false;
        s_status.dtr = false;
        return;
    }
    if (event_id == ARDUINO_USB_CDC_LINE_STATE_EVENT)
    {
        auto* data = static_cast<arduino_usb_cdc_event_data_t*>(event_data);
        if (data)
        {
            s_status.dtr = data->line_state.dtr;
        }
    }
}
#endif
} // namespace

bool start()
{
#if CONFIG_TINYUSB_CDC_ENABLED
    if (s_status.started)
    {
        return true;
    }
    s_status.started = true;
    s_status.connected = false;
    s_status.dtr = false;

    s_cdc.enableReboot(false);
    s_cdc.setRxBufferSize(2048);
    s_cdc.setTxTimeoutMs(10);
    s_cdc.onEvent(cdc_event_cb);
    s_cdc.begin();

    USB.begin();
    return true;
#else
    return false;
#endif
}

void stop()
{
#if CONFIG_TINYUSB_CDC_ENABLED
    if (!s_status.started)
    {
        return;
    }
    s_cdc.end();
    s_status.started = false;
    s_status.connected = false;
    s_status.dtr = false;
#endif
}

size_t read(uint8_t* buffer, size_t max_len)
{
#if CONFIG_TINYUSB_CDC_ENABLED
    if (!s_status.started || !buffer || max_len == 0)
    {
        return 0;
    }
    return s_cdc.read(buffer, max_len);
#else
    (void)buffer;
    (void)max_len;
    return 0;
#endif
}

size_t write(const uint8_t* data, size_t len)
{
#if CONFIG_TINYUSB_CDC_ENABLED
    if (!s_status.started || !data || len == 0)
    {
        return 0;
    }
    return s_cdc.write(data, len);
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

bool is_connected()
{
#if CONFIG_TINYUSB_CDC_ENABLED
    return s_status.started && s_status.connected && s_status.dtr;
#else
    return false;
#endif
}

Status get_status()
{
    return s_status;
}

} // namespace usb_cdc
