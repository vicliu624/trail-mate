#if defined(ARDUINO_T_DECK)
#include "board/TDeckBoard.h"
#else
#include "board/TLoRaPagerBoard.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ui/LV_Helper.h"
#include <Preferences.h>
#include <cmath>
#include <ctime>
#include <esp_sleep.h>

#include "app/app_context.h"
#include "display/DisplayConfig.h"
#include "ui/app_screen.h"
#include "ui/assets/images.h"
#include "ui/ui_common.h"
#include "ui/widgets/system_notification.h"

// Custom app icons generated as C images (RGB565A8)
extern "C"
{
    extern const lv_image_dsc_t gps_icon;
    extern const lv_image_dsc_t Chat;
    extern const lv_image_dsc_t Setting;
    extern const lv_image_dsc_t contact;
    extern const lv_image_dsc_t team_icon;
    extern const lv_image_dsc_t tracker_icon;
    extern const lv_image_dsc_t shutdown;
    // Note: img_usb is already declared in images.h (C++ linkage)
}

// Debug switch for performance timing logs
// Set to 1 to enable [MAIN] timing logs, 0 to disable
#define MAIN_TIMING_DEBUG 0

// Forward declarations for app entry functions (implemented in ui_*.cpp)
void ui_gps_enter(lv_obj_t* parent);
void ui_gps_exit(lv_obj_t* parent);
void ui_chat_enter(lv_obj_t* parent);
void ui_chat_exit(lv_obj_t* parent);
void ui_contacts_enter(lv_obj_t* parent);
void ui_contacts_exit(lv_obj_t* parent);
void ui_team_enter(lv_obj_t* parent);
void ui_team_exit(lv_obj_t* parent);
void ui_tracker_enter(lv_obj_t* parent);
void ui_tracker_exit(lv_obj_t* parent);
void ui_setting_enter(lv_obj_t* parent);
void ui_setting_exit(lv_obj_t* parent);
#ifdef ARDUINO_USB_MODE
void ui_usb_enter(lv_obj_t* parent);
void ui_usb_exit(lv_obj_t* parent);
#endif

// GPS data access - now provided by GpsService
// Use gps::GpsState and gps::GpsService::getInstance().getData()

// Forward declaration for user activity update (for screen sleep management)
void updateUserActivity();

// Forward declaration to check if screen is sleeping
bool isScreenSleeping();

// Forward declaration to get/set screen sleep timeout
uint32_t getScreenSleepTimeout();
void setScreenSleepTimeout(uint32_t timeout_ms);

// Forward declaration to disable/enable screen sleep (for USB mode, etc.)
void disableScreenSleep();
void enableScreenSleep();
bool isScreenSleepDisabled();

// GPS data access - now provided by GpsService
// Use gps::GpsService::getInstance().getData()
// Use gps::GpsService::getInstance().getCollectionInterval()/setCollectionInterval()

// Factory-style menu structure (global for ui_*.cpp access)
lv_obj_t* main_screen = nullptr;
lv_obj_t* menu_panel = nullptr;
lv_obj_t* app_panel = nullptr;
lv_group_t* menu_g = nullptr;
lv_group_t* app_g = nullptr;
lv_obj_t* desc_label = nullptr;
lv_obj_t* time_label = nullptr;    // Time display label at top left of menu
lv_obj_t* battery_label = nullptr; // Battery display label at top right of menu

namespace
{
bool format_menu_time(char* out, size_t out_len)
{
    if (!out || out_len < 6)
    {
        return false;
    }
    time_t now = time(nullptr);
    if (now <= 0)
    {
        return false;
    }
    time_t local = ui_apply_timezone_offset(now);
    struct tm* info = gmtime(&local);
    if (!info)
    {
        return false;
    }
    strftime(out, out_len, "%H:%M", info);
    return true;
}

// App function types (like factory example)
class FunctionAppScreen : public AppScreen
{
  public:
    FunctionAppScreen(const char* name,
                      const lv_image_dsc_t* icon,
                      void (*enter)(lv_obj_t*),
                      void (*exit)(lv_obj_t*))
        : name_(name), icon_(icon), enter_(enter), exit_(exit) {}

