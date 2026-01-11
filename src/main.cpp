#include "board/TLoRaPagerBoard.h"
#include "ui/LV_Helper.h"
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Preferences.h>

#include "display/DisplayConfig.h"
#include "ui/assets/images.h"
#include "ui/ui_common.h"

// Forward declarations for app entry functions (implemented in ui_*.cpp)
void ui_gps_enter(lv_obj_t *parent);
void ui_chat_enter(lv_obj_t *parent);
void ui_setting_enter(lv_obj_t *parent);

// GPS data structure for thread-safe access
struct GPSData {
    double lat;
    double lng;
    uint8_t satellites;
    bool valid;
    uint32_t age;  // Age of data in milliseconds
};

// Forward declaration for GPS data access
GPSData getGPSData();

// Forward declaration for user activity update (for screen sleep management)
void updateUserActivity();

// Forward declaration to check if screen is sleeping
bool isScreenSleeping();

// Forward declaration to get/set screen sleep timeout
uint32_t getScreenSleepTimeout();
void setScreenSleepTimeout(uint32_t timeout_ms);

// Forward declaration to get/set GPS collection interval
uint32_t getGPSCollectionInterval();
void setGPSCollectionInterval(uint32_t interval_ms);

// Factory-style menu structure (global for ui_*.cpp access)
lv_obj_t *main_screen = nullptr;
lv_obj_t *menu_panel = nullptr;
lv_group_t *menu_g = nullptr;
lv_group_t *app_g = nullptr;
lv_obj_t *desc_label = nullptr;

namespace {

// App function types (like factory example)
typedef void (*app_func_t)(lv_obj_t *parent);

typedef struct {
    app_func_t setup_func_cb;
    app_func_t exit_func_cb;
    void *user_data;
} app_t;

// App entry functions (implemented in ui_*.cpp files, global scope)

app_t ui_gps_main = {
    .setup_func_cb = ui_gps_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};

app_t ui_chat_main = {
    .setup_func_cb = ui_chat_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};

app_t ui_setting_main = {
    .setup_func_cb = ui_setting_enter,
    .exit_func_cb = nullptr,
    .user_data = nullptr,
};

const char *kAppNames[3] = {"GPS", "Chat", "Setting"};
const lv_image_dsc_t *kAppImages[3] = {&img_gps, &img_msgchat, &img_configuration};
app_t *kAppFuncs[3] = {&ui_gps_main, &ui_chat_main, &ui_setting_main};

#if LVGL_VERSION_MAJOR == 9
static uint32_t name_change_id;
#endif

// set_default_group and menu_show are implemented in ui_common.cpp

void menu_hidden()
{
    lv_tileview_set_tile_by_index(main_screen, 0, 1, LV_ANIM_ON);
}

static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    const char *text = (const char *)lv_event_get_user_data(e);
    if (c == LV_EVENT_FOCUSED) {
#if LVGL_VERSION_MAJOR == 9
        lv_obj_send_event(desc_label, (lv_event_code_t)name_change_id, (void *)text);
#else
        // For LVGL v8, would use lv_msg_send
#endif
    }
}

static void create_app(lv_obj_t *parent, const char *name, const lv_image_dsc_t *img, app_t *app_fun)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_coord_t w = 150;
    lv_coord_t h = LV_PCT(100);

    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_outline_color(btn, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN);
    uint32_t phy_hor_res = lv_display_get_physical_horizontal_resolution(NULL);
    if (phy_hor_res < 320) {
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_set_user_data(btn, (void *)name);

    if (img != NULL) {
        lv_obj_t *icon = lv_image_create(btn);
        lv_image_set_src(icon, img);
        lv_obj_center(icon);
    }
    /* Text change event callback */
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_FOCUSED, (void *)name);

    /* Click to select event callback */
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        app_t *func_cb = (app_t *)lv_event_get_user_data(e);
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        if (c == LV_EVENT_CLICKED) {
            set_default_group(app_g);
            if (func_cb->setup_func_cb) {
                (*func_cb->setup_func_cb)(parent);
            }
            menu_hidden();
        }
    },
    LV_EVENT_CLICKED, app_fun);
}

void menu_name_label_event_cb(lv_event_t *e)
{
#if LVGL_VERSION_MAJOR == 9
    const char *v = (const char *)lv_event_get_param(e);
    if (v) {
        lv_label_set_text(lv_event_get_target_obj(e), v);
    }
#else
    // For LVGL v8
#endif
}

