/**
 * @file LV_Helper_v9.cpp
 * @brief LVGL v9.x helper functions for T-LoRa-Pager
 *
 * This file provides LVGL initialization and integration functions,
 * including display driver setup, input device registration, and
 * custom memory management for LVGL v9.x.
 */

#include "input/morse_engine.h"
#include "ui/LV_Helper.h"
#include "walkie/walkie_service.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

#if LVGL_VERSION_MAJOR == 9

// Test toggles (set to 0 to disable)
#define LV_TEST_FORCE_DMA_BUF 0
#define LV_TEST_FORCE_DMA_FULL_SIZE 0
#define LV_TEST_FLUSH_LOG 0
#define LV_TEST_FLUSH_SAMPLE 0

static lv_display_t* disp_drv;
static lv_draw_buf_t draw_buf;
static lv_indev_t* indev_touch;
static lv_indev_t* indev_encoder;
static lv_indev_t* indev_keyboard;

static lv_color16_t* buf = nullptr;
static lv_color16_t* buf1 = nullptr;

#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_T_WATCH_S3)
#define _SWAP_COLORS
#endif

static void disp_flush(lv_display_t* disp_drv, const lv_area_t* area, uint8_t* color_p)
{
#if LV_TEST_FLUSH_LOG
    static uint32_t s_flush_count = 0;
    static uint32_t s_flush_last_ms = 0;
    static lv_area_t s_last_area = {0, 0, 0, 0};
#if LV_TEST_FLUSH_SAMPLE
    static uint32_t s_zero_streak = 0;
#endif
#endif
    size_t len = lv_area_get_size(area);
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    auto* plane = (LilyGo_Display*)lv_display_get_user_data(disp_drv);

#ifdef _SWAP_COLORS
    lv_draw_sw_rgb565_swap(color_p, len);
#endif

    plane->pushColors(area->x1, area->y1, w, h, (uint16_t*)color_p);

    lv_display_flush_ready(disp_drv);

#if LV_TEST_FLUSH_LOG
#if LV_TEST_FLUSH_SAMPLE
    bool all_zero_sample = false;
    if (color_p && len > 0)
    {
        const uint16_t* pix = reinterpret_cast<const uint16_t*>(color_p);
        size_t step = len / 8;
        if (step == 0) step = 1;
        all_zero_sample = true;
        for (size_t i = 0, idx = 0; i < 8 && idx < len; ++i, idx += step)
        {
            if (pix[idx] != 0)
            {
                all_zero_sample = false;
                break;
            }
        }
    }
    if (all_zero_sample)
    {
        s_zero_streak++;
    }
    else
    {
        s_zero_streak = 0;
    }
#endif
    s_flush_count++;
    s_last_area = *area;
    uint32_t now_ms = millis();
    if (now_ms - s_flush_last_ms >= 1000)
    {
        lv_obj_t* scr = lv_screen_active();
        unsigned child_cnt = scr ? (unsigned)lv_obj_get_child_cnt(scr) : 0;
        Serial.printf("[LVGL] flush/s=%lu last_area=%d,%d-%d,%d children=%u",
                      (unsigned long)s_flush_count,
                      (int)s_last_area.x1, (int)s_last_area.y1,
                      (int)s_last_area.x2, (int)s_last_area.y2,
                      child_cnt);
#if LV_TEST_FLUSH_SAMPLE
        Serial.printf(" zero_streak=%lu", (unsigned long)s_zero_streak);
#endif
        Serial.printf("\n");
        s_flush_count = 0;
        s_flush_last_ms = now_ms;
    }
#endif
}

#ifdef USING_INPUT_DEV_TOUCHPAD
// Forward declaration from main.cpp
extern bool isScreenSleeping();
extern void updateUserActivity();

static void touchpad_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    static int16_t x, y;
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    uint8_t touched = plane->getPoint(&x, &y, 1);
    if (touched)
    {
        input::MorseEngine::notifyTouch();
        if (isScreenSleeping())
        {
            updateUserActivity();
        }
        else
        {
            updateUserActivity();
        }
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}
#endif

#ifdef USING_INPUT_DEV_ROTARY
// Forward declaration from main.cpp
extern bool isScreenSleeping();
extern void updateUserActivity();
// Forward declaration from ui_gps.cpp
extern bool isGPSLoadingTiles();

