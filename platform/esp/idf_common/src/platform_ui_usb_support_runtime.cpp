#include "platform/ui/usb_support_runtime.h"

#include "app/app_facade_access.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "screen_sleep.h"
#include "team/usecase/team_pairing_service.h"

#include <cstdio>

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "platform/esp/boards/tab5_board_profile.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
extern "C" esp_err_t bsp_sdcard_init(char* mount_point, size_t max_files);
extern "C" esp_err_t bsp_sdcard_deinit(char* mount_point);
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

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) && defined(CONFIG_TINYUSB_MSC_ENABLED)
constexpr const char* kUsbVendor = "TrailMate";
constexpr const char* kUsbProduct = "USB Disk";
constexpr const char* kUsbSerial = "TM-IDF";
constexpr int kTab5SdLdoChan = 4;
constexpr uint8_t kInterfaceMsc = 0;
constexpr uint8_t kInterfaceTotal = 1;
constexpr uint8_t kEndpointMscOut = 0x01;
constexpr uint8_t kEndpointMscIn = 0x81;
constexpr uint16_t kUsbConfigDescLen = TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN;

bool s_usb_installed = false;
bool s_storage_initialized = false;
sdmmc_card_t* s_card = nullptr;
sdmmc_host_t s_sd_host = SDMMC_HOST_DEFAULT();
sd_pwr_ctrl_handle_t s_pwr_ctrl_handle = nullptr;

static tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(s_device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static char const* s_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    kUsbVendor,
    kUsbProduct,
    kUsbSerial,
    "MSC",
};

static uint8_t const s_msc_fs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, kInterfaceTotal, 0, kUsbConfigDescLen, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(kInterfaceMsc, 0, kEndpointMscOut, kEndpointMscIn, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const tusb_desc_device_qualifier_t s_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0,
};

static uint8_t const s_msc_hs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, kInterfaceTotal, 0, kUsbConfigDescLen, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(kInterfaceMsc, 0, kEndpointMscOut, kEndpointMscIn, 512),
};
#endif

void refresh_runtime_message()
{
    if (!s_status.active)
    {
        return;
    }

    if (tinyusb_msc_storage_in_use_by_usb_host())
    {
        set_status_message("USB host connected");
    }
    else
    {
        set_status_message("Waiting for host...");
    }
}

void storage_mount_changed_cb(tinyusb_msc_event_t* event)
{
    if (!event)
    {
        return;
    }

    if (event->mount_changed_data.is_mounted)
    {
        set_status_message("Storage mounted to app");
    }
    else
    {
        set_status_message("USB host connected");
    }
}

void deinit_sd_host()
{
    if (s_card)
    {
        free(s_card);
        s_card = nullptr;
    }

    if (s_sd_host.flags & SDMMC_HOST_FLAG_DEINIT_ARG)
    {
        s_sd_host.deinit_p(s_sd_host.slot);
    }
    else if (s_sd_host.deinit)
    {
        (*s_sd_host.deinit)();
    }

    if (s_pwr_ctrl_handle)
    {
        (void)sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl_handle);
        s_pwr_ctrl_handle = nullptr;
    }
}

esp_err_t init_sd_host_raw()
{
    if (s_card)
    {
        return ESP_OK;
    }

    s_sd_host = SDMMC_HOST_DEFAULT();
    s_sd_host.slot = SDMMC_HOST_SLOT_0;
    s_sd_host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = kTab5SdLdoChan,
    };

    if (!s_pwr_ctrl_handle)
    {
        const esp_err_t ldo_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl_handle);
        if (ldo_err != ESP_OK)
        {
            return ldo_err;
        }
    }
    s_sd_host.pwr_ctrl_handle = s_pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = static_cast<gpio_num_t>(platform::esp::boards::tab5::kBoardProfile.sdmmc.clk);
    slot_config.cmd = static_cast<gpio_num_t>(platform::esp::boards::tab5::kBoardProfile.sdmmc.cmd);
    slot_config.d0 = static_cast<gpio_num_t>(platform::esp::boards::tab5::kBoardProfile.sdmmc.d0);
    slot_config.d1 = static_cast<gpio_num_t>(platform::esp::boards::tab5::kBoardProfile.sdmmc.d1);
    slot_config.d2 = static_cast<gpio_num_t>(platform::esp::boards::tab5::kBoardProfile.sdmmc.d2);
    slot_config.d3 = static_cast<gpio_num_t>(platform::esp::boards::tab5::kBoardProfile.sdmmc.d3);

    s_card = static_cast<sdmmc_card_t*>(malloc(sizeof(sdmmc_card_t)));
    if (!s_card)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = (*s_sd_host.init)();
    if (err != ESP_OK)
    {
        deinit_sd_host();
        return err;
    }

    err = sdmmc_host_init_slot(s_sd_host.slot, &slot_config);
    if (err != ESP_OK)
    {
        deinit_sd_host();
        return err;
    }

    err = sdmmc_card_init(&s_sd_host, s_card);
    if (err != ESP_OK)
    {
        deinit_sd_host();
        return err;
    }

    return ESP_OK;
}