    const char* name() const override { return name_; }
    const lv_image_dsc_t* icon() const override { return icon_; }

    void enter(lv_obj_t* parent) override
    {
        if (enter_)
        {
            enter_(parent);
        }
    }

    void exit(lv_obj_t* parent) override
    {
        if (exit_)
        {
            exit_(parent);
        }
    }

  private:
    const char* name_;
    const lv_image_dsc_t* icon_;
    void (*enter_)(lv_obj_t*);
    void (*exit_)(lv_obj_t*);
};

// Shutdown app - directly triggers system shutdown
static void ui_shutdown_enter(lv_obj_t* parent)
{
    (void)parent;
    // Directly trigger software shutdown without confirmation dialog
    // The main menu access already implies user intent
    board.softwareShutdown();
}

static FunctionAppScreen s_gps_app("GPS", &gps_icon, ui_gps_enter, ui_gps_exit);
static FunctionAppScreen s_tracker_app("Tracker", &tracker_icon, ui_tracker_enter, ui_tracker_exit);
static FunctionAppScreen s_chat_app("Chat", &Chat, ui_chat_enter, ui_chat_exit);
static FunctionAppScreen s_contacts_app("Contacts", &contact, ui_contacts_enter, ui_contacts_exit);
static FunctionAppScreen s_team_app("Team", &team_icon, ui_team_enter, ui_team_exit);
static FunctionAppScreen s_setting_app("Setting", &Setting, ui_setting_enter, ui_setting_exit);
static FunctionAppScreen s_shutdown_app("Shutdown", &shutdown, ui_shutdown_enter, nullptr);

#ifdef ARDUINO_USB_MODE
static FunctionAppScreen s_usb_app("USB Mass Storage", &img_usb, ui_usb_enter, ui_usb_exit);
static AppScreen* kAppScreens[] = {&s_gps_app, &s_tracker_app, &s_chat_app, &s_contacts_app,
                                   &s_team_app, &s_usb_app, &s_setting_app, &s_shutdown_app};
#define NUM_APPS 8
#else
static AppScreen* kAppScreens[] = {&s_gps_app, &s_tracker_app, &s_chat_app, &s_contacts_app,
                                   &s_team_app, &s_setting_app, &s_shutdown_app};
#define NUM_APPS 7
#endif

#if LVGL_VERSION_MAJOR == 9
static uint32_t name_change_id;
#endif

// set_default_group and menu_show are implemented in ui_common.cpp

void menu_hidden()
{
    lv_tileview_set_tile_by_index(main_screen, 0, 1, LV_ANIM_ON);
}

static void btn_event_cb(lv_event_t* e)
{
    lv_event_code_t c = lv_event_get_code(e);
    const char* text = (const char*)lv_event_get_user_data(e);
    if (c == LV_EVENT_FOCUSED)
    {
#if LVGL_VERSION_MAJOR == 9
        lv_obj_send_event(desc_label, (lv_event_code_t)name_change_id, (void*)text);
#else
        // For LVGL v8, would use lv_msg_send
#endif
    }
}

static void create_app(lv_obj_t* parent, AppScreen* app)
{
    const char* name = app ? app->name() : "";
    const lv_image_dsc_t* img = app ? app->icon() : nullptr;

    lv_obj_t* btn = lv_btn_create(parent);
    lv_coord_t w = 150;
    lv_coord_t h = LV_PCT(100);

    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_outline_color(btn, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN);
    uint32_t phy_hor_res = lv_display_get_physical_horizontal_resolution(NULL);
    if (phy_hor_res < 320)
    {
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_set_user_data(btn, (void*)name);

    if (img != NULL)
    {
        lv_obj_t* icon = lv_image_create(btn);
        lv_image_set_src(icon, img);
        lv_obj_center(icon);
    }
    /* Text change event callback */
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_FOCUSED, (void*)name);

    /* Click to select event callback */
    lv_obj_add_event_cb(
        btn, [](lv_event_t* e)
        {
        lv_event_code_t c = lv_event_get_code(e);
        auto* target_app = static_cast<AppScreen*>(lv_event_get_user_data(e));
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        if (c == LV_EVENT_CLICKED) {
            set_default_group(app_g);
            ui_switch_to_app(target_app, parent);
            menu_hidden();
        } },
        LV_EVENT_CLICKED, app);
}

void menu_name_label_event_cb(lv_event_t* e)
{
#if LVGL_VERSION_MAJOR == 9
    const char* v = (const char*)lv_event_get_param(e);
    if (v)
    {
        lv_label_set_text(lv_event_get_target_obj(e), v);
    }
#else
    // For LVGL v8
#endif
}

// App entry functions are implemented in ui_gps.cpp, ui_chat.cpp, ui_setting.cpp

} // namespace
// GPS data access - now provided by TLoRaPagerBoard
// GPS data collection task is now in TLoRaPagerBoard class

