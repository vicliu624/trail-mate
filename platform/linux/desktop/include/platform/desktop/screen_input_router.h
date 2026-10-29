#pragma once

#include "platform/desktop/sdl_lvgl_host.h"
#include "platform/linux/screen_service.h"
#include <algorithm>
#include <set>

namespace platform::desktop
{
// UI composition policy: wake events never activate the page below the saver.
class ScreenInputRouter
{
  public:
    explicit ScreenInputRouter(unsigned height) : swipe_distance_(std::max(24U, height / 8)) {}
    static bool observe(void* context, const SdlLvglHost::InputActivity& input)
    {
        return static_cast<ScreenInputRouter*>(context)->route(input);
    }
    bool route(const SdlLvglHost::InputActivity& input)
    {
        namespace screen = platform::ui::screen;
        using Kind = SdlLvglHost::InputActivity::Kind;
        if (input.kind == Kind::Cancel)
        {
            suppressed_.clear();
            pointer_quarantine_ = gesture_ = false;
            return false;
        }
        const bool sleeping = screen::is_sleeping();
        const bool preview = screen::is_saver_active();
        bool consumed = sleeping || preview;
        if (input.kind == Kind::Key)
        {
            if (!input.pressed && suppressed_.erase(input.key)) return true;
            if (input.pressed && suppressed_.count(input.key)) return true;
            if (consumed && input.pressed)
            {
                if (suppressed_.size() < 128) suppressed_.insert(input.key);
                if (preview && (input.key == ' ' || input.key == 10 || input.key == 13)) screen::request_resume();
                else screen::handle_input();
            }
            else if (input.pressed) screen::record_activity();
        }
        else if (input.kind == Kind::Pointer)
        {
            if (pointer_quarantine_)
            {
                if (!input.pressed) pointer_quarantine_ = false;
                return true;
            }
            if (sleeping)
            {
                pointer_quarantine_ = input.pressed;
                gesture_ = false;
                screen::handle_input();
            }
            else if (preview)
            {
                screen::handle_input();
                if (input.pressed && !input.motion)
                {
                    gesture_ = true;
                    gesture_y_ = input.y;
                }
                else if (!input.pressed && !input.motion)
                {
                    if (gesture_ && gesture_y_ - input.y >= static_cast<int>(swipe_distance_)) screen::request_resume();
                    gesture_ = false;
                }
            }
            else screen::record_activity();
        }
        else
        {
            if (consumed) screen::handle_input();
            else screen::record_activity();
        }
        platform::linux_runtime::service_screen_runtime();
        return consumed;
    }

  private:
    unsigned swipe_distance_;
    std::set<uint32_t> suppressed_;
    bool pointer_quarantine_ = false;
    bool gesture_ = false;
    int gesture_y_ = 0;
};
} // namespace platform::desktop
