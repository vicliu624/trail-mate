#include "lvgl.h"
#include "platform/desktop/sdl_lvgl_host.h"
#include <SDL.h>
#include <cstring>
#include <string>

struct Texts
{
    bool editing = true;
    std::string committed;
    std::string preedit;
    int start = 0, length = 0;
    static bool enabled(void* context) { return static_cast<Texts*>(context)->editing; }
    static void commit(void* context, const char* text) { static_cast<Texts*>(context)->committed += text; }
    static void composition(void* context, const char* text, int start, int length)
    {
        auto& self = *static_cast<Texts*>(context);
        self.preedit = text;
        self.start = start;
        self.length = length;
    }
};

struct Keys
{
    bool held = false;
    unsigned presses = 0;
    static bool observe(void* context, uint32_t key, bool down)
    {
        auto& self = *static_cast<Keys*>(context);
        if (!key) self.held = false;
        if (key != ' ') return false;
        self.held = down;
        if (down) ++self.presses;
        return true;
    }
};

int main()
{
    lv_init();
    for (int rotation : {0, 90, 180, 270})
    {
        platform::desktop::DeviceOptions options{"test", "test", 160, 80, rotation, false};
        options.hardware_keyboard = true;
        Keys keys;
        Texts texts;
        platform::desktop::SdlLvglHost host(options);
        host.setKeyObserver(Keys::observe, &keys);
        host.setTextObserver(Texts::enabled, Texts::commit, &texts, Texts::composition);
        lv_timer_handler();
        host.present();
        SDL_Event event{};
        event.type = SDL_MOUSEBUTTONDOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = 0;
        event.button.y = 0;
        SDL_PushEvent(&event);
        if (!host.pump()) return 1;
        lv_indev_t* pointer = lv_indev_get_next(nullptr);
        while (pointer && lv_indev_get_type(pointer) != LV_INDEV_TYPE_POINTER) pointer = lv_indev_get_next(pointer);
        if (!pointer) return 12;
        lv_indev_read(pointer);
        lv_point_t point{};
        lv_indev_get_point(pointer, &point);
        const int expected_x = rotation == 180 || rotation == 270 ? 159 : 0;
        const int expected_y = rotation == 90 || rotation == 180 ? 79 : 0;
        if (point.x != expected_x || point.y != expected_y) return 2;
        const auto flipped = (rotation + 180) % 360;
        host.setRotation(flipped);
        SDL_PushEvent(&event);
        if (!host.pump()) return 4;
        lv_indev_read(pointer);
        lv_indev_get_point(pointer, &point);
        const int flipped_x = flipped == 180 || flipped == 270 ? 159 : 0;
        const int flipped_y = flipped == 90 || flipped == 180 ? 79 : 0;
        if (point.x != flipped_x || point.y != flipped_y) return 5;
        unsigned clicks = 0;
        auto* button = lv_button_create(lv_screen_active());
        lv_obj_set_pos(button, 0, 0);
        lv_obj_set_size(button, options.width, options.height);
        lv_obj_add_event_cb(button, [](lv_event_t* event) { ++*static_cast<unsigned*>(lv_event_get_user_data(event)); }, LV_EVENT_CLICKED, &clicks);
        lv_obj_update_layout(button);
        event.type = SDL_MOUSEBUTTONUP;
        SDL_PushEvent(&event);
        if (!host.pump()) return 17;
        lv_indev_read(pointer);
        const auto baseline_clicks = clicks;
        event.type = SDL_MOUSEBUTTONDOWN;
        SDL_PushEvent(&event);
        event.type = SDL_MOUSEBUTTONUP;
        SDL_PushEvent(&event);
        if (!host.pump()) return 18;
        lv_indev_read(pointer);
        if (clicks != baseline_clicks + 1) return 19;
        event = {};
        event.type = SDL_FINGERDOWN;
        event.tfinger.x = .25f;
        event.tfinger.y = .25f;
        SDL_PushEvent(&event);
        event.type = SDL_FINGERUP;
        SDL_PushEvent(&event);
        if (!host.pump()) return 20;
        lv_indev_read(pointer);
        if (clicks != baseline_clicks + 2) return 21;
        lv_obj_delete(button);
        event = {};
        event.type = SDL_TEXTEDITING;
        std::strcpy(event.edit.text, "compose");
        event.edit.start = 2;
        event.edit.length = 3;
        SDL_PushEvent(&event);
        if (!host.pump() || texts.preedit != "compose" || texts.start != 2 || texts.length != 3 || !texts.committed.empty()) return 15;
#if SDL_VERSION_ATLEAST(2, 0, 22)
        event = {};
        event.type = SDL_TEXTEDITING_EXT;
        const std::string extended(100, 'x');
        event.editExt.text = SDL_strdup(extended.c_str());
        event.editExt.start = 20;
        event.editExt.length = 30;
        SDL_PushEvent(&event);
        if (!host.pump() || texts.preedit != extended || texts.start != 20 || texts.length != 30) return 16;
#endif
        event = {};
        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = SDLK_a;
        event.key.keysym.mod = KMOD_SHIFT;
        SDL_PushEvent(&event);
        event.type = SDL_KEYUP;
        SDL_PushEvent(&event);
        event = {};
        event.type = SDL_TEXTINPUT;
        std::strcpy(event.text.text, "A!\xE4\xB8\xAD");
        SDL_PushEvent(&event);
        if (!host.pump() || texts.committed != "A!\xE4\xB8\xAD" || !texts.preedit.empty()) return 9;
        auto* keyboard = lv_indev_get_next(nullptr);
        while (keyboard && lv_indev_get_type(keyboard) != LV_INDEV_TYPE_KEYPAD) keyboard = lv_indev_get_next(keyboard);
        if (!keyboard) return 13;
        lv_indev_read(keyboard);
        if (lv_indev_get_key(keyboard) != 0) return 10; // No duplicate raw 'a'.
        texts.editing = false;
        SDL_PushEvent(&event);
        if (!host.pump() || texts.committed != "A!\xE4\xB8\xAD") return 11;
        event = {};
        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = SDLK_SPACE;
        SDL_PushEvent(&event);
        SDL_PushEvent(&event);
        if (!host.pump() || !keys.held || keys.presses != 1) return 6;
        texts.editing = true;
        event = {};
        event.type = SDL_TEXTEDITING;
        std::strcpy(event.edit.text, "pending");
        SDL_PushEvent(&event);
        event = {};
        event.type = SDL_WINDOWEVENT;
        event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        SDL_PushEvent(&event);
        event = {};
        event.type = SDL_TEXTINPUT;
        std::strcpy(event.text.text, "stale");
        SDL_PushEvent(&event);
        if (!host.pump() || keys.held) return 7;
        if (texts.committed != "A!\xE4\xB8\xAD" || !texts.preedit.empty()) return 14;
        event = {};
        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = SDLK_SPACE;
        SDL_PushEvent(&event);
        if (!host.pump() || !keys.held) return 8;
        event.key.keysym.sym = SDLK_F4;
        event.key.keysym.mod = KMOD_ALT;
        SDL_PushEvent(&event);
        if (host.pump() || keys.held) return 3;
    }
    lv_deinit();
    return 0;
}
