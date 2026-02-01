#ifdef ARDUINO_USB_MODE

/**
 * @file ui_usb.cpp
 * @brief USB Mass Storage interface implementation
 * 
 * This module implements USB Mass Storage functionality to expose SD card
 * as a USB storage device to the computer.
 * 
 * Credits to @geo_tp for the original POC: https://github.com/geo-tp/Esp32-USB-Stick
 */

#include "ui_usb.h"
#include "ui_common.h"
#include "widgets/top_bar.h"
#include "board/BoardBase.h"
#include "../gps/gps_service_api.h"
#include <USB.h>
#include <USBMSC.h>
#include <SD.h>
#include <Arduino.h>
#include <cstring>  // For memcpy
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Forward declarations from main.cpp
extern void disableScreenSleep();
extern void enableScreenSleep();
// GPS task handle is now accessed via GpsService

static lv_obj_t *status_label = NULL;
static lv_obj_t *root = NULL;
static lv_obj_t *content = NULL;
static lv_obj_t *loading_overlay = NULL;
static lv_obj_t *loading_box = NULL;
static lv_timer_t *exit_timer = NULL;
static ui::widgets::TopBar top_bar;
static USBMSC msc;
static bool should_stop = false;
static bool usb_mode_active = false;  // Track if USB mode is active
static bool usb_exit_started = false;
static bool usb_stopped = false;

// Forward declarations
static void back_event_handler(void* user_data);
static void show_loading(const char *message);
static void stop_usb_async_cb(lv_timer_t* timer);
static void update_status_message(const char *message);
static int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
static int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
static bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject);

/**
 * @brief USB read callback - reads data from SD card
 * 
 * This function reads raw sectors from the SD card using ESP32's SD library
 * readRAW() method. The implementation follows the pattern from Launcher's
 * MassStorage class.
 * 
 * NOTE: Launcher does NOT use SPI lock in USB callbacks - USB operations
 * are time-critical and locking may cause stability issues.
 */
static int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    // Verify sector size (get from SD library, like Launcher does)
    const uint32_t secSize = SD.sectorSize();
    if (secSize == 0) {
        return -1; // disk error
    }
    
    // Read sectors (exactly like Launcher's implementation)
    for (uint32_t x = 0; x < bufsize / secSize; ++x) {
        if (!SD.readRAW(reinterpret_cast<uint8_t *>(buffer) + (x * secSize), lba + x)) {
            return -1; // read error
        }
    }
    
    return bufsize;
}

/**
 * @brief USB write callback - writes data to SD card
 * 
 * This function writes raw sectors to the SD card using ESP32's SD library
 * writeRAW() method. The implementation follows the pattern from Launcher's
 * MassStorage class.
 * 
 * NOTE: Launcher does NOT use SPI lock in USB callbacks - USB operations
 * are time-critical and locking may cause stability issues.
 * Launcher also uses a temporary buffer for each sector write.
 */
static int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // Verify free space (like Launcher does)
    uint64_t freeSpace = SD.totalBytes() - SD.usedBytes();
    if (bufsize > freeSpace) {
        return -1; // no space available
    }
    
    // Verify sector size (get from SD library, like Launcher does)
    const uint32_t secSize = SD.sectorSize();
    if (secSize == 0) {
        return -1; // disk error
    }
    
    // Write sectors (exactly like Launcher's implementation with temporary buffer)
    for (uint32_t x = 0; x < bufsize / secSize; ++x) {
        uint8_t blkBuffer[secSize];
        memcpy(blkBuffer, buffer + secSize * x, secSize);
        if (!SD.writeRAW(blkBuffer, lba + x)) {
            return -1; // write error
        }
    }
    
    return bufsize;
}

/**
 * @brief USB start/stop callback
 * 
 * Handles USB connection/disconnection events (exactly like Launcher's implementation)
 */
static bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject)
{
    if (!start && load_eject) {
        // USB ejected by host
        should_stop = true;
        return false;
    }
    
    // Launcher's implementation is simpler - just return true for start
    return true;
}