// App entry functions are implemented in ui_gps.cpp, ui_chat.cpp, ui_setting.cpp

} // namespace

// GPS data storage and synchronization (outside namespace for global access)
static GPSData gps_data = {0.0, 0.0, 0, false, 0};
static SemaphoreHandle_t gps_data_mutex = NULL;
static TaskHandle_t gps_task_handle = NULL;
static uint32_t gps_last_update_time = 0;  // Timestamp of last valid GPS update
static uint32_t gps_collection_interval_ms = 1000;  // Default 1 second (1Hz), can be configured

// Screen sleep management
static uint32_t last_user_activity_time = 0;  // Timestamp of last user activity
static SemaphoreHandle_t activity_mutex = NULL;
static TaskHandle_t screen_sleep_task_handle = NULL;
static bool screen_sleeping = false;
static uint8_t saved_keyboard_brightness = 127;  // Save keyboard brightness before sleep (default 127)
static uint32_t screen_sleep_timeout_ms = 30000;  // Default 30 seconds, can be configured
static Preferences preferences;  // For saving/loading settings

/**
 * GPS data collection task (configurable frequency)
 * This task continuously collects GPS data from the sensor and stores it in memory
 * This avoids race conditions by having a single task access the GPS hardware
 * Collection frequency is configurable (minimum 60 seconds = 1 minute)
 */
static void gpsDataCollectionTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t loop_count = 0;
    uint32_t task_start_ms = millis();
    uint32_t last_log_ms = 0;
    
    Serial.printf("[GPS Task] ===== TASK STARTED =====\n");
    Serial.printf("[GPS Task] Started at %lu ms, GPS ready: %d\n", task_start_ms, instance.isGPSReady());
    Serial.printf("[GPS Task] Collection interval: %lu ms\n", getGPSCollectionInterval());
    
    while (true) {
        loop_count++;
        uint32_t now_ms = millis();
        bool gps_ready = instance.isGPSReady();
        
        // Log every loop for first 10 loops, then every 10 loops, or every 5 seconds
        bool should_log = (loop_count <= 10) || 
                         (loop_count % 10 == 0) || 
                         ((now_ms - last_log_ms) >= 5000);
        
        if (should_log) {
            Serial.printf("[GPS Task] Loop %lu (elapsed: %lu ms): GPS ready=%d, valid=%d, mutex=%p\n", 
                         loop_count, now_ms - task_start_ms, gps_ready, gps_data.valid, gps_data_mutex);
            last_log_ms = now_ms;
        }
        
        if (gps_ready) {
            // Process GPS data from serial stream
            // This is the only place that directly accesses the GPS hardware
            uint32_t chars_processed = instance.gps.loop();
            
            if (should_log && chars_processed > 0) {
                Serial.printf("[GPS Task] GPS loop processed %lu characters\n", chars_processed);
            }
            
            // Update GPS data structure with mutex protection
            if (gps_data_mutex != NULL && xSemaphoreTake(gps_data_mutex, portMAX_DELAY) == pdTRUE) {
                bool was_valid = gps_data.valid;
                bool has_fix = instance.gps.location.isValid();
                uint8_t sat_count = instance.gps.satellites.value();
                
                if (has_fix) {
                    gps_data.lat = instance.gps.location.lat();
                    gps_data.lng = instance.gps.location.lng();
                    gps_data.satellites = sat_count;
                    gps_data.valid = true;
                    gps_last_update_time = millis();
                    gps_data.age = 0;  // Fresh data
                    
                    // Always log when fix is acquired or every 10 loops
                    if (!was_valid || should_log) {
                        Serial.printf("[GPS Task] *** FIX ACQUIRED *** lat=%.6f, lng=%.6f, sat=%d (loop %lu)\n", 
                                     gps_data.lat, gps_data.lng, gps_data.satellites, loop_count);
                    }
                } else {
                    gps_data.valid = false;
                    if (was_valid) {
                        Serial.printf("[GPS Task] *** FIX LOST *** (loop %lu)\n", loop_count);
                    }
                    // Log periodically when GPS is ready but no fix yet
                    if (should_log) {
                        Serial.printf("[GPS Task] GPS ready but no fix yet (loop %lu, sat=%d, chars=%lu)\n", 
                                     loop_count, sat_count, chars_processed);
                    }
                    // Age is calculated when reading data
                }
                xSemaphoreGive(gps_data_mutex);
            } else {
                Serial.printf("[GPS Task] ERROR: Failed to take mutex (loop %lu)\n", loop_count);
            }
        } else {
            // Log more frequently when GPS is not ready
            if (should_log) {
                Serial.printf("[GPS Task] GPS not ready (loop %lu, elapsed: %lu ms)\n", 
                             loop_count, now_ms - task_start_ms);
            }
        }
        
        // Get current collection interval from persistent storage
        uint32_t interval_ms = getGPSCollectionInterval();
        TickType_t frequency = pdMS_TO_TICKS(interval_ms);
        
        if (should_log) {
            Serial.printf("[GPS Task] Waiting %lu ms until next cycle...\n", interval_ms);
        }
        
        // Wait for next cycle (configurable interval)
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

/**
 * Get current GPS data (thread-safe)
 * This function is called from ui_gps.cpp to read GPS data from memory
 */
GPSData getGPSData()
{
    GPSData data = {0.0, 0.0, 0, false, 0};
    if (gps_data_mutex != NULL) {
        if (xSemaphoreTake(gps_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            data = gps_data;
            // Calculate age of data
            if (data.valid && gps_last_update_time > 0) {
                data.age = millis() - gps_last_update_time;
            } else {
                data.age = UINT32_MAX;  // Invalid or very old data
            }
            xSemaphoreGive(gps_data_mutex);
        }
    }
    return data;
}

/**
 * Check if screen is currently sleeping
 * @return true if screen is sleeping, false otherwise
 */
bool isScreenSleeping()
{
    bool sleeping = false;
    if (activity_mutex != NULL) {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            sleeping = screen_sleeping;
            xSemaphoreGive(activity_mutex);
        }
    }
    return sleeping;
}

/**
 * Get current screen sleep timeout
 * This function reads from persistent storage to ensure we always have the latest value
 * @return Screen sleep timeout in milliseconds
 */
uint32_t getScreenSleepTimeout()
{
    uint32_t timeout = 30000;  // Default 30 seconds
    
    // Read from persistent storage to ensure we have the latest value
    preferences.begin("settings", true);  // Read-only mode
    timeout = preferences.getUInt("sleep_timeout", 30000);  // Default 30 seconds
    preferences.end();
    
    // Ensure minimum timeout
    if (timeout < 10000) {
        timeout = 30000;
    }
    
    // Also update memory variable for faster access
    if (activity_mutex != NULL) {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            screen_sleep_timeout_ms = timeout;
            xSemaphoreGive(activity_mutex);
        }
    }
    
    return timeout;
}