// Screen sleep management
static uint32_t last_user_activity_time = 0; // Timestamp of last user activity
static SemaphoreHandle_t activity_mutex = NULL;
static TaskHandle_t screen_sleep_task_handle = NULL;
static bool screen_sleeping = false;
static bool screen_sleep_disabled = false;       // Flag to disable screen sleep (e.g., during USB mode)
static uint8_t saved_keyboard_brightness = 127;  // Save keyboard brightness before sleep (default 127)
static uint32_t screen_sleep_timeout_ms = 30000; // Default 30 seconds, can be configured
static Preferences preferences;                  // For saving/loading settings

// Power management wrapper function for accessing preferences
Preferences& getPreferencesInstance() { return preferences; }

// Access function for screen sleep task handle (used by power management)
TaskHandle_t getScreenSleepTaskHandle() { return screen_sleep_task_handle; }

// GPS data collection task is now in GpsService
// Use gps::GpsService::getInstance().getData() to access GPS data

/**
 * Check if screen is currently sleeping
 * @return true if screen is sleeping, false otherwise
 */
bool isScreenSleeping()
{
    bool sleeping = false;
    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
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
    uint32_t timeout = 30000; // Default 30 seconds

    // Read from persistent storage to ensure we have the latest value
    preferences.begin("settings", true);                   // Read-only mode
    timeout = preferences.getUInt("sleep_timeout", 30000); // Default 30 seconds
    preferences.end();

    // Ensure minimum timeout
    if (timeout < 10000)
    {
        timeout = 30000;
    }

    // Also update memory variable for faster access
    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
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

    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            screen_sleep_timeout_ms = timeout_ms;
            // Save to preferences
            preferences.begin("settings", false);
            preferences.putUInt("sleep_timeout", timeout_ms);
            preferences.end();
            xSemaphoreGive(activity_mutex);
        }
    }
}

// GPS collection interval functions are now in GpsService
// Use gps::GpsService::getInstance().getCollectionInterval()/setCollectionInterval()

/**
 * Disable screen sleep (e.g., during USB Mass Storage mode)
 * This prevents the screen from sleeping and keeps USB functionality active
 */
void disableScreenSleep()
{
    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            screen_sleep_disabled = true;
            // Wake up screen if it's sleeping
            if (screen_sleeping)
            {
                screen_sleeping = false;
                board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                // Restore keyboard brightness
                if (board.hasKeyboard())
                {
                    board.keyboardSetBrightness(saved_keyboard_brightness);
                }
            }
            xSemaphoreGive(activity_mutex);
        }
    }
}

/**
 * Enable screen sleep (restore normal behavior)
 */
