#include "ui/fm_radio_app.h"
#include "platform/ui/screen_runtime.h"
#include "ui/app_runtime.h"
#include "ui/input/pager_navigation.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar.h"
#include <array>
#include <cstdio>

namespace platform::linux_runtime
{
struct FmRadioApp::Impl
{
    std::unique_ptr<FmSession> session;
    lv_obj_t *root = nullptr, *frequency = nullptr, *status = nullptr, *controls = nullptr;
    lv_obj_t *start = nullptr, *start_label = nullptr, *volume_label = nullptr, *warning_label = nullptr, *warning_button = nullptr;
    lv_timer_t* timer = nullptr;
    ::ui::widgets::TopBar bar{};
    uint32_t hz = 100800000;
    unsigned volume = 50;
    bool allow_warning = false, rejected = false;
    struct Action
    {
        Impl* self;
        void (*call)(Impl&);
    };
    std::array<Action, 9> actions{};
    size_t action_count = 0;
    explicit Impl(std::unique_ptr<FmSession> s) : session(std::move(s)) {}
    static lv_obj_t* text(lv_obj_t* parent, const char* value)
    {
        auto* label = lv_label_create(parent);
        lv_label_set_text(label, value);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        return label;
    }
    lv_obj_t* button(const char* caption, void (*call)(Impl&))
    {
        auto* b = lv_button_create(controls);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 36);
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
            { auto& a = *static_cast<Action*>(lv_event_get_user_data(e)); a.call(*a.self); },
            LV_EVENT_CLICKED, &action);
        lv_obj_add_event_cb(b, key, LV_EVENT_KEY, this);
        lv_group_add_obj(app_g, b);
        return b;
    }
    void toggle()
    {
        if (!session) return;
        if (session->snapshot().active) session->requestStop();
        else rejected = !session->tune(hz, allow_warning);
        refresh();
    }
    void step(int direction)
    {
        if (direction < 0 && hz >= 100100000) hz -= 100000;
        if (direction > 0 && hz <= 107900000) hz += 100000;
        if (session && session->snapshot().active) rejected = !session->tune(hz, allow_warning);
        refresh();
    }
    void changeVolume(int amount)
    {
        const int next = static_cast<int>(volume) + amount;
        volume = next < 0 ? 0 : next > 100 ? 100
                                           : static_cast<unsigned>(next);
        if (session) session->setVolume(volume);
        refresh();
    }
    static void key(lv_event_t* event)
    {
        auto& self = *static_cast<Impl*>(lv_event_get_user_data(event));
        const auto value = lv_event_get_key(event);
        if (value == LV_KEY_ESC) ui_request_exit_to_menu();
        else if (value == ' ') self.toggle();
        else if (value == '[') self.step(-1);
        else if (value == ']') self.step(1);
        else if (value == '-') self.changeVolume(-5);
        else if (value == '+' || value == '=') self.changeVolume(5);
    }
    void refresh()
    {
        if (!root) return;
        lv_label_set_text_fmt(frequency, "%u.%u MHz", hz / 1000000, (hz / 100000) % 10);
        lv_label_set_text_fmt(volume_label, "Volume %u%%", volume);
        lv_label_set_text(warning_label, allow_warning ? "IMG_CAL exception: ON" : "IMG_CAL exception: OFF");
        if (!session)
        {
            lv_label_set_text(status, "FM unavailable in this runtime mode. No simulated reception.");
            lv_obj_add_state(start, LV_STATE_DISABLED);
            return;
        }
        const auto s = session->snapshot();
        if (s.active) lv_obj_add_state(warning_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(warning_button, LV_STATE_DISABLED);
        lv_label_set_text(start_label, s.active ? "Stop" : "Start");
        const char* phase = "Stopped";
        switch (s.phase)
        {
        case FmSession::Phase::Idle:
            phase = "Ready";
            break;
        case FmSession::Phase::Starting:
            phase = "Starting receiver";
            break;
        case FmSession::Phase::PcmQueued:
            phase = "PCM submitted (not station lock)";
            break;
        case FmSession::Phase::Stopping:
            phase = "Stopping / restoring radio";
            break;
        case FmSession::Phase::Stopped:
            break;
        case FmSession::Phase::Error:
            phase = "Receiver error";
            break;
        }
        static const char* errors[] = {"None", "Packet receive handoff failed", "Radio unavailable or busy",
                                       "Audio output failed", "Invalid or discontinuous IQ", "IQ queue full",
                                       "Radio stream failed", "Radio restoration failed", "Internal worker failure"};
        char message[400];
        char chip[64];
        using SetupError = platform::shared::radio::FmLr2021Rx::Error;
        if (s.radio.setup_attempted && (s.radio.setup.error == SetupError::None || s.radio.setup.error == SetupError::ChipErrors))
            std::snprintf(chip, sizeof(chip), "Final chip flags: 0x%04x", s.radio.setup.chip_errors);
        else std::snprintf(chip, sizeof(chip), "Final chip flags: not read / pending");
        std::snprintf(message, sizeof(message), "%s%s\nSession: %u.%u MHz | PCM samples: %llu\n%s%s\n%s%s",
                      phase, rejected ? " / Request rejected: hardware unavailable" : "",
                      s.frequency_hz / 1000000, (s.frequency_hz / 100000) % 10,
                      static_cast<unsigned long long>(s.pcm_samples), errors[static_cast<unsigned>(s.error)],
                      s.lease_restore_failed || s.radio.recovery_error ? " | RESTORATION FAILED" : "",
                      chip, s.calibration_exception_requested ? " | IMG_CAL exception requested" : "");
        lv_label_set_text(status, message);
    }
    void close()
    {
        if (session) session->requestStop();
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
};
FmRadioApp::FmRadioApp(std::unique_ptr<FmSession> s) : impl_(std::make_unique<Impl>(std::move(s))) {}
FmRadioApp::~FmRadioApp()
{
    impl_->close();
    ui_detach_app(this);
}
void FmRadioApp::exit(lv_obj_t*) { impl_->close(); }
void FmRadioApp::enter(lv_obj_t* parent)
{
    auto& s = *impl_;
    s.close();
    s.root = lv_obj_create(parent);
    platform::ui::screen::disable_sleep();
    lv_obj_set_size(s.root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s.root, 0, 0);
    lv_obj_set_style_border_width(s.root, 0, 0);
    lv_obj_set_style_radius(s.root, 0, 0);
    lv_obj_set_style_bg_color(s.root, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_text_color(s.root, ::ui::theme::text(), 0);
    ::ui::widgets::TopBarConfig config;
    config.power_indicator = false;
    ::ui::widgets::top_bar_init(s.bar, s.root, config);
    ::ui::widgets::top_bar_set_title(s.bar, "FM RADIO");
    ::ui::widgets::top_bar_set_back_callback(
        s.bar, [](void*)
        { ui_request_exit_to_menu(); },
        nullptr);
    s.frequency = Impl::text(s.root, "100.8 MHz");
    s.status = Impl::text(s.root, "Ready");
    Impl::text(s.root, "Experimental LR2021 RX: 100.0-108.0 MHz, mono. RF rate/passband and audible reception require device verification.");
    s.controls = lv_obj_create(s.root);
    lv_obj_set_size(s.controls, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.controls, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(s.controls, 4, 0);
    lv_obj_set_style_bg_color(s.controls, ::ui::theme::surface(), 0);
    lv_group_remove_all_objs(app_g);
    s.button("[ -0.1 MHz", [](Impl& v)
             { v.step(-1); });
    s.start = s.button("Start", [](Impl& v)
                       { v.toggle(); });
    s.start_label = lv_obj_get_child(s.start, 0);
    s.button("] +0.1 MHz", [](Impl& v)
             { v.step(1); });
    s.button("Vol -", [](Impl& v)
             { v.changeVolume(-5); });
    s.volume_label = Impl::text(s.controls, "Volume 50%");
    lv_obj_set_width(s.volume_label, 105);
    s.button("Vol +", [](Impl& v)
             { v.changeVolume(5); });
    auto* warning = s.button("IMG_CAL exception: OFF", [](Impl& v)
                             { if (!v.session || !v.session->snapshot().active) v.allow_warning = !v.allow_warning; v.refresh(); });
    s.warning_label = lv_obj_get_child(warning, 0);
    s.warning_button = warning;
    s.button("H Help", [](Impl& v)
             { ::ui::input::pager::open_help(v.root, app_g); });
    s.button("Back", [](Impl&)
             { ui_request_exit_to_menu(); });
    ::ui::input::pager::prepare_group(app_g);
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(input, app_g);
    lv_group_focus_obj(s.start);
    s.timer = lv_timer_create([](lv_timer_t* timer)
                              { static_cast<Impl*>(lv_timer_get_user_data(timer))->refresh(); },
                              100, &s);
    s.refresh();
}
} // namespace platform::linux_runtime
