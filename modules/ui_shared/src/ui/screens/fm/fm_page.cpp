#include "ui/screens/fm/fm_page.h"
#include "ui/assets/radio_icons.h"
#if (defined(ESP_PLATFORM) && defined(TRAIL_MATE_IDF_RADIO_LR2021)) || defined(TRAIL_MATE_PLATFORM_LINUX) || defined(TRAIL_MATE_EXTERNAL_FM_RUNTIME)
#include "platform/ui/fm_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "ui/app_runtime.h"
#include "ui/components/shortcut_help_modal.h"
#include "ui/input/pager_navigation.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar.h"
#include <array>
#include <cstdio>

namespace ui::fm
{
namespace
{
namespace runtime = platform::ui::fm;
struct State
{
    lv_obj_t *root = nullptr, *body = nullptr, *frequency = nullptr, *status = nullptr, *controls = nullptr;
    lv_obj_t *start = nullptr, *stop = nullptr, *stop_label = nullptr, *down = nullptr, *up = nullptr;
    lv_obj_t *volume = nullptr, *calibration = nullptr, *calibration_label = nullptr;
    lv_timer_t* timer = nullptr;
    ::ui::widgets::TopBar bar{};
    ::ui::components::shortcut_help_modal::State help;
    uint32_t hz = 100800000;
    bool exception = false, pending_start = false, rejected = false;
    uint64_t pending_session = 0;
    std::array<char, 512> message{};
    std::array<char, 64> chip{};
    struct Action
    {
        State* state;
        void (*call)(State&);
    };
    std::array<Action, 10> actions{};
    size_t action_count = 0;

