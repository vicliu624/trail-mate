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
static lv_obj_t *menu = NULL;
static USBMSC msc;
static bool should_stop = false;
static bool usb_mode_active = false;  // Track if USB mode is active

// Forward declarations
static void back_event_handler(lv_event_t *e);
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
static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(menu, obj)) {
        // Stop USB MSC
        should_stop = true;
        usb_mode_active = false;
        msc.end();
        
        // Re-enable screen sleep now that USB mode is exiting
        ::enableScreenSleep();
        
        // Resume GPS task now that USB mode is exiting
        TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
        if (gps_task_handle != NULL) {
            vTaskResume(gps_task_handle);
        }
        
        // Clean up UI
        if (status_label) {
            status_label = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        
        menu_show();
    }
}

/**
 * @brief Enter USB Mass Storage mode
 */
void ui_usb_enter(lv_obj_t *parent)
{
    // Check if SD card is ready
    if (!board.isCardReady()) {
        // Show error message
        menu = create_menu(parent, back_event_handler);
        lv_obj_t *main_page = lv_menu_page_create(menu, "USB Mass Storage");
        
        lv_obj_t *error_label = lv_label_create(main_page);
        lv_label_set_text(error_label, "SD Card Not Found\nPlease insert SD card");
        lv_obj_center(error_label);
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF0000), LV_PART_MAIN);
        
        lv_menu_set_page(menu, main_page);
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
    
    // Create menu
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, "USB Mass Storage");
    
    // Create status label
    status_label = lv_label_create(main_page);
    lv_label_set_text(status_label, "Initializing...");
    lv_obj_center(status_label);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    
    // Create info label
    lv_obj_t *info_label = lv_label_create(main_page);
    lv_label_set_text(info_label, "Press Back to exit USB mode");
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_opa(info_label, LV_OPA_70, LV_PART_MAIN);
    
    lv_menu_set_page(menu, main_page);
    
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
    
    // Signal loop to exit
    should_stop = true;
    usb_mode_active = false;
    
    if (menu) {
        // Stop USB MSC (similar to Launcher's destructor)
        msc.end();
        
        // Re-enable screen sleep now that USB mode is exiting
        ::enableScreenSleep();
        
        // Resume GPS task now that USB mode is exiting
        TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
        if (gps_task_handle != NULL) {
            vTaskResume(gps_task_handle);
        }
        
        // Clean up UI
        if (status_label) {
            status_label = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
    }
}

/**
 * @brief Check if USB Mass Storage mode is currently active
 */
bool ui_usb_is_active()
{
    return usb_mode_active;
}

#endif // ARDUINO_USB_MODE
