#pragma once
#include "app/input_event.h"
#include "text-input-unstable-v3-client-protocol.h"
#include <deque>
#include <utility>
#include <vector>

namespace trailmate::cardputer_zero::platform::wayland
{
// Protocol ownership only; no LVGL or application-page dependencies.
class WaylandTextInput
{
  public:
    WaylandTextInput(zwp_text_input_manager_v3* manager, wl_seat* seat, wl_surface* surface,
                     std::vector<app::InputEvent>& events) : surface_(surface), events_(events)
    {
        object_ = zwp_text_input_manager_v3_get_text_input(manager, seat);
        zwp_text_input_v3_add_listener(object_, &listener_, this);
    }
    ~WaylandTextInput() { zwp_text_input_v3_destroy(object_); }
    void update(const app::TextInputState& state)
    {
        const bool changed_focus = state.focus != state_.focus || state.enabled != state_.enabled ||
                                   state.password != state_.password || state.multiline != state_.multiline;
        if (changed_focus)
        {
            if (entered_ && enabled_)
            {
                zwp_text_input_v3_disable(object_);
                commit(0);
            }
            enabled_ = false;
            pending_ = {};
            defer_ = false;
            invalid_ = false;
            from_method_ = false;
        }
        const bool changed = changed_focus || state.surrounding != state_.surrounding || state.cursor != state_.cursor ||
                             state.password != state_.password || state.multiline != state_.multiline || state.x != state_.x || state.y != state_.y ||
                             state.width != state_.width || state.height != state_.height;
        state_ = state;
        dirty_ = dirty_ || changed;
        if (!entered_ || !state.enabled || (defer_ && !changed_focus) || (!dirty_ && enabled_)) return;
        if (!enabled_)
        {
            zwp_text_input_v3_enable(object_);
            enabled_ = true;
        }
        uint32_t hint = state.password ? ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA : 0;
        if (state.multiline) hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE;
        zwp_text_input_v3_set_content_type(object_, hint, state.password ? ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        // Never disclose password contents to the input method.
        if (!state.password && state.surrounding.size() <= 4000 && state.cursor >= 0 && app::utf8Boundary(state.surrounding, state.cursor))
            zwp_text_input_v3_set_surrounding_text(object_, state.surrounding.c_str(), state.cursor, state.cursor);
        zwp_text_input_v3_set_cursor_rectangle(object_, state.x, state.y, state.width, state.height);
        zwp_text_input_v3_set_text_change_cause(object_, from_method_ ? ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD : ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
        from_method_ = false;
        commit(state.focus);
        dirty_ = false;
    }

  private:
    void commit(uint64_t focus)
    {
        zwp_text_input_v3_commit(object_);
        serials_.emplace_back(++serial_, focus);
        if (serials_.size() > 64) serials_.pop_front();
    }
    static void enter(void* data, zwp_text_input_v3*, wl_surface* surface)
    {
        auto& self = *static_cast<WaylandTextInput*>(data);
        self.entered_ = surface == self.surface_;
        self.enabled_ = false;
        self.pending_ = {};
        self.defer_ = false;
        self.invalid_ = false;
        self.dirty_ = true;
        self.update(self.state_);
    }
    static void leave(void* data, zwp_text_input_v3*, wl_surface*)
    {
        auto& self = *static_cast<WaylandTextInput*>(data);
        if (self.enabled_ && self.events_.size() < 128)
        {
            app::InputEvent event;
            event.key = app::InputKey::TextEdit;
            event.edit.focus = self.state_.focus;
            self.events_.push_back(std::move(event));
        }
        self.entered_ = self.enabled_ = false;
        self.pending_ = {};
        self.invalid_ = false;
    }
    static void preedit(void* data, zwp_text_input_v3*, const char* text, int32_t begin, int32_t end)
    {
        auto& self = *static_cast<WaylandTextInput*>(data);
        const std::string_view value = text ? text : "";
        if (value.size() > 4000 || !app::validUtf8(value))
        {
            self.invalid_ = true;
            return;
        }
        self.pending_.preedit = value;
        self.pending_.preedit_begin = begin;
        self.pending_.preedit_end = end;
    }
    static void committed(void* data, zwp_text_input_v3*, const char* text)
    {
        auto& self = *static_cast<WaylandTextInput*>(data);
        const std::string_view value = text ? text : "";
        if (value.size() > 4000 || !app::validUtf8(value))
        {
            self.invalid_ = true;
            return;
        }
        self.pending_.commit = value;
    }
    static void deleted(void* data, zwp_text_input_v3*, uint32_t before, uint32_t after)
    {
        auto& self = *static_cast<WaylandTextInput*>(data);
        self.pending_.before = before;
        self.pending_.after = after;
    }
    static void done(void* data, zwp_text_input_v3*, uint32_t serial)
    {
        auto& self = *static_cast<WaylandTextInput*>(data);
        uint64_t focus = 0;
        for (const auto& value : self.serials_)
            if (value.first == serial) focus = value.second;
        if (focus && focus == self.state_.focus && serial == self.serial_) self.defer_ = false;
        if (self.entered_ && self.enabled_ && focus && focus == self.state_.focus && !self.invalid_ && self.events_.size() < 128)
        {
            self.pending_.focus = focus;
            app::InputEvent event;
            event.key = app::InputKey::TextEdit;
            event.edit = std::move(self.pending_);
            self.events_.push_back(std::move(event));
            self.from_method_ = true;
            self.defer_ = serial != self.serial_;
        }
        self.pending_ = {};
        self.invalid_ = false;
    }
    inline static const zwp_text_input_v3_listener listener_{enter, leave, preedit, committed, deleted, done};
    zwp_text_input_v3* object_ = nullptr;
    wl_surface* surface_;
    std::vector<app::InputEvent>& events_;
    app::TextInputState state_;
    app::TextEdit pending_;
    std::deque<std::pair<uint32_t, uint64_t>> serials_;
    uint32_t serial_ = 0;
    bool entered_ = false, enabled_ = false, invalid_ = false, defer_ = false, from_method_ = false, dirty_ = false;
};
} // namespace trailmate::cardputer_zero::platform::wayland