/**
 * Set screen sleep timeout
 * @param timeout_ms Screen sleep timeout in milliseconds (minimum 10 seconds)
 */
void setScreenSleepTimeout(uint32_t timeout_ms)
{
    // Minimum 10 seconds, maximum 300 seconds (5 minutes)
    if (timeout_ms < 10000) timeout_ms = 10000;
    if (timeout_ms > 300000) timeout_ms = 300000;
    
    if (activity_mutex != NULL) {
        if (xSemaphoreTake(activity_mutex, portMAX_DELAY) == pdTRUE) {
            screen_sleep_timeout_ms = timeout_ms;
            // Save to preferences
            preferences.begin("settings", false);
            preferences.putUInt("sleep_timeout", timeout_ms);
            preferences.end();
            xSemaphoreGive(activity_mutex);
        }
    }
}

/**
 * Get current GPS collection interval
 * This function reads from persistent storage to ensure we always have the latest value
 * @return GPS collection interval in milliseconds
 */
uint32_t getGPSCollectionInterval()
{
    uint32_t interval = 1000;  // Default 1 second (1Hz)
    
    // Read from persistent storage to ensure we have the latest value
    preferences.begin("settings", true);  // Read-only mode
    interval = preferences.getUInt("gps_interval", 1000);  // Default 1 second
    preferences.end();
    
    // Ensure minimum interval (60 seconds = 1 minute)
    if (interval < 60000) {
        interval = 60000;
    }
    
    // Also update memory variable for faster access
    if (gps_data_mutex != NULL) {
        if (xSemaphoreTake(gps_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gps_collection_interval_ms = interval;
            xSemaphoreGive(gps_data_mutex);
        }
    }
    
    return interval;
}

/**
 * Set GPS collection interval
 * @param interval_ms GPS collection interval in milliseconds (minimum 60 seconds = 1 minute)
 */
void setGPSCollectionInterval(uint32_t interval_ms)
{
    // Minimum 60 seconds (1 minute), maximum 600 seconds (10 minutes)
    if (interval_ms < 60000) interval_ms = 60000;
    if (interval_ms > 600000) interval_ms = 600000;
    
    if (gps_data_mutex != NULL) {
        if (xSemaphoreTake(gps_data_mutex, portMAX_DELAY) == pdTRUE) {
            gps_collection_interval_ms = interval_ms;
            // Save to preferences
            preferences.begin("settings", false);
            preferences.putUInt("gps_interval", interval_ms);
            preferences.end();
            xSemaphoreGive(gps_data_mutex);
        }
    }
}

/**
 * Update user activity timestamp (call this when user interacts with device)
 * This function is thread-safe and can be called from any context
 */
void updateUserActivity()
{
    if (activity_mutex != NULL) {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            last_user_activity_time = millis();
            // If screen is sleeping, wake it up
            if (screen_sleeping) {
                screen_sleeping = false;
                instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                // Restore keyboard brightness
                if (instance.hasKeyboard()) {
                    instance.kb.setBrightness(saved_keyboard_brightness);
                }
            }
            xSemaphoreGive(activity_mutex);
        }
    }
}

