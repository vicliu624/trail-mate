#pragma once

#include <cstdint>
#include <stddef.h>

#include "lvgl.h"

namespace ui
{
namespace menu_runtime
{

enum class Scene : uint8_t
{
    Menu = 0,
    App,
    WatchFace,
    Sleeping,
};

struct Hooks
{
    struct WatchFaceHooks
    {
        void (*create)(lv_obj_t* parent) = nullptr;
        void (*set_time)(int hour, int minute, int month, int day, const char* weekday, int battery_percent) = nullptr;
        void (*set_node_id)(uint32_t node_id) = nullptr;
        void (*show)(bool show) = nullptr;
        bool (*is_ready)() = nullptr;
        void (*set_unlock_cb)(void (*cb)(void)) = nullptr;
    } watch_face{};

    bool (*format_time)(char* out, size_t out_len) = nullptr;
    void (*show_main_menu)() = nullptr;
};

void init(lv_obj_t* screen_root, lv_obj_t* main_screen, lv_obj_t* menu_panel, const Hooks& hooks);
void showWatchFace();
void onWakeFromSleep();
void setMenuActive(bool active);
void setScene(Scene scene);
Scene currentScene();

} // namespace menu_runtime
} // namespace ui