void enableScreenSleep()
{
    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            screen_sleep_disabled = false;
            // Reset activity time to current time to prevent immediate sleep
            last_user_activity_time = millis();
            xSemaphoreGive(activity_mutex);
        }
    }
}

/**
 * Check if screen sleep is currently disabled
 * @return true if screen sleep is disabled, false otherwise
 */
bool isScreenSleepDisabled()
{
    bool disabled = false;
    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            disabled = screen_sleep_disabled;
            xSemaphoreGive(activity_mutex);
        }
    }
    return disabled;
}

/**
 * Update user activity timestamp (call this when user interacts with device)
 * This function is thread-safe and can be called from any context
 */
void updateUserActivity()
{
    if (activity_mutex != NULL)
    {
        if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            last_user_activity_time = millis();
            // If screen is sleeping, wake it up
            if (screen_sleeping)
            {
                screen_sleeping = false;
                board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                // Restore keyboard brightness
                if (board.hasKeyboard())
                {
                    board.keyboardSetBrightness(saved_keyboard_brightness);
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
static void screenSleepTask(void* pvParameters)
{
    (void)pvParameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t check_interval = pdMS_TO_TICKS(1000); // Check every 1 second

    while (true)
    {
        // User activity detection is now handled by LVGL input device callbacks:
        // - lv_encoder_read() calls updateUserActivity() for rotary encoder
        // - keypad_read() calls updateUserActivity() for keyboard
        // No need to poll here, as it would consume input events before LVGL can process them

        // Check if screen should sleep or wake
        if (activity_mutex != NULL)
        {
            if (xSemaphoreTake(activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                uint32_t current_time = millis();
                uint32_t time_since_activity = current_time - last_user_activity_time;

                // Get current timeout from persistent storage (may have been changed in settings)
                // Read directly from preferences to ensure we have the latest value
                preferences.begin("settings", true); // Read-only mode
                uint32_t current_timeout = preferences.getUInt("sleep_timeout", 30000);
                preferences.end();

                // Ensure minimum timeout
                if (current_timeout < 10000)
                {
                    current_timeout = 30000;
                }

                // Update memory variable
                screen_sleep_timeout_ms = current_timeout;

                // Skip sleep if screen sleep is disabled (e.g., during USB mode)
                if (screen_sleep_disabled)
                {
                    // If screen is sleeping but sleep is now disabled, wake it up
                    if (screen_sleeping)
                    {
                        screen_sleeping = false;
                        board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                        // Restore keyboard brightness
                        if (board.hasKeyboard())
                        {
                            board.keyboardSetBrightness(saved_keyboard_brightness);
                        }
                    }
                    xSemaphoreGive(activity_mutex);
                }
                else
                {
                    // Normal sleep logic
                    if (!screen_sleeping && time_since_activity >= current_timeout)
                    {
                        // Put screen to sleep
                        screen_sleeping = true;
                        // Save current keyboard brightness before turning off
                        if (board.hasKeyboard())
                        {
                            saved_keyboard_brightness = board.keyboardGetBrightness();
                            board.keyboardSetBrightness(0); // Turn off keyboard backlight
                        }
                        board.setBrightness(0); // Turn off display backlight
                    }
                    else if (screen_sleeping && time_since_activity < current_timeout)
                    {
                        // Wake up screen (shouldn't happen here, but just in case)
                        screen_sleeping = false;
                        board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                        // Restore keyboard brightness
                        if (board.hasKeyboard())
                        {
                            board.keyboardSetBrightness(saved_keyboard_brightness);
                        }
                    }

                    xSemaphoreGive(activity_mutex);
                }
            }
        }

        // Wait for next check
        vTaskDelayUntil(&last_wake_time, check_interval);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(100); // Give Serial time to stabilize before printing logs
    Serial.printf("\n\n[Setup] ===== SYSTEM STARTUP =====\n");
    Serial.printf("[Setup] Serial initialized at 115200 baud\n");

    // Check if waking up from deep sleep
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool waking_from_sleep = (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (waking_from_sleep)
    {
        Serial.printf("[Setup] Wakeup cause: %d\n", wakeup_reason);
    }

    board.begin();

    // If waking from deep sleep, perform wake up initialization
    if (waking_from_sleep)
    {
        board.wakeUp();
    }
    beginLvglHelper(static_cast<LilyGo_Display&>(instance));

    // Initialize system notification component
    ui::SystemNotification::init();

    // Initialize chat application context
    app::AppContext& app_ctx = app::AppContext::getInstance();
    bool use_mock = false; // Enable real LoRa adapter for logging and radio tests
    if (app_ctx.init(board, &instance, &instance, &instance, use_mock))
    {
        Serial.printf("[Setup] Chat application context initialized\n");
    }
    else
    {
        Serial.printf("[Setup] WARNING: Failed to initialize chat context\n");
    }

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
    app_panel = lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);
    if (app_panel)
    {
        lv_obj_set_style_bg_color(app_panel, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(app_panel, LV_OPA_COVER, 0);
    }

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Create time label at top left of menu */
    time_label = lv_label_create(menu_panel);
    lv_obj_set_width(time_label, LV_SIZE_CONTENT);
    lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 5, 0); // Top left, 5px from left edge
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(time_label, lv_color_black(), 0); // Black text for better contrast
    // Add light background to make black text visible
    lv_obj_set_style_bg_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(time_label, LV_OPA_80, 0); // Semi-transparent white background
    lv_obj_set_style_pad_all(time_label, 4, 0);
    // Make sure time label is on top
    lv_obj_move_foreground(time_label);
    if (lv_display_get_physical_horizontal_resolution(NULL) < 320)
    {
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
    }
    else
    {
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_18, 0);
    }
    lv_label_set_text(time_label, "--:--");

    /* Create battery label at top right of menu */
    battery_label = lv_label_create(menu_panel);
    lv_obj_set_width(battery_label, LV_SIZE_CONTENT);
    lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -5, 0); // Top right, 5px from right edge
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_black(), 0);
    // Add light background to make black text visible
    lv_obj_set_style_bg_color(battery_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(battery_label, LV_OPA_80, 0); // Semi-transparent white background
    lv_obj_set_style_pad_all(battery_label, 4, 0);
    // Make sure battery label is on top
    lv_obj_move_foreground(battery_label);
    if (lv_display_get_physical_horizontal_resolution(NULL) < 320)
    {
        lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_14, 0);
    }
    else
    {
        lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_18, 0);
    }
    lv_label_set_text(battery_label, "?%");

    /* Initialize the menu view - moved down to make room for time */
    lv_obj_t* panel = lv_obj_create(menu_panel);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(70));
    lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    // Move panel down to make room for time label (adjust offset based on screen size)
    int panel_offset = 30; // Offset for time label space
    if (lv_display_get_physical_vertical_resolution(NULL) > 320)
    {
        panel_offset = 35; // Slightly more space on larger screens
    }
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, panel_offset);
    lv_obj_add_style(panel, &style_frameless, 0);

    /* Add applications */
    for (int i = 0; i < NUM_APPS; ++i)
    {
        create_app(panel, kAppScreens[i]);
        lv_group_add_obj(menu_g, lv_obj_get_child(panel, i));
    }

    int offset = -10;
    if (lv_display_get_physical_vertical_resolution(NULL) > 320)
    {
        offset = -45;
    }
    /* Initialize the label */
    desc_label = lv_label_create(menu_panel);
    lv_obj_set_width(desc_label, LV_PCT(100));
    lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, offset);
    lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_CENTER, 0);

    if (lv_display_get_physical_horizontal_resolution(NULL) < 320)
    {
        lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_16, 0);
        lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, -25);
    }
    else
    {
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

    // Create timer to update time display (minimum resource usage)
    // Update every 60 seconds, display format: HH:MM (no seconds)
    // This minimizes I2C communication and UI updates
    const uint32_t time_update_interval_ms = 60000; // 60 seconds = minimum update frequency

    lv_timer_t* time_timer = lv_timer_create([](lv_timer_t* timer)
                                             {
        if (time_label == nullptr) {
            return;
        }
        
        // Check if RTC is ready
        if (!board.isRTCReady()) {
            lv_label_set_text(time_label, "--:--");
            return;
        }
        
        // Get current time with timezone offset (HH:MM format, no seconds)
        char time_str[16];
        if (format_menu_time(time_str, sizeof(time_str))) {
            // Only update if text actually changed (saves UI redraw)
            static char last_time_str[16] = "";
            if (strcmp(time_str, last_time_str) != 0) {
                lv_label_set_text(time_label, time_str);
                strncpy(last_time_str, time_str, sizeof(last_time_str) - 1);
                last_time_str[sizeof(last_time_str) - 1] = '\0';
            }
        } else {
            // If RTC read fails, show error indicator
            lv_label_set_text(time_label, "??:??");
        } },
                                             time_update_interval_ms, NULL);
    lv_timer_set_repeat_count(time_timer, -1); // Repeat indefinitely

    // Create timer to update battery display (minimum resource usage)
    // Update every 60 seconds (1 minute) - battery changes slowly, minimize I2C communication
    const uint32_t battery_update_interval_ms = 60000; // 60 seconds = minimum update frequency

    lv_timer_t* battery_timer = lv_timer_create([](lv_timer_t* timer)
                                                {
        if (battery_label == nullptr) {
            return;
        }
        
        char battery_str[32];
        bool charging = board.isCharging();
        int level = board.getBatteryLevel();
        
        if (level < 0) {
            // Battery gauge not available
            lv_label_set_text(battery_label, "?%");
            return;
        }
        
        // Format battery display with shared helper
        ui_format_battery(level, charging, battery_str, sizeof(battery_str));
        
        // Only update if text actually changed (saves UI redraw)
        static char last_battery_str[32] = "";
        if (strcmp(battery_str, last_battery_str) != 0) {
            lv_label_set_text(battery_label, battery_str);
            strncpy(last_battery_str, battery_str, sizeof(last_battery_str) - 1);
            last_battery_str[sizeof(last_battery_str) - 1] = '\0';
        } },
                                                battery_update_interval_ms, NULL);
    lv_timer_set_repeat_count(battery_timer, -1); // Repeat indefinitely

    // Update time immediately (don't wait for first timer tick)
    if (board.isRTCReady())
    {
        char time_str[16];
        if (format_menu_time(time_str, sizeof(time_str)))
        {
            lv_label_set_text(time_label, time_str);
        }
    }

    // Update battery immediately (don't wait for first timer tick)
    char battery_str[32];
    bool charging = board.isCharging();
    int level = board.getBatteryLevel();
    if (level >= 0)
    {
        // Select appropriate battery symbol based on level
        const char* battery_symbol;
        if (charging)
        {
            battery_symbol = LV_SYMBOL_CHARGE;
        }
        else if (level >= 90)
        {
            battery_symbol = LV_SYMBOL_BATTERY_FULL;
        }
        else if (level >= 60)
        {
            battery_symbol = LV_SYMBOL_BATTERY_3;
        }
        else if (level >= 30)
        {
            battery_symbol = LV_SYMBOL_BATTERY_2;
        }
        else if (level >= 10)
        {
            battery_symbol = LV_SYMBOL_BATTERY_1;
        }
        else
        {
            battery_symbol = LV_SYMBOL_BATTERY_EMPTY;
        }
        snprintf(battery_str, sizeof(battery_str), "%s %d%%", battery_symbol, level);
        lv_label_set_text(battery_label, battery_str);
    }

    board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // GPS data collection task is now created in TLoRaPagerBoard::begin()

    // Create activity mutex for screen sleep management
    activity_mutex = xSemaphoreCreateMutex();
    if (activity_mutex == NULL)
    {
        log_e("Failed to create activity mutex");
    }
    else
    {
        // Initialize activity time
        last_user_activity_time = millis();

        // Load screen sleep timeout from preferences
        preferences.begin("settings", true);
        screen_sleep_timeout_ms = preferences.getUInt("sleep_timeout", 30000); // Default 30 seconds
        preferences.end();

        // Ensure minimum timeout
        if (screen_sleep_timeout_ms < 10000)
        {
            screen_sleep_timeout_ms = 30000;
        }
    }

    // Create screen sleep management task
    BaseType_t sleep_task_result = xTaskCreate(
        screenSleepTask,
        "screen_sleep",
        2 * 1024, // Stack size
        NULL,
        3, // Priority (lower than GPS task)
        &screen_sleep_task_handle);
    if (sleep_task_result != pdPASS)
    {
        log_e("Failed to create screen sleep task");
    }
    else
    {
        log_d("Screen sleep management task created successfully");
    }

    // If waking from deep sleep, update user activity to prevent immediate screen sleep
    if (waking_from_sleep)
    {
        updateUserActivity();
        log_d("Updated user activity after waking from sleep");
    }
}

