/**
 * @file reticulum_call_overlay.cpp
 * @brief Global Reticulum call interruption page.
 */

#include "ui/widgets/reticulum_call_overlay.h"

#include "lvgl.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/callback_app_screen.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"

#include <cstdio>
#include <cstring>

namespace ui::widgets::reticulum_call_overlay
{
namespace
{

namespace call = ::platform::ui::reticulum_call;

constexpr uint32_t kPageBg = 0xFAF0D8;
constexpr uint32_t kHeaderBg = 0xEBA341;
constexpr uint32_t kLine = 0xE7C98F;
constexpr uint32_t kText = 0x6B4A1E;
constexpr uint32_t kTextDim = 0x8A6A3A;
constexpr uint32_t kDanger = 0xB94A2C;
constexpr uint32_t kDangerPressed = 0x95351F;
constexpr uint32_t kAccept = 0x3E7D3E;
constexpr uint32_t kAcceptPressed = 0x2F662F;
constexpr uint8_t kVolumeStep = 5;
constexpr uint32_t kActivationGuardMs = 400;

enum class Action : uint8_t
{
    Answer,
    Hangup,
    Decline,
    ToggleMode,
    ToggleMicrophone,
    ToggleSpeaker,
    TogglePtt,
};

struct PageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* peer = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* shortcut_hint = nullptr;
    lv_obj_t* answer_btn = nullptr;
    lv_obj_t* hangup_btn = nullptr;
    lv_obj_t* decline_btn = nullptr;
    lv_obj_t* controls = nullptr;
    lv_obj_t* mode_btn = nullptr;
    lv_obj_t* microphone_btn = nullptr;
    lv_obj_t* speaker_btn = nullptr;
    lv_obj_t* ptt_btn = nullptr;
    lv_group_t* group = nullptr;
    bool presented = false;
    uint32_t activation_armed_ms = 0;
    uint32_t last_present_failure_log_ms = 0;
    call::State last_state = call::State::Idle;
    call::RealtimePhase last_phase = call::RealtimePhase::Idle;
};

PageState s_page;
uint32_t s_failed_dial_revision = 0;

const lv_font_t* body_font()
{
    return ::ui::page_profile::resolve_body_font();
}

const lv_font_t* caption_font()
{
    return ::ui::page_profile::resolve_caption_font();
}

void set_label(lv_obj_t* label,
               const char* text,
               uint32_t color,
               const lv_font_t* font)
{
    if (!label)
    {
        return;
    }
    const char* value = text ? text : "";
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    ::ui::fonts::apply_localized_font(label,
                                      value,
                                      font ? font : body_font());
}

void stop_event(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    if (lv_indev_t* indev = lv_event_get_indev(event))
    {
        lv_indev_stop_processing(indev);
    }
}

bool activation_guard_active()
{
    return static_cast<int32_t>(lv_tick_get() - s_page.activation_armed_ms) < 0;
}

bool is_ending_key(uint32_t key)
{
    return key == LV_KEY_ESC || key == LV_KEY_BACKSPACE;
}

void update_shortcut_hint()
{
    char hint[48] = {};
    std::snprintf(hint,
                  sizeof(hint),
                  "+/- Volume %u%%",
                  static_cast<unsigned>(call::speaker_volume()));
    set_label(s_page.shortcut_hint, hint, kTextDim, caption_font());
}

bool handle_volume_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    const bool louder = key == '+' || key == '=';
    const bool quieter = key == '-' || key == '_';
    if (!louder && !quieter)
    {
        return false;
    }

    const int delta = louder ? static_cast<int>(kVolumeStep)
                             : -static_cast<int>(kVolumeStep);
    int next = static_cast<int>(call::speaker_volume()) + delta;
    if (next < 0)
    {
        next = 0;
    }
    else if (next > 100)
    {
        next = 100;
    }
    call::set_speaker_volume(static_cast<uint8_t>(next));
    update_shortcut_hint();
    stop_event(event);
    return true;
}

bool handle_focus_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !s_page.group)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_UP || key == LV_KEY_LEFT || key == LV_KEY_PREV)
    {
        lv_group_focus_prev(s_page.group);
        lv_group_set_editing(s_page.group, false);
        stop_event(event);
        return true;
    }
    if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT || key == LV_KEY_NEXT)
    {
        lv_group_focus_next(s_page.group);
        lv_group_set_editing(s_page.group, false);
        stop_event(event);
        return true;
    }
    return false;
}

