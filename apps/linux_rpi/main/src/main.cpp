#include "lvgl/lvgl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

const char* getenvDefault(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

int detectSt7789vFramebuffer(char* dev_path, std::size_t buf_size)
{
    if (dev_path == nullptr || buf_size == 0U) {
        return -1;
    }

    FILE* fp = std::fopen("/proc/fb", "r");
    if (fp == nullptr) {
        return -1;
    }

    char line[256];
    int fb_num = -1;
    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        if (std::strstr(line, "fb_st7789v") != nullptr && std::sscanf(line, "%d", &fb_num) == 1) {
            break;
        }
    }

    std::fclose(fp);
    if (fb_num < 0) {
        return -1;
    }

    std::snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
    return 0;
}

void createBringupUi()
{
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Trail Mate");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 12);

    lv_obj_t* subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Cardputer Zero bring-up");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x93C5FD), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t* body = lv_label_create(screen);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 292);
    lv_label_set_text(
        body,
        "This shell intentionally uses M5Stack_Linux_Libs for device-facing LVGL, "
        "fbdev and input bring-up. Trail Mate feature migration will layer on top "
        "of this SDK-backed runtime instead of copying UserDemo's app structure."
    );
    lv_obj_set_style_text_color(body, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 14, 66);

    lv_obj_t* hint = lv_label_create(screen);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 292);
    lv_label_set_text(
        hint,
        "Set LV_LINUX_FBDEV_DEVICE or let the runtime auto-detect fb_st7789v. "
        "Set LV_LINUX_KEYBOARD_DEVICE if evdev keyboard probing needs override."
    );
    lv_obj_set_style_text_color(hint, lv_color_hex(0xFCD34D), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 14, -12);
}

#if LV_USE_EVDEV
void initLinuxInput(lv_display_t* display)
{
    const char* pointer_device = std::getenv("LV_LINUX_MOUSE_DEVICE");
    if (pointer_device != nullptr && pointer_device[0] != '\0') {
        lv_indev_t* pointer = lv_evdev_create(LV_INDEV_TYPE_POINTER, pointer_device);
        lv_indev_set_display(pointer, display);
    }

    const char* keyboard_device = getenvDefault(
        "LV_LINUX_KEYBOARD_DEVICE",
        "/dev/input/by-path/platform-3f804000.i2c-event"
    );
    if (keyboard_device != nullptr && keyboard_device[0] != '\0') {
        lv_indev_t* keyboard = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, keyboard_device);
        lv_indev_set_display(keyboard, display);
    }
}
#endif

#if LV_USE_LINUX_FBDEV
void initLinuxDisplay()
{
    char detected_path[64] = {0};
    const char* device = std::getenv("LV_LINUX_FBDEV_DEVICE");
    if ((device == nullptr || device[0] == '\0') && detectSt7789vFramebuffer(detected_path, sizeof(detected_path)) == 0) {
        device = detected_path;
    }
    if (device == nullptr || device[0] == '\0') {
        device = "/dev/fb0";
    }

    std::printf("Using framebuffer device: %s\n", device);
    lv_display_t* display = lv_linux_fbdev_create();
    if (display == nullptr) {
        std::fprintf(stderr, "Failed to create Linux fbdev display\n");
        std::exit(1);
    }

    lv_linux_fbdev_set_file(display, device);

#if LV_USE_EVDEV
    initLinuxInput(display);
#endif
}
#elif LV_USE_SDL
void initLinuxDisplay()
{
    const int width = std::atoi(getenvDefault("LV_SDL_VIDEO_WIDTH", "320"));
    const int height = std::atoi(getenvDefault("LV_SDL_VIDEO_HEIGHT", "170"));

    lv_display_t* display = lv_sdl_window_create(width, height);
    if (display == nullptr) {
        std::fprintf(stderr, "Failed to create SDL display\n");
        std::exit(1);
    }

    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
}
#else
#error Unsupported LVGL backend configuration
#endif

} // namespace

int main()
{
    lv_init();
    initLinuxDisplay();
    createBringupUi();

    while (true) {
        lv_timer_handler();
        usleep(1000);
    }

    return 0;
}
