#pragma once
#include "lvgl.h"
#include <cstdint>

namespace ui::widgets
{

class Toast
{
  public:
    enum class Type : uint8_t
    {
        Info,
        Success,
        Error
    };

    struct Options
    {
        uint32_t duration_ms = 1500; // How long the toast stays fully visible
        uint32_t fade_ms = 200;      // Fade‑in / fade‑out duration
        int y_offset = -20;          // Vertical offset from bottom (negative moves upward)

        uint8_t max_width_pct = 80; // Max width as a percentage of screen width
        uint8_t pad_h = 12;         // Horizontal padding
        uint8_t pad_v = 8;          // Vertical padding
    };

    static void show(lv_obj_t* parent, const char* text, Type type = Type::Info);
    static void show(lv_obj_t* parent, const char* text, Type type, const Options& opt);
    static void hide();

    static constexpr Options defaults() { return Options{}; }

  private:
    struct Impl;
    static void* active_;

    static lv_color_t bgColor(Type type);
    static void timer_cb(lv_timer_t* t);
    static void destroy(Impl* impl);

    static Impl* activeImpl() { return reinterpret_cast<Impl*>(active_); }
    static void setActiveImpl(Impl* p) { active_ = reinterpret_cast<void*>(p); }
};

} // namespace ui::widgets