bool page_root_valid()
{
    return s_page.root && lv_obj_is_valid(s_page.root);
}

void log_present_failure(const char* reason)
{
    const uint32_t now = lv_tick_get();
    if (now - s_page.last_present_failure_log_ms < 1000U)
    {
        return;
    }
    s_page.last_present_failure_log_ms = now;
    std::printf("[UI][Call] present failed reason=%s\n",
                reason ? reason : "unknown");
}

void root_key_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    if (handle_volume_key(event) || handle_focus_key(event))
    {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (is_ending_key(key))
    {
        stop_event(event);
        if (!activation_guard_active())
        {
            call::hangup();
        }
    }
}

void perform_action(Action action)
{
    switch (action)
    {
    case Action::Answer:
        if (!call::accept())
        {
            ::ui::feedback::show_notice("Call unavailable", 1800);
        }
        break;
    case Action::Hangup:
        call::hangup();
        break;
    case Action::Decline:
        call::reject();
        break;
    case Action::ToggleMode:
    {
        const call::Snapshot snapshot = call::snapshot();
        (void)call::set_duplex_mode(
            snapshot.duplex_mode == call::DuplexMode::Full
                ? call::DuplexMode::Half
                : call::DuplexMode::Full);
        break;
    }
    case Action::ToggleMicrophone:
    {
        const call::Snapshot snapshot = call::snapshot();
        call::set_microphone_muted(!snapshot.microphone_muted);
        break;
    }
    case Action::ToggleSpeaker:
    {
        const call::Snapshot snapshot = call::snapshot();
        call::set_speaker_muted(!snapshot.speaker_muted);
        break;
    }
    case Action::TogglePtt:
    {
        const call::Snapshot snapshot = call::snapshot();
        call::set_ptt_pressed(!snapshot.ptt_pressed);
        break;
    }
    }
}

void action_cb(lv_event_t* event)
{
    if (!event || handle_volume_key(event) || handle_focus_key(event))
    {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        if (is_ending_key(key))
        {
            stop_event(event);
            if (!activation_guard_active())
            {
                call::hangup();
            }
            return;
        }
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if (activation_guard_active())
    {
        stop_event(event);
        return;
    }
    stop_event(event);
    perform_action(static_cast<Action>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}

void style_button(lv_obj_t* button,
                  uint32_t background,
                  uint32_t pressed)
{
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(background), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_FOCUSED);
}

lv_obj_t* create_action_button(lv_obj_t* parent,
                               const char* text,
                               Action action,
                               uint32_t background,
                               uint32_t pressed)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    style_button(button, background, pressed);
    lv_obj_add_event_cb(button,
                        action_cb,
                        LV_EVENT_ALL,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));

    lv_obj_t* label = lv_label_create(button);
    set_label(label, text, 0xFFFFFF, body_font());
    lv_obj_center(label);
    if (s_page.group)
    {
        lv_group_add_obj(s_page.group, button);
    }
    return button;
}

lv_obj_t* app_parent()
{
    if (!main_screen || !lv_obj_is_valid(main_screen) ||
        lv_obj_get_child_count(main_screen) < 2)
    {
        return nullptr;
    }
    return lv_obj_get_child(main_screen, 1);
}

