#include "platform/ui/usb_support_runtime.h"

#include "app/app_facade_access.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/ui/device_runtime.h"
#include "screen_sleep.h"
#include "team/usecase/team_pairing_service.h"

#include <cstdio>

#if defined(ARDUINO_USB_MODE)
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_event.h>

#include <cstring>
#endif

namespace platform::ui::usb_support
{
namespace
{

Status s_status{};
char s_message[96] = "";
bool s_prepared = false;

void set_status_message(const char* message)
{
    const char* source = (message && message[0] != '\0') ? message : "";
    std::snprintf(s_message, sizeof(s_message), "%s", source);
    s_status.message = s_message;
}

void stop_pairing()
{
    if (team::TeamPairingService* pairing = app::teamFacade().getTeamPairing())
    {
        pairing->stop();
    }
}

#if defined(ARDUINO_USB_MODE)
USBMSC s_msc;
bool s_backend_started = false;

int32_t usbReadCallback(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    (void)offset;
    const uint32_t sec_size = SD.sectorSize();
    if (sec_size == 0)
    {
        return -1;
    }

    for (uint32_t index = 0; index < bufsize / sec_size; ++index)
    {
        if (!SD.readRAW(reinterpret_cast<uint8_t*>(buffer) + (index * sec_size), lba + index))
        {
            return -1;
        }
    }

    return static_cast<int32_t>(bufsize);
}

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    (void)offset;
    uint64_t free_space = SD.totalBytes() - SD.usedBytes();
    if (bufsize > free_space)
    {
        return -1;
    }

    const uint32_t sec_size = SD.sectorSize();
    if (sec_size == 0)
    {
        return -1;
    }

    for (uint32_t index = 0; index < bufsize / sec_size; ++index)
    {
        uint8_t blk_buffer[512];
        if (sec_size > sizeof(blk_buffer))
        {
            return -1;
        }
        std::memcpy(blk_buffer, buffer + sec_size * index, sec_size);
        if (!SD.writeRAW(blk_buffer, lba + index))
        {
            return -1;
        }
    }

    return static_cast<int32_t>(bufsize);
}

bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    if (!start && load_eject)
    {
        s_status.stop_requested = true;
        set_status_message("USB host requested eject");
        return false;
    }

    return true;
}

void setup_usb_msc()
{
    uint64_t card_size = SD.cardSize();
    uint32_t sec_size = SD.sectorSize();
    if (card_size == 0 || sec_size == 0)
    {
        set_status_message("SD card not ready");
        return;
    }

    uint32_t num_sectors = static_cast<uint32_t>(card_size / sec_size);
    s_msc.vendorID("TrailMate");
    s_msc.productID("SD Card");
    s_msc.productRevision("1.0");
    s_msc.onRead(usbReadCallback);
    s_msc.onWrite(usbWriteCallback);
    s_msc.onStartStop(usbStartStopCallback);
    s_msc.mediaPresent(true);
    s_msc.begin(num_sectors, sec_size);

    USB.onEvent([](void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
    {
        (void)arg;
        (void)event_data;
        if (event_base != ARDUINO_USB_EVENTS)
        {
            return;
        }

        switch (event_id)
        {
        case ARDUINO_USB_STARTED_EVENT:
            set_status_message("USB Started - Ready");
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            set_status_message("USB Stopped");
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            set_status_message("USB Suspended");
            break;
        case ARDUINO_USB_RESUME_EVENT:
            set_status_message("USB Resumed");
            break;
        default:
            break;
        }
    });

    USB.begin();
    set_status_message("Initializing USB...");
    s_backend_started = true;
}
#endif

} // namespace

bool is_supported()
{
#if defined(ARDUINO_USB_MODE)
    return true;
#else
    return false;
#endif
}

void prepare_mass_storage_mode()
{
    stop_pairing();
    esp_wifi_stop();
    disableScreenSleep();

    TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
    if (gps_task_handle != nullptr)
    {
        vTaskSuspend(gps_task_handle);
    }
}

void restore_mass_storage_mode()
{
    enableScreenSleep();

    TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
    if (gps_task_handle != nullptr)
    {
        vTaskResume(gps_task_handle);
    }
}

bool start()
{
    s_status = Status{};
    s_status.message = s_message;
    s_status.stop_requested = false;

    if (!is_supported())
    {
        set_status_message("USB is unavailable on this target");
        return false;
    }

#if defined(ARDUINO_USB_MODE)
    if (s_backend_started)
    {
        s_status.active = true;
        return true;
    }

    if (!platform::ui::device::card_ready())
    {
        set_status_message("SD Card Not Found");
        return false;
    }

    prepare_mass_storage_mode();
    s_prepared = true;
    setup_usb_msc();
    platform::ui::device::delay_ms(500);
    s_status.active = s_backend_started;
    if (s_backend_started && s_message[0] == '\0')
    {
        set_status_message("USB Started - Ready");
    }
    return s_backend_started;
#else
    return false;
#endif
}

void stop()
{
#if defined(ARDUINO_USB_MODE)
    if (s_backend_started)
    {
        s_msc.end();
        s_backend_started = false;
    }
#endif

    if (s_prepared)
    {
        restore_mass_storage_mode();
        s_prepared = false;
    }

    s_status.active = false;
    s_status.stop_requested = false;
    set_status_message("USB Stopped");
}

Status get_status()
{
    s_status.message = s_message;
    return s_status;
}

} // namespace platform::ui::usb_support