bool start_backend()
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        set_status_message("SD card not ready");
        return false;
    }

    (void)bsp_sdcard_deinit(const_cast<char*>(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()));

    const esp_err_t sd_err = init_sd_host_raw();
    if (sd_err != ESP_OK)
    {
        set_status_message("Raw SD init failed");
        (void)bsp_sdcard_init(const_cast<char*>(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()), 8);
        return false;
    }

    esp_vfs_fat_mount_config_t mount_cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_cfg.max_files = 8;
    tinyusb_msc_sdmmc_config_t config_sdmmc = {
        s_card,
        storage_mount_changed_cb,
        nullptr,
        mount_cfg,
    };
    if (tinyusb_msc_storage_init_sdmmc(&config_sdmmc) != ESP_OK)
    {
        set_status_message("USB storage init failed");
        deinit_sd_host();
        (void)bsp_sdcard_init(const_cast<char*>(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()), 8);
        return false;
    }
    s_storage_initialized = true;
    (void)tinyusb_msc_register_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED, storage_mount_changed_cb);

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &s_device_descriptor,
        .string_descriptor = s_string_descriptors,
        .string_descriptor_count = sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = s_msc_fs_configuration_desc,
        .hs_configuration_descriptor = s_msc_hs_configuration_desc,
        .qualifier_descriptor = &s_device_qualifier,
#else
        .configuration_descriptor = s_msc_fs_configuration_desc,
#endif
    };
    if (tinyusb_driver_install(&tusb_cfg) != ESP_OK)
    {
        (void)tinyusb_msc_unregister_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED);
        tinyusb_msc_storage_deinit();
        s_storage_initialized = false;
        deinit_sd_host();
        (void)bsp_sdcard_init(const_cast<char*>(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()), 8);
        set_status_message("TinyUSB install failed");
        return false;
    }

    s_usb_installed = true;
    s_status.active = true;
    refresh_runtime_message();
    return true;
}

void stop_backend()
{
    if (s_usb_installed)
    {
        tinyusb_driver_uninstall();
        s_usb_installed = false;
    }

    if (s_storage_initialized)
    {
        (void)tinyusb_msc_unregister_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED);
        tinyusb_msc_storage_deinit();
        s_storage_initialized = false;
    }

    deinit_sd_host();
    (void)bsp_sdcard_init(const_cast<char*>(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()), 8);
}
#endif

} // namespace

bool is_supported()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) && defined(CONFIG_TINYUSB_MSC_ENABLED)
    return true;
#else
    return false;
#endif
}

void prepare_mass_storage_mode()
{
    stop_pairing();
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
        set_status_message("USB MSC is unavailable on this IDF target");
        return false;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) && defined(CONFIG_TINYUSB_MSC_ENABLED)
    if (s_status.active)
    {
        refresh_runtime_message();
        return true;
    }

    prepare_mass_storage_mode();
    s_prepared = true;
    const bool ok = start_backend();
    if (!ok)
    {
        restore_mass_storage_mode();
        s_prepared = false;
        s_status.active = false;
        return false;
    }
    s_status.active = true;
    refresh_runtime_message();
    return true;
#else
    return false;
#endif
}

void stop()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) && defined(CONFIG_TINYUSB_MSC_ENABLED)
    if (s_status.active)
    {
        stop_backend();
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
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) && defined(CONFIG_TINYUSB_MSC_ENABLED)
    if (s_status.active)
    {
        refresh_runtime_message();
    }
#endif
    s_status.message = s_message;
    return s_status;
}

} // namespace platform::ui::usb_support