    static lv_obj_t* text(lv_obj_t* parent, const char* value)
    {
        auto* label = lv_label_create(parent);
        lv_label_set_text(label, value);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        return label;
    }
    static void disabled(lv_obj_t* object, bool value)
    {
        if (value) lv_obj_add_state(object, LV_STATE_DISABLED);
        else lv_obj_remove_state(object, LV_STATE_DISABLED);
    }
    lv_obj_t* button(const char* caption, void (*call)(State&))
    {
        auto* b = lv_button_create(controls);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_bg_color(b, ::ui::theme::surface_alt(), 0);
        lv_obj_set_style_text_color(b, ::ui::theme::text(), 0);
        lv_obj_set_style_border_color(b, ::ui::theme::border(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, ::ui::theme::accent(), LV_STATE_FOCUSED);
        auto* label = lv_label_create(b);
        lv_label_set_text(label, caption);
        lv_obj_center(label);
        auto& action = actions[action_count++];
        action = {this, call};
        lv_obj_add_event_cb(
            b, [](lv_event_t* e)
            { auto& a = *static_cast<Action*>(lv_event_get_user_data(e)); a.call(*a.state); },
            LV_EVENT_CLICKED, &action);
        lv_obj_add_event_cb(b, key, LV_EVENT_KEY, this);
        lv_group_add_obj(app_g, b);
        return b;
    }
    void begin()
    {
        pending_start = false;
        const auto state = runtime::snapshot();
        if (!state.active && !state.cleanup_failed) rejected = !runtime::start(hz, exception);
        refresh();
    }
    void halt()
    {
        pending_start = false;
        runtime::request_stop();
        refresh();
    }
    void step(int direction)
    {
        const auto old = hz;
        if (direction < 0 && hz >= 100100000) hz -= 100000;
        if (direction > 0 && hz <= 107900000) hz += 100000;
        if (hz == old) return;
        const auto state = runtime::snapshot();
        if (state.active && state.error == runtime::Error::None && !state.cleanup_failed)
        {
            pending_start = true;
            pending_session = state.session;
            runtime::request_stop();
        }
        refresh();
    }
    void gain(int amount)
    {
        const int value = static_cast<int>(runtime::snapshot().volume) + amount;
        runtime::set_volume(value < 0 ? 0 : value > 100 ? 100
                                                        : static_cast<unsigned>(value));
        refresh();
    }
    static void key(lv_event_t* e)
    {
        auto& self = *static_cast<State*>(lv_event_get_user_data(e));
        const auto value = lv_event_get_key(e);
        if (value == LV_KEY_ESC) ui_request_exit_to_menu();
        else if (value == 'h' || value == 'H') self.show_help();
        else if (value == ' ')
        {
            const auto state = runtime::snapshot();
            if (state.active || state.cleanup_failed) self.halt();
            else self.begin();
        }
        else if (value == '[') self.step(-1);
        else if (value == ']') self.step(1);
        else if (value == '-') self.gain(-5);
        else if (value == '+' || value == '=') self.gain(5);
    }
    void show_help()
    {
        namespace modal = ::ui::components::shortcut_help_modal;
        if (modal::is_open(help))
        {
            modal::close(help);
            return;
        }
        static constexpr modal::Row rows[] = {
            {"Touch", nullptr, "Tap a control"},
            {"Tab", nullptr, "Select next control"},
            {"Enter", nullptr, "Activate selected control"},
            {"Space", nullptr, "Start / stop independent FM receiver"},
            {"[ / ]", nullptr, "Tune -/+ 0.1 MHz; waits for stop"},
            {"- / +", nullptr, "Change FM volume by 5 percent"},
            {"IMG_CAL", nullptr, "Calibration exception; stopped only"},
            {"Stop", nullptr, "Stop / retry failed audio cleanup"},
            {"H", nullptr, "Open / close this help"},
            {"Esc", nullptr, "Close help; otherwise return to Menu"},
            {"Back", nullptr, "Stop receiver and return to Menu"}};
        modal::Config config;
        config.title = "FM RADIO";
        config.rows = rows;
        config.row_count = sizeof(rows) / sizeof(rows[0]);
        config.width = 360;
        config.height = 320;
        config.restore_group = app_g;
        modal::open(help, root, config);
    }
    void refresh()
    {
        if (!root) return;
        auto state = runtime::snapshot();
        if (pending_start && !state.active)
        {
            pending_start = false;
            rejected = state.error != runtime::Error::None || state.cleanup_failed || state.session != pending_session;
            if (!rejected) rejected = !runtime::start(hz, exception);
            state = runtime::snapshot();
        }
        const bool supported = runtime::is_supported();
        lv_label_set_text_fmt(frequency, "Selected: %u.%u MHz", static_cast<unsigned>(hz / 1000000), static_cast<unsigned>((hz / 100000) % 10));
        lv_label_set_text_fmt(volume, "Volume %u%%", state.volume);
        lv_label_set_text(calibration_label, exception ? "Calibration exception: ON" : "Calibration: strict");
        lv_label_set_text(stop_label, state.cleanup_failed && !state.active ? "Retry cleanup" : "Stop");
        disabled(start, !supported || state.active || state.cleanup_failed || pending_start);
        disabled(stop, !state.active && !state.cleanup_failed && !pending_start);
        disabled(down, !supported || hz <= 100000000);
        disabled(up, !supported || hz >= 108000000);
        disabled(calibration, state.active || state.cleanup_failed || pending_start);
        const char* phase = "Ready";
        switch (state.phase)
        {
        case runtime::Phase::Idle:
            break;
        case runtime::Phase::Starting:
            phase = "Starting receiver";
            break;
        case runtime::Phase::PcmSubmitted:
            phase = "PCM submitted (not station lock)";
            break;
        case runtime::Phase::Stopping:
            phase = "Stopping / restoring resources";
            break;
        case runtime::Phase::Stopped:
            phase = "Stopped";
            break;
        case runtime::Phase::Error:
            phase = "Receiver error";
            break;
        }
        static const char* errors[] = {"None", "Radio unavailable or busy", "Audio output failed", "Not enough memory",
                                       "Cannot create worker", "Invalid or discontinuous IQ", "IQ queue full",
                                       "Radio stream failed", "Radio restoration failed", "Audio cleanup failed"};
        if (state.chip_flags_valid) std::snprintf(chip.data(), chip.size(), "Final chip flags: 0x%04x", state.chip_flags);
        else std::snprintf(chip.data(), chip.size(), "Final chip flags: not read / pending");
        std::snprintf(message.data(), message.size(), "%s%s\nSession: %u.%u MHz | PCM samples: %llu\n%s%s\n%s%s%s",
                      supported ? phase : "FM unavailable on this target", pending_start ? " / Retune queued" : "",
                      static_cast<unsigned>(state.frequency_hz / 1000000), static_cast<unsigned>((state.frequency_hz / 100000) % 10),
                      static_cast<unsigned long long>(state.pcm_samples), errors[static_cast<unsigned>(state.error)],
                      state.cleanup_failed ? " - codec still reserved; Stop retries cleanup" : "", chip.data(),
                      state.calibration_exception_requested ? " | IMG_CAL exception requested" : "",
                      rejected ? "\nStart/retune rejected; inspect receiver error" : "");
        lv_label_set_text(status, message.data());
    }
    void close()
    {
        pending_start = false;
        ::ui::components::shortcut_help_modal::close(help);
        if (root) runtime::request_stop();
        if (timer) lv_timer_delete(timer);
        timer = nullptr;
        if (root)
        {
            platform::ui::screen::enable_sleep();
            lv_obj_delete(root);
        }
        root = nullptr;
        action_count = 0;
    }
    void enter(lv_obj_t* parent)
    {
        close();
        const auto current = runtime::snapshot();
        hz = current.frequency_hz;
        exception = current.calibration_exception_requested;
        rejected = false;
        root = lv_obj_create(parent);
        platform::ui::screen::disable_sleep();
        lv_obj_set_size(root, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_set_style_radius(root, 0, 0);
        lv_obj_set_style_bg_color(root, ::ui::theme::page_bg(), 0);
        lv_obj_set_style_text_color(root, ::ui::theme::text(), 0);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        ::ui::widgets::TopBarConfig config;
        config.power_indicator = false;
        config.page_help = false; // This page owns working touch/keyboard Help on P4.
        ::ui::widgets::top_bar_init(bar, root, config);
        ::ui::widgets::top_bar_set_title(bar, "FM RADIO");
        ::ui::widgets::top_bar_set_back_callback(
            bar, [](void*)
            { ui_request_exit_to_menu(); },
            nullptr);
        body = lv_obj_create(root);
        lv_obj_set_width(body, lv_pct(100));
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(body, 8, 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_radius(body, 0, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        frequency = text(body, "Selected: 100.8 MHz");
        status = text(body, "Ready");
        text(body, "Experimental LR2021 RX: 100-108 MHz, mono. RF rate, passband and audible reception require device verification. Calibration exception permits IMG_CAL only.");
        controls = lv_obj_create(body);
        lv_obj_set_size(controls, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_all(controls, 6, 0);
        lv_obj_set_style_bg_color(controls, ::ui::theme::surface(), 0);
        lv_group_remove_all_objs(app_g);
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
        down = button("[ -0.1 MHz", [](State& s)
                      { s.step(-1); });
        start = button("Start", [](State& s)
                       { s.begin(); });
        stop = button("Stop", [](State& s)
                      { s.halt(); });
        stop_label = lv_obj_get_child(stop, 0);
        up = button("] +0.1 MHz", [](State& s)
                    { s.step(1); });
        button("Vol -", [](State& s)
               { s.gain(-5); });
        volume = text(controls, "Volume 50%");
        lv_obj_set_width(volume, 105);
        button("Vol +", [](State& s)
               { s.gain(5); });
        calibration = button("Calibration: strict", [](State& s)
                             { const auto state = runtime::snapshot(); if (!state.active && !state.cleanup_failed && !s.pending_start) s.exception = !s.exception; s.refresh(); });
        calibration_label = lv_obj_get_child(calibration, 0);
        button("H Help", [](State& s)
               { s.show_help(); });
        button("Back", [](State&)
               { ui_request_exit_to_menu(); });
        ::ui::input::pager::prepare_group(app_g);
        for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
            if (lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(input, app_g);
        timer = lv_timer_create([](lv_timer_t* timer)
                                { static_cast<State*>(lv_timer_get_user_data(timer))->refresh(); },
                                100, this);
        refresh();
        const auto state = runtime::snapshot();
        lv_group_focus_obj(state.active || state.cleanup_failed ? stop : start);
    }
};
struct Application final : AppScreen
{
    State state;
    const char* stable_id() const override { return "fm_radio"; }
    const char* name() const override { return "FM Radio"; }
    const lv_image_dsc_t* icon() const override { return &fm_radio_icon; }
    void enter(lv_obj_t* parent) override { state.enter(parent); }
    void exit(lv_obj_t*) override { state.close(); }
};
Application app;
} // namespace
AppScreen* application() { return runtime::is_supported() ? &app : nullptr; }
} // namespace ui::fm
#endif