static void lv_encoder_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    RotaryMsg_t msg = plane->getRotary();

    // If screen is sleeping, only wake it up, don't pass input to UI
    if (isScreenSleeping())
    {
        if (msg.dir != ROTARY_DIR_NONE || msg.centerBtnPressed)
        {
            updateUserActivity(); // Wake up screen
        }
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED; // Don't pass input to UI
        return;
    }

    // If GPS is loading tiles, ignore input
    if (isGPSLoadingTiles())
    {
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (walkie::is_active())
    {
        if (msg.dir == ROTARY_DIR_UP)
        {
            walkie::adjust_volume(1);
            updateUserActivity();
        }
        else if (msg.dir == ROTARY_DIR_DOWN)
        {
            walkie::adjust_volume(-1);
            updateUserActivity();
        }

        if (msg.centerBtnPressed)
        {
            data->enc_diff = 0;
            data->state = LV_INDEV_STATE_PRESSED;
            plane->feedback((void*)drv);
            return;
        }

        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // Screen is awake, process input normally
    if (msg.dir != ROTARY_DIR_NONE || msg.centerBtnPressed)
    {
        updateUserActivity(); // Update activity timestamp
    }

    switch (msg.dir)
    {
    case ROTARY_DIR_UP:
        data->enc_diff = 1;
        break;
    case ROTARY_DIR_DOWN:
        data->enc_diff = -1;
        break;
    default:
        data->state = LV_INDEV_STATE_RELEASED;
        break;
    }
    if (msg.centerBtnPressed)
    {
        data->state = LV_INDEV_STATE_PRESSED;
    }
    plane->feedback((void*)drv);
}
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
// Forward declaration from main.cpp
extern bool isScreenSleeping();
extern void updateUserActivity();

static void keypad_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    char c = '\0';
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    int state = plane->getKeyChar(&c);

    // If screen is sleeping, only wake it up, don't pass input to UI
    if (isScreenSleeping())
    {
        if (state == KEYBOARD_PRESSED)
        {
            updateUserActivity(); // Wake up screen
        }
        data->state = LV_INDEV_STATE_REL; // Don't pass key to UI
        return;
    }

    // Screen is awake, process input normally
    if (state == KEYBOARD_PRESSED || state == KEYBOARD_RELEASED)
    {
        walkie::on_key_event(c, state);
    }

    if (state == KEYBOARD_PRESSED)
    {
        updateUserActivity(); // Update activity timestamp
        data->key = c;
        data->state = LV_INDEV_STATE_PR;
        plane->feedback((void*)drv);
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}
#endif

static uint32_t lv_tick_get_callback(void)
{
    return millis();
}

static void lv_rounder_cb(lv_event_t* e)
{
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    if (!(area->x2 & 1))
        area->x2++;
    if (area->y1 & 1)
        area->y1--;
    if (!(area->y2 & 1))
        area->y2++;
}

static void lv_res_changed_cb(lv_event_t* e)
{
    auto* plane = (LilyGo_Display*)lv_event_get_user_data(e);
    plane->setRotation(lv_display_get_rotation(NULL));
}

void beginLvglHelper(LilyGo_Display& board, bool debug)
{
#ifdef _SWAP_COLORS
    log_d("Using color swap function");
#endif

    Serial.println("[LVGL] init");
    lv_init();
    Serial.println("[LVGL] init done");

#if LV_USE_LOG
    if (debug)
    {
        lv_log_register_print_cb(lv_log_print_g_cb);
    }
#endif

    // Allocate display buffers
    // Use DMA-capable memory if board supports DMA, otherwise use PSRAM
    bool useDMA = board.useDMA();
#if LV_TEST_FORCE_DMA_BUF
    useDMA = true;
#endif
    Serial.printf("[LVGL] buffer alloc start (useDMA=%d)\n", useDMA ? 1 : 0);
    Serial.printf("[LVGL] free heap internal=%u dma=%u psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
    size_t lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);
    size_t full_screen_size = lv_buffer_size;

    if (useDMA)
    {
        // For DMA, keep internal RAM pressure low to avoid starving other subsystems.
#if LV_TEST_FORCE_DMA_FULL_SIZE
        // keep full size for testing if memory allows
#else
        lv_buffer_size = (board.width() * board.height() / 6) * sizeof(lv_color16_t);
#endif
        buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        log_d("Using DMA buffers, size: %d bytes each", lv_buffer_size);

        if (!buf || !buf1)
        {
            Serial.println("[LVGL] DMA buffer alloc failed, fallback to PSRAM");
            if (buf)
            {
                free(buf);
            }
            if (buf1)
            {
                free(buf1);
            }
            buf = nullptr;
            buf1 = nullptr;
            useDMA = false;
        }
    }
    if (!useDMA)
    {
        lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);
#if HAS_PSRAM
        // For non-DMA, use full screen buffer in PSRAM
        buf = (lv_color16_t*)ps_malloc(lv_buffer_size);
        buf1 = (lv_color16_t*)ps_malloc(lv_buffer_size);
        log_d("Using PSRAM buffers, size: %d bytes each", lv_buffer_size);
        if (!buf || !buf1)
        {
            if (buf)
            {
                free(buf);
            }
            if (buf1)
            {
                free(buf1);
            }
            buf = nullptr;
            buf1 = nullptr;

            // Avoid grabbing huge internal buffers if PSRAM is unavailable.
            lv_buffer_size = (board.width() * board.height() / 8) * sizeof(lv_color16_t);
            if (lv_buffer_size < (board.width() * 20U * sizeof(lv_color16_t)))
            {
                lv_buffer_size = board.width() * 20U * sizeof(lv_color16_t);
            }
            buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
            buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
            if (!buf || !buf1)
            {
                if (buf)
                {
                    free(buf);
                }
                if (buf1)
                {
                    free(buf1);
                }
                buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
                buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
            }
            Serial.printf("[LVGL] PSRAM alloc failed, fallback to small internal buffers (size=%u)\n",
                          (unsigned)lv_buffer_size);
        }
#else
        // For boards without PSRAM, use heap
        buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
        buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
        Serial.println("[LVGL] PSRAM disabled, using heap buffers");
#endif
    }

    if (!buf || !buf1)
    {
        Serial.println("[LVGL] Failed to allocate display buffers");
        log_e("Failed to allocate LVGL display buffers!");
        return;
    }

    Serial.printf("[LVGL] buffers ready (size=%u)\n", static_cast<unsigned>(lv_buffer_size));
    disp_drv = lv_display_create(board.width(), board.height());

    if (board.needFullRefresh())
    {
        lv_display_render_mode_t mode = LV_DISPLAY_RENDER_MODE_FULL;
        if (lv_buffer_size < full_screen_size)
        {
            mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
            Serial.println("[LVGL] full-refresh downgraded to partial due buffer size");
        }
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, mode);
    }
    else
    {
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(disp_drv, lv_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    }
    Serial.println("[LVGL] display buffers set");
    Serial.printf("[LVGL] free heap after alloc internal=%u dma=%u psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
    lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp_drv, disp_flush);
    lv_display_set_user_data(disp_drv, &board);

    lv_display_set_resolution(disp_drv, board.width(), board.height());
    lv_display_set_rotation(disp_drv, (lv_display_rotation_t)board.getRotation());
    lv_display_add_event_cb(disp_drv, lv_res_changed_cb, LV_EVENT_RESOLUTION_CHANGED, &board);

#ifdef USING_INPUT_DEV_TOUCHPAD
    if (board.hasTouch())
    {
        indev_touch = lv_indev_create();
        lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_touch, touchpad_read);
        lv_indev_set_user_data(indev_touch, &board);
        lv_indev_enable(indev_touch, true);
        lv_indev_set_display(indev_touch, disp_drv);
        lv_indev_set_group(indev_touch, lv_group_get_default());
    }
