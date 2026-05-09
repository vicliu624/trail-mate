#include "lvgl/lvgl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// Trail Mate shared shell session — available when the SDK project is
// configured to pull in platform/linux/common and modules/*.
// #include "ui/shell_ui_runner.h"

namespace
{

const char* getenvDefault(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

int detectSt7789vFramebuffer(char* dev_path, std::size_t buf_size)
{
    if (dev_path == nullptr || buf_size == 0U) return -1;

    FILE* fp = std::fopen("/proc/fb", "r");
    if (fp == nullptr) return -1;

    char line[256];
    int fb_num = -1;
    while (std::fgets(line, sizeof(line), fp) != nullptr)
    {
        if (std::strstr(line, "fb_st7789v") != nullptr &&
            std::sscanf(line, "%d", &fb_num) == 1)
        {
            break;
        }
    }
    std::fclose(fp);
    if (fb_num < 0) return -1;

    std::snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
    return 0;
}

void logStartupInfo(const char* fbdev, const char* input_dev,
                    const char* settings_root)
{
    std::printf("=== Trail Mate Cardputer Zero ===\n");
    std::printf("Display:        %s\n", fbdev ? fbdev : "(none)");
    std::printf("Input:          %s\n", input_dev ? input_dev : "(none)");
    std::printf("Settings root:  %s\n",
                settings_root ? settings_root : "(default)");
    std::printf("Runtime mode:   %s\n",
                getenvDefault("TRAIL_MATE_RUNTIME_MODE", "local"));
}

#if LV_USE_EVDEV
void initLinuxInput(lv_display_t* display)
{
    const char* pointer = std::getenv("LV_LINUX_MOUSE_DEVICE");
    if (pointer != nullptr && pointer[0] != '\0')
    {
        lv_indev_t* dev = lv_evdev_create(LV_INDEV_TYPE_POINTER, pointer);
        lv_indev_set_display(dev, display);
    }

    const char* keyboard = getenvDefault(
        "LV_LINUX_KEYBOARD_DEVICE",
        "/dev/input/by-path/platform-3f804000.i2c-event");
    if (keyboard != nullptr && keyboard[0] != '\0')
    {
        lv_indev_t* dev = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, keyboard);
        lv_indev_set_display(dev, display);
        std::printf("Keyboard: %s\n", keyboard);
    }
    else
    {
        std::fprintf(stderr,
                     "[device] No keyboard device found. "
                     "Set LV_LINUX_KEYBOARD_DEVICE to override.\n");
    }
}
#endif

#if LV_USE_LINUX_FBDEV
void initLinuxDisplay()
{
    char detected_path[64] = {0};
    const char* device = std::getenv("LV_LINUX_FBDEV_DEVICE");
    if ((device == nullptr || device[0] == '\0') &&
        detectSt7789vFramebuffer(detected_path, sizeof(detected_path)) == 0)
    {
        device = detected_path;
    }
    if (device == nullptr || device[0] == '\0')
    {
        device = "/dev/fb0";
    }

    std::printf("Framebuffer: %s\n", device);
    lv_display_t* display = lv_linux_fbdev_create();
    if (display == nullptr)
    {
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
    if (display == nullptr)
    {
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

    logStartupInfo(
        std::getenv("LV_LINUX_FBDEV_DEVICE"),
        std::getenv("LV_LINUX_KEYBOARD_DEVICE"),
        std::getenv("TRAIL_MATE_SETTINGS_ROOT"));

    // ------------------------------------------------------------------
    // When the SDK build links against platform/linux/common, replace the
    // bring-up placeholder below with the shared shell session:
    //
    //   #include "ui/shell_ui_runner.h"
    //
    //   trailmate::cardputer_zero::linux_ui::ShellSession shell;
    //   if (!shell.begin()) { std::fprintf(stderr, "Shell session failed\n"); return 1; }
    //
    //   trailmate::cardputer_zero::linux_ui::NativeLvglHost host{shell};
    //   while (true) { host.tick(); lv_timer_handler(); usleep(1000); }
    //
    // Until then, keep the bring-up UI as a visual confirmation that LVGL,
    // fbdev, and evdev are working correctly on the target hardware.
    // ------------------------------------------------------------------

    // Bring-up UI: confirms display and input plumbing work.
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Trail Mate");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 12);

    lv_obj_t* subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Cardputer Zero — SDK bring-up");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x93C5FD), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t* body = lv_label_create(screen);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 292);
    lv_label_set_text(body,
                      "LVGL fbdev + evdev + M5Stack_Linux_Libs plumbing verified.\n\n"
                      "Next: link platform/linux/common and switch this shell to\n"
                      "Trail Mate shared ShellSession + NativeLvglHost.");
    lv_obj_set_style_text_color(body, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 14, 66);

    while (true)
    {
        lv_timer_handler();
        usleep(1000);
    }

    return 0;
}