/**
 * Screen sleep management task
 * Monitors user activity and puts screen to sleep after 30 seconds of inactivity
 * Wakes up screen when user input is detected
 */
static void screenSleepTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t check_interval = pdMS_TO_TICKS(1000);  // Check every 1 second
    
    while (true) {
        // User activity detection is now handled by LVGL input device callbacks:
        // - lv_encoder_read() calls updateUserActivity() for rotary encoder
        // - keypad_read() calls updateUserActivity() for keyboard
        // No need to poll here, as it would consume input events before LVGL can process them
        
        // Check if screen should sleep or wake
        if (activity_mutex != NULL) {
            if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                uint32_t current_time = millis();
                uint32_t time_since_activity = current_time - last_user_activity_time;
                
                // Get current timeout from persistent storage (may have been changed in settings)
                // Read directly from preferences to ensure we have the latest value
                preferences.begin("settings", true);  // Read-only mode
                uint32_t current_timeout = preferences.getUInt("sleep_timeout", 30000);
                preferences.end();
                
                // Ensure minimum timeout
                if (current_timeout < 10000) {
                    current_timeout = 30000;
                }
                
                // Update memory variable
                screen_sleep_timeout_ms = current_timeout;
                
                if (!screen_sleeping && time_since_activity >= current_timeout) {
                    // Put screen to sleep
                    screen_sleeping = true;
                    // Save current keyboard brightness before turning off
                    if (instance.hasKeyboard()) {
                        saved_keyboard_brightness = instance.kb.getBrightness();
                        instance.kb.setBrightness(0);  // Turn off keyboard backlight
                    }
                    instance.setBrightness(0);  // Turn off display backlight
                } else if (screen_sleeping && time_since_activity < current_timeout) {
                    // Wake up screen (shouldn't happen here, but just in case)
                    screen_sleeping = false;
                    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                    // Restore keyboard brightness
                    if (instance.hasKeyboard()) {
                        instance.kb.setBrightness(saved_keyboard_brightness);
                    }
                }
                
                xSemaphoreGive(activity_mutex);
            }
        }
        
        // Wait for next check
        vTaskDelayUntil(&last_wake_time, check_interval);
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();
    beginLvglHelper(instance);

    // Set screen background (like factory example)
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);

    // Create groups (like factory example)
    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    static lv_style_t style_frameless;
    lv_style_init(&style_frameless);
    lv_style_set_radius(&style_frameless, 0);
    lv_style_set_border_width(&style_frameless, 0);
    lv_style_set_bg_color(&style_frameless, lv_color_white());
    lv_style_set_shadow_width(&style_frameless, 55);
    lv_style_set_shadow_color(&style_frameless, lv_color_black());

    /* Create tileview (like factory example) */
    main_screen = lv_tileview_create(lv_screen_active());
    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));

    /* Create two views for switching menus and app UI */
    menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Initialize the menu view */
    lv_obj_t *panel = lv_obj_create(menu_panel);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(70));
    lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(panel, &style_frameless, 0);

    /* Add applications (only GPS, Chat, Setting) */
    for (int i = 0; i < 3; ++i) {
        create_app(panel, kAppNames[i], kAppImages[i], kAppFuncs[i]);
        lv_group_add_obj(menu_g, lv_obj_get_child(panel, i));
    }

    int offset = -10;
    if (lv_display_get_physical_vertical_resolution(NULL) > 320) {
        offset = -45;
    }
    /* Initialize the label */
    desc_label = lv_label_create(menu_panel);
    lv_obj_set_width(desc_label, LV_PCT(100));
    lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, offset);
    lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_CENTER, 0);

    if (lv_display_get_physical_horizontal_resolution(NULL) < 320) {
        lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_16, 0);
        lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, -25);
    } else {
        lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_20, 0);
    }
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