#endif

#ifdef USING_INPUT_DEV_ROTARY
    if (board.hasEncoder())
    {
        indev_encoder = lv_indev_create();
        lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(indev_encoder, lv_encoder_read);
        lv_indev_set_user_data(indev_encoder, &board);
        lv_indev_enable(indev_encoder, true);
        lv_indev_set_display(indev_encoder, disp_drv);
        lv_indev_set_group(indev_encoder, lv_group_get_default());
    }
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
    if (board.hasKeyboard())
    {
        indev_keyboard = lv_indev_create();
        lv_indev_set_type(indev_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev_keyboard, keypad_read);
        lv_indev_set_user_data(indev_keyboard, &board);
        lv_indev_enable(indev_keyboard, true);
        lv_indev_set_display(indev_keyboard, disp_drv);
        lv_indev_set_group(indev_keyboard, lv_group_get_default());
    }
#endif

    lv_tick_set_cb(lv_tick_get_callback);
    lv_group_set_default(lv_group_create());
}

void lv_set_default_group(lv_group_t* group)
{
    lv_indev_t* cur_drv = NULL;
    for (;;)
    {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv)
        {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}

lv_indev_t* lv_get_touch_indev()
{
    return indev_touch;
}

lv_indev_t* lv_get_keyboard_indev()
{
    return indev_keyboard;
}

lv_indev_t* lv_get_encoder_indev()
{
    return indev_encoder;
}

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
// LVGL 9.x custom memory management functions
extern "C" void lv_mem_init(void)
{
    return; /*Nothing to init*/
}

extern "C" void lv_mem_deinit(void)
{
    return; /*Nothing to deinit*/
}

extern "C" lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes)
{
    /*Not supported*/
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    /*Not supported*/
    LV_UNUSED(pool);
    return;
}

extern "C" void* lv_malloc_core(size_t size)
{
#if HAS_PSRAM
    return ps_malloc(size);
#else
    return malloc(size);
#endif
}

extern "C" void* lv_realloc_core(void* p, size_t new_size)
{
#if HAS_PSRAM
    return ps_realloc(p, new_size);
#else
    return realloc(p, new_size);
#endif
}

extern "C" void lv_free_core(void* p)
{
    free(p);
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t* mon_p)
{
    /*Not supported*/
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /*Not supported*/
    return LV_RESULT_OK;
}
#endif

#endif