/**
 * @brief Update status message on screen
 */
static void update_status_message(const char *message)
{
    if (status_label) {
        lv_label_set_text(status_label, message);
    }
    Serial.printf("[USB] %s\n", message);
}

static void show_loading(const char *message)
{
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) {
        return;
    }

    if (loading_overlay) {
        lv_obj_del(loading_overlay);
        loading_overlay = NULL;
        loading_box = NULL;
    }

    loading_overlay = lv_obj_create(top_layer);
    lv_obj_set_size(loading_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(loading_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(loading_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(loading_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(loading_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(loading_overlay, LV_OBJ_FLAG_CLICKABLE);

    loading_box = lv_obj_create(loading_overlay);
    lv_obj_set_size(loading_box, 160, 80);
    lv_obj_center(loading_box);
    lv_obj_set_style_bg_color(loading_box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(loading_box, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(loading_box, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(loading_box, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(loading_box, 8, LV_PART_MAIN);
    lv_obj_clear_flag(loading_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(loading_box);
    lv_label_set_text(label, message ? message : "Loading...");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
}

static void stop_usb_async_cb(lv_timer_t* timer)
{
    if (timer) {
        lv_timer_del(timer);
    }
    exit_timer = NULL;

    if (!usb_stopped) {
        should_stop = true;
        usb_mode_active = false;
        msc.end();
        ::enableScreenSleep();
        TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
        if (gps_task_handle != NULL) {
            vTaskResume(gps_task_handle);
        }
        usb_stopped = true;
    }

    ui_request_exit_to_menu();
}

/**
 * @brief Setup USB Mass Storage
 * 
 * This function sets up USB Mass Storage following the pattern from Launcher's
 * MassStorage class. It gets SD card sector information and configures the MSC.
 */
static void setup_usb_msc()
{
    // Verify SD card is ready before starting USB MSC
    if (!board.isCardReady()) {
        update_status_message("SD Card Not Ready");
        return;
    }
    
    // Ensure SD card is still accessible (double check)
    if (SD.cardType() == CARD_NONE) {
        update_status_message("SD Card Not Detected");
        return;
    }
    
    // Get sector size and number of sectors (exactly like Launcher's implementation)
    // Use SD library's sectorSize() method (like Launcher uses SDM.sectorSize())
    uint32_t secSize = SD.sectorSize();
    if (secSize == 0) {
        update_status_message("SD Card Sector Error");
        return;
    }
    
    // Calculate number of sectors from card size
    uint64_t cardSize = SD.cardSize();
    uint32_t numSectors = cardSize / secSize;
    
    // Configure USB MSC (similar to Launcher)
    msc.vendorID("TrailMate");
    msc.productID("SD Card");
    msc.productRevision("1.0");
    
    // Set callbacks
    msc.onRead(usbReadCallback);
    msc.onWrite(usbWriteCallback);
    msc.onStartStop(usbStartStopCallback);
    
    msc.mediaPresent(true);
    msc.begin(numSectors, secSize);
    
    // Setup USB event handler (exactly like Launcher's implementation)
    USB.onEvent([](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
        if (event_base == ARDUINO_USB_EVENTS) {
            switch (event_id) {
                case ARDUINO_USB_STARTED_EVENT:
                    update_status_message("USB Started - Ready");
                    break;
                case ARDUINO_USB_STOPPED_EVENT:
                    update_status_message("USB Stopped");
                    break;
                case ARDUINO_USB_SUSPEND_EVENT:
                    update_status_message("USB Suspended");
                    break;
                case ARDUINO_USB_RESUME_EVENT:
                    update_status_message("USB Resumed");
                    break;
                default:
                    break;
            }
        }
    });
    
    // Start USB
    USB.begin();
    update_status_message("Initializing USB...");
}

/**
 * @brief Back button event handler
 */
static void back_event_handler(void* /*user_data*/)
{
    if (usb_exit_started) {
        return;
    }
    usb_exit_started = true;
    show_loading("Exiting USB...");
    update_status_message("Stopping USB...");

    if (exit_timer) {
        lv_timer_del(exit_timer);
        exit_timer = NULL;
    }
    exit_timer = lv_timer_create(stop_usb_async_cb, 100, NULL);
    if (exit_timer) {
        lv_timer_set_repeat_count(exit_timer, 1);
    }
}

/**
 * @brief Enter USB Mass Storage mode
 */
void ui_usb_enter(lv_obj_t *parent)
{
    usb_exit_started = false;
    usb_stopped = false;
    if (exit_timer) {
        lv_timer_del(exit_timer);
        exit_timer = NULL;
    }

    if (root) {
        lv_obj_del(root);
        root = NULL;
        content = NULL;
        status_label = NULL;
        loading_box = NULL;
        if (loading_overlay) {
            lv_obj_del(loading_overlay);
            loading_overlay = NULL;
        }
        top_bar = {};
    }

    root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    ::ui::widgets::top_bar_init(top_bar, root);
    ::ui::widgets::top_bar_set_title(top_bar, "USB Mass Storage");
    ::ui::widgets::top_bar_set_back_callback(top_bar, back_event_handler, nullptr);
    ui_update_top_bar_battery(top_bar);

    content = lv_obj_create(root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Check if SD card is ready
    if (!board.isCardReady()) {
        // Show error message
        lv_obj_t *error_label = lv_label_create(content);
        lv_label_set_text(error_label, "SD Card Not Found\nPlease insert SD card");
        lv_obj_center(error_label);
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF0000), LV_PART_MAIN);
        return;
    }
    
    should_stop = false;
    
    // Disable screen sleep during USB mode to keep USB functionality active
    disableScreenSleep();
    
        // Suspend GPS task during USB mode to prevent any potential interference
        // GPS uses serial port, but suspending it ensures no unexpected resource conflicts
        TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
        if (gps_task_handle != NULL) {
            vTaskSuspend(gps_task_handle);
        }
    
    // Create status label
    status_label = lv_label_create(content);
    lv_label_set_text(status_label, "Initializing...");
    lv_obj_center(status_label);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    
    // Create info label
    lv_obj_t *info_label = lv_label_create(content);
    lv_label_set_text(info_label, "Press Back to exit USB mode");
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x606060), LV_PART_MAIN);
    lv_obj_set_style_text_opa(info_label, LV_OPA_80, LV_PART_MAIN);
    
    // Setup USB MSC
    setup_usb_msc();
    
    // Wait a bit for USB to initialize (like Launcher does)
    vTaskDelay(pdTICKS_TO_MS(500));
    
    // Mark USB mode as active (main loop will handle the USB loop)
    usb_mode_active = true;
}

/**
 * @brief Exit USB Mass Storage mode
 * 
 * This function cleans up USB MSC and restores USB to normal mode,
 * following the pattern from Launcher's MassStorage destructor.
 */
void ui_usb_exit(lv_obj_t *parent)
{
    (void)parent;
    
    if (exit_timer) {
        lv_timer_del(exit_timer);
        exit_timer = NULL;
    }

    if (!usb_stopped) {
        // Signal loop to exit
        should_stop = true;
        usb_mode_active = false;

        // Stop USB MSC (similar to Launcher's destructor)
        msc.end();

        // Re-enable screen sleep now that USB mode is exiting
        ::enableScreenSleep();

        // Resume GPS task now that USB mode is exiting
        TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
        if (gps_task_handle != NULL) {
            vTaskResume(gps_task_handle);
        }
        usb_stopped = true;
    }

    // Clean up UI
    if (status_label) {
        status_label = NULL;
    }
    if (loading_overlay) {
        lv_obj_del(loading_overlay);
        loading_overlay = NULL;
        loading_box = NULL;
    }
    if (root) {
        lv_obj_del(root);
        root = NULL;
    }
    content = NULL;
    top_bar = {};
}

/**
 * @brief Check if USB Mass Storage mode is currently active
 */
bool ui_usb_is_active()
{
    return usb_mode_active;
}

#endif // ARDUINO_USB_MODE