void enter_page(lv_obj_t* parent)
{
    if (!parent)
    {
        return;
    }
    lv_obj_clean(parent);
    s_page.group = lv_group_create();
    set_default_group(s_page.group);

    s_page.root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page.root);
    lv_obj_set_size(s_page.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page.root,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_page.root, lv_color_hex(kPageBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_page.root,
                             ::ui::page_profile::current().dense ? 8 : 12,
                             LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_page.root, 8, LV_PART_MAIN);
    lv_obj_add_flag(s_page.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_page.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_page.root, root_key_cb, LV_EVENT_KEY, nullptr);
    if (s_page.group)
    {
        lv_group_add_obj(s_page.group, s_page.root);
        lv_group_focus_obj(s_page.root);
    }

    s_page.title = lv_label_create(s_page.root);
    lv_obj_set_width(s_page.title, LV_PCT(100));
    lv_obj_set_style_bg_color(s_page.title, lv_color_hex(kHeaderBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page.title, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_page.title, 8, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_page.title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_page.peer = lv_label_create(s_page.root);
    lv_obj_set_width(s_page.peer, LV_PCT(100));
    lv_label_set_long_mode(s_page.peer, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_page.peer, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_page.status = lv_label_create(s_page.root);
    lv_obj_set_width(s_page.status, LV_PCT(100));
    lv_label_set_long_mode(s_page.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_page.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* spacer = lv_obj_create(s_page.root);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, 1);
    lv_obj_set_height(spacer, 0);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* actions = lv_obj_create(s_page.root);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions,
                      ::ui::page_profile::resolve_control_button_height() + 4);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_page.answer_btn = create_action_button(actions,
                                             "Answer",
                                             Action::Answer,
                                             kAccept,
                                             kAcceptPressed);
    s_page.hangup_btn = create_action_button(actions,
                                             "Hang up",
                                             Action::Hangup,
                                             kDanger,
                                             kDangerPressed);
    s_page.decline_btn = create_action_button(actions,
                                              "Decline",
                                              Action::Decline,
                                              kHeaderBg,
                                              0xC98118);

    s_page.controls = lv_obj_create(s_page.root);
    lv_obj_remove_style_all(s_page.controls);
    lv_obj_set_width(s_page.controls, LV_PCT(100));
    lv_obj_set_height(s_page.controls,
                      ::ui::page_profile::resolve_control_button_height() + 4);
    lv_obj_set_flex_flow(s_page.controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_page.controls,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s_page.mode_btn = create_action_button(s_page.controls,
                                           "FDX",
                                           Action::ToggleMode,
                                           kHeaderBg,
                                           0xC98118);
    s_page.microphone_btn = create_action_button(s_page.controls,
                                                 "Mic",
                                                 Action::ToggleMicrophone,
                                                 kAccept,
                                                 kAcceptPressed);
    s_page.speaker_btn = create_action_button(s_page.controls,
                                              "Speaker",
                                              Action::ToggleSpeaker,
                                              kAccept,
                                              kAcceptPressed);
    s_page.ptt_btn = create_action_button(s_page.controls,
                                          "PTT",
                                          Action::TogglePtt,
                                          kHeaderBg,
                                          0xC98118);

    s_page.shortcut_hint = lv_label_create(s_page.root);
    lv_obj_set_width(s_page.shortcut_hint, LV_PCT(100));
    lv_obj_set_style_text_align(s_page.shortcut_hint,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    s_page.activation_armed_ms = lv_tick_get() + kActivationGuardMs;
    set_label(s_page.title, "Reticulum call", kText, body_font());
    set_label(s_page.peer, "Connecting", kText, body_font());
    set_label(s_page.status, "Preparing call screen", kTextDim, caption_font());
    update_shortcut_hint();
}

void exit_page(lv_obj_t* parent)
{
    if (page_root_valid())
    {
        lv_obj_del(s_page.root);
    }
    if (s_page.group)
    {
        if (lv_group_get_default() == s_page.group)
        {
            set_default_group(nullptr);
        }
        lv_group_del(s_page.group);
    }
    const bool presented = s_page.presented;
    s_page = PageState{};
    s_page.presented = presented;
    (void)parent;
}

::ui::CallbackAppScreen s_call_app("reticulum_call",
                                   "Call",
                                   nullptr,
                                   enter_page,
                                   exit_page);

bool hash_has_value(const uint8_t* hash)
{
    if (!hash)
    {
        return false;
    }
    for (std::size_t index = 0; index < call::kHashSize; ++index)
    {
        if (hash[index] != 0)
        {
            return true;
        }
    }
    return false;
}

void format_hash_prefix(const char* label,
                        const uint8_t* hash,
                        char* out,
                        std::size_t out_len)
{
    std::snprintf(out,
                  out_len,
                  "%s %.2X%.2X%.2X%.2X",
                  label,
                  static_cast<unsigned>(hash[0]),
                  static_cast<unsigned>(hash[1]),
                  static_cast<unsigned>(hash[2]),
                  static_cast<unsigned>(hash[3]));
}

void format_peer_name(const call::Snapshot& snapshot,
                      char* out,
                      std::size_t out_len)
{
    if (snapshot.peer_name[0] != '\0')
    {
        std::snprintf(out, out_len, "%s", snapshot.peer_name);
    }
    else if (hash_has_value(snapshot.peer_destination_hash))
    {
        format_hash_prefix("Dest", snapshot.peer_destination_hash, out, out_len);
    }
    else if (hash_has_value(snapshot.peer_identity_hash))
    {
        format_hash_prefix("ID", snapshot.peer_identity_hash, out, out_len);
    }
    else
    {
        std::snprintf(out, out_len, "%s", "Identifying caller");
    }
}

void update_page(const call::Snapshot& snapshot)
{
    if (!page_root_valid())
    {
        return;
    }

    const bool identifying =
        snapshot.realtime_phase == call::RealtimePhase::IncomingIdentifying;
    const bool incoming =
        snapshot.realtime_phase == call::RealtimePhase::IncomingRinging;
    const bool starting =
        snapshot.realtime_phase == call::RealtimePhase::AcceptedStarting;
    const bool active =
        snapshot.realtime_phase == call::RealtimePhase::ActiveCall;
    const bool closing =
        snapshot.realtime_phase == call::RealtimePhase::ClosingCall;
    const bool changed = snapshot.state != s_page.last_state ||
                         snapshot.realtime_phase != s_page.last_phase;

    set_label(s_page.title,
              (identifying || incoming) ? "Incoming call"
                                        : (closing ? "Closing call"
                                                   : (snapshot.state == call::State::Outgoing
                                                          ? "Calling"
                                                          : "Reticulum call")),
              kText,
              body_font());

    char peer_name[48] = {};
    format_peer_name(snapshot, peer_name, sizeof(peer_name));
    set_label(s_page.peer, peer_name, kText, body_font());

    char status[96] = {};
    if (!snapshot.wifi_ready)
    {
        std::snprintf(status, sizeof(status), "%s", "Network gateway unavailable");
    }
    else if (!snapshot.media_supported)
    {
        std::snprintf(status, sizeof(status), "%s", "Audio hardware unavailable");
    }
    else if (identifying)
    {
        std::snprintf(status, sizeof(status), "%s", "Identifying caller");
    }
    else if (closing)
    {
        std::snprintf(status, sizeof(status), "%s", "Hanging up");
    }
    else if (starting)
    {
        std::snprintf(status, sizeof(status), "%s", "Establishing audio");
    }
    else if (active)
    {
        const uint32_t elapsed_s =
            snapshot.active_since_ms == 0
                ? 0
                : (sys::millis_now() - snapshot.active_since_ms) / 1000U;
        std::snprintf(status,
                      sizeof(status),
                      "%02lu:%02lu RX %lu/%luB TX %lu/%luB drop %lu/%lu",
                      static_cast<unsigned long>(elapsed_s / 60U),
                      static_cast<unsigned long>(elapsed_s % 60U),
                      static_cast<unsigned long>(snapshot.rx_packets),
                      static_cast<unsigned long>(snapshot.rx_bytes),
                      static_cast<unsigned long>(snapshot.tx_packets),
                      static_cast<unsigned long>(snapshot.tx_bytes),
                      static_cast<unsigned long>(snapshot.rx_dropped),
                      static_cast<unsigned long>(snapshot.tx_dropped));
    }
    else
    {
        std::snprintf(status, sizeof(status), "%s", "Secure call over Reticulum");
    }
    set_label(s_page.status, status, kTextDim, caption_font());
    update_shortcut_hint();

    if (active)
    {
        lv_obj_clear_flag(s_page.controls, LV_OBJ_FLAG_HIDDEN);
        set_label(lv_obj_get_child(s_page.mode_btn, 0),
                  snapshot.duplex_mode == call::DuplexMode::Full ? "FDX" : "HDX",
                  0xFFFFFF,
                  caption_font());
        set_label(lv_obj_get_child(s_page.microphone_btn, 0),
                  snapshot.microphone_muted ? "Mic off" : "Mic on",
                  0xFFFFFF,
                  caption_font());
        set_label(lv_obj_get_child(s_page.speaker_btn, 0),
                  snapshot.speaker_muted ? "Spk off" : "Spk on",
                  0xFFFFFF,
                  caption_font());
        set_label(lv_obj_get_child(s_page.ptt_btn, 0),
                  snapshot.ptt_pressed ? "TX on" : "PTT",
                  0xFFFFFF,
                  caption_font());
        if (snapshot.duplex_mode == call::DuplexMode::Full)
        {
            lv_obj_add_flag(s_page.ptt_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(s_page.ptt_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        lv_obj_add_flag(s_page.controls, LV_OBJ_FLAG_HIDDEN);
    }

    if (identifying)
    {
        lv_obj_add_flag(s_page.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_page.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        if (changed)
        {
            lv_group_focus_obj(activation_guard_active() ? s_page.root
                                                         : s_page.decline_btn);
        }
    }
    else if (incoming)
    {
        lv_obj_clear_flag(s_page.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_page.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        if (changed)
        {
            lv_group_focus_obj(activation_guard_active() ? s_page.root
                                                         : s_page.answer_btn);
        }
    }
    else if (closing)
    {
        lv_obj_add_flag(s_page.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        if (changed)
        {
            lv_group_focus_obj(s_page.root);
        }
    }
    else
    {
        lv_obj_add_flag(s_page.answer_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page.decline_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_page.hangup_btn, LV_OBJ_FLAG_HIDDEN);
        if (changed)
        {
            lv_group_focus_obj(activation_guard_active() ? s_page.root
                                                         : s_page.hangup_btn);
        }
    }
    if (!activation_guard_active() && s_page.group &&
        lv_group_get_focused(s_page.group) == s_page.root)
    {
        if (incoming && !lv_obj_has_flag(s_page.answer_btn, LV_OBJ_FLAG_HIDDEN))
        {
            lv_group_focus_obj(s_page.answer_btn);
        }
        else if (!identifying && !closing &&
                 !lv_obj_has_flag(s_page.hangup_btn, LV_OBJ_FLAG_HIDDEN))
        {
            lv_group_focus_obj(s_page.hangup_btn);
        }
        else if (identifying &&
                 !lv_obj_has_flag(s_page.decline_btn, LV_OBJ_FLAG_HIDDEN))
        {
            lv_group_focus_obj(s_page.decline_btn);
        }
    }
    if (s_page.group)
    {
        lv_group_set_editing(s_page.group, false);
    }
    s_page.last_state = snapshot.state;
    s_page.last_phase = snapshot.realtime_phase;
}

} // namespace

void release()
{
    if (!s_page.presented) return;
    auto* parent = app_parent();
    s_page.presented = false;
    ui_dismiss_interruption_app(parent);
}
void tick()
{
    call::service_ui_runtime();
    const call::Snapshot snapshot = call::snapshot();
    const bool active = snapshot.realtime_phase != call::RealtimePhase::Idle;
    if (snapshot.failed_dial_revision && snapshot.failed_dial_revision != s_failed_dial_revision)
    {
        s_failed_dial_revision = snapshot.failed_dial_revision;
        ::ui::feedback::show_notice("Call could not start", 2200);
    }

    if (active && s_page.presented && !page_root_valid())
    {
        lv_obj_t* parent = app_parent();
        if (parent)
        {
            s_call_app.exit(parent);
            s_call_app.enter(parent);
            s_page.presented = page_root_valid();
        }
        else
        {
            s_page.presented = false;
        }
    }

    if (active && !s_page.presented)
    {
        lv_obj_t* parent = app_parent();
        if (parent && ui_present_interruption_app(&s_call_app, parent))
        {
            s_page.presented = page_root_valid();
            if (!s_page.presented)
            {
                log_present_failure("no_root");
            }
        }
        else
        {
            log_present_failure("no_parent_or_switch");
        }
    }

    if (!active && s_page.presented)
    {
        lv_obj_t* parent = app_parent();
        s_page.presented = false;
        ui_dismiss_interruption_app(parent);
        return;
    }

    if (active)
    {
        update_page(snapshot);
    }
}

bool visible()
{
    return s_page.presented && s_page.root && lv_obj_is_valid(s_page.root);
}

} // namespace ui::widgets::reticulum_call_overlay