// Forward declaration to check if USB mode is active
#ifdef ARDUINO_USB_MODE
extern bool ui_usb_is_active();
#endif

void loop()
{
    static uint32_t last_lvgl_ms = 0;
    constexpr uint32_t kLvglIntervalMs = 20;
    uint32_t now_ms = millis();

#ifdef ARDUINO_USB_MODE
    // If USB mode is active, run USB loop (like Launcher's loop() function)
    // This ensures USB tasks get CPU time and prevents other tasks from interfering
    if (ui_usb_is_active())
    {
        // Process LVGL for USB mode
        if (now_ms - last_lvgl_ms >= kLvglIntervalMs)
        {
            last_lvgl_ms = now_ms;
            lv_timer_handler();
        }

        // Yield to allow USB and other critical tasks to run
        // This is critical for USB stability (like Launcher's yield())
        yield();

        // Small delay to prevent CPU spinning
        delay(10);
        return;
    }
#endif

    // Normal processing when NOT in low power mode
    // Handle power button events
    board.handlePowerButton();

    // Update chat application context
    app::AppContext& app_ctx = app::AppContext::getInstance();
    app_ctx.update();

#if MAIN_TIMING_DEBUG
    static uint32_t last_loop_ms = 0;
    static uint32_t loop_count = 0;

    // Record loop() call interval
    if (last_loop_ms > 0)
    {
        uint32_t interval = now_ms - last_loop_ms;
        if (interval > 50)
        { // Only log intervals > 50ms (indicating delay)
            Serial.printf("[MAIN] loop() interval: %lu ms (count=%lu)\n", interval, loop_count);
        }
    }
    last_loop_ms = now_ms;
    loop_count++;
#endif

    // Normal processing only when NOT in low power mode
    bool run_lvgl = (now_ms - last_lvgl_ms >= kLvglIntervalMs);
#if MAIN_TIMING_DEBUG
    uint32_t t_before = 0;
#endif
    if (run_lvgl)
    {
        last_lvgl_ms = now_ms;
#if MAIN_TIMING_DEBUG
        t_before = millis();
#endif
        lv_timer_handler();
    }

#if MAIN_TIMING_DEBUG
    if (run_lvgl)
    {
        uint32_t t_after = millis();
        uint32_t handler_duration = t_after - t_before;

        // Log lv_timer_handler() execution time
        if (handler_duration > 10)
        {
            Serial.printf("[MAIN] lv_timer_handler() took %lu ms\n", handler_duration);
        }
    }
#endif

    delay(2);
}