#if LVGL_VERSION_MAJOR == 9
    name_change_id = lv_event_register_id();
    lv_obj_add_event_cb(desc_label, menu_name_label_event_cb, (lv_event_code_t)name_change_id, NULL);
    lv_obj_send_event(lv_obj_get_child(panel, 0), LV_EVENT_FOCUSED, NULL);
#else
    // For LVGL v8
#endif

    lv_obj_update_snap(panel, LV_ANIM_ON);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    
    // Create GPS data mutex
    gps_data_mutex = xSemaphoreCreateMutex();
    if (gps_data_mutex == NULL) {
        log_e("Failed to create GPS data mutex");
    }
    
    // Load GPS collection interval from preferences
    preferences.begin("settings", true);
    gps_collection_interval_ms = preferences.getUInt("gps_interval", 1000);  // Default 1 second
    preferences.end();
    
    // Ensure minimum interval (60 seconds = 1 minute)
    if (gps_collection_interval_ms < 60000) {
        gps_collection_interval_ms = 60000;
    }
    
    // Create GPS data collection task (configurable frequency)
    // Always create the task, even if GPS is not ready yet (it will wait and retry)
    Serial.printf("[Setup] GPS ready check: %d\n", instance.isGPSReady());
    BaseType_t task_result = xTaskCreate(
        gpsDataCollectionTask,
        "gps_collect",
        4 * 1024,  // Stack size
        NULL,
        5,  // Priority (lower than rotary task)
        &gps_task_handle
    );
    if (task_result != pdPASS) {
        Serial.printf("[Setup] ERROR: Failed to create GPS data collection task\n");
        log_e("Failed to create GPS data collection task");
    } else {
        Serial.printf("[Setup] GPS data collection task created successfully (interval: %lu ms, GPS ready: %d)\n", 
                     gps_collection_interval_ms, instance.isGPSReady());
        log_d("GPS data collection task created successfully (interval: %lu ms)", gps_collection_interval_ms);
    }
    
    // Create activity mutex for screen sleep management
    activity_mutex = xSemaphoreCreateMutex();
    if (activity_mutex == NULL) {
        log_e("Failed to create activity mutex");
    } else {
        // Initialize activity time
        last_user_activity_time = millis();
        
        // Load screen sleep timeout from preferences
        preferences.begin("settings", true);
        screen_sleep_timeout_ms = preferences.getUInt("sleep_timeout", 30000);  // Default 30 seconds
        preferences.end();
        
        // Ensure minimum timeout
        if (screen_sleep_timeout_ms < 10000) {
            screen_sleep_timeout_ms = 30000;
        }
    }
    
    // Create screen sleep management task
    BaseType_t sleep_task_result = xTaskCreate(
        screenSleepTask,
        "screen_sleep",
        2 * 1024,  // Stack size
        NULL,
        3,  // Priority (lower than GPS task)
        &screen_sleep_task_handle
    );
    if (sleep_task_result != pdPASS) {
        log_e("Failed to create screen sleep task");
    } else {
        log_d("Screen sleep management task created successfully");
    }
}

void loop()
{
    static uint32_t last_loop_ms = 0;
    lv_timer_handler();
    
    delay(2);
}
