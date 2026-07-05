#include "nrf52_node_ui_runtime.h"

#if defined(TRAILMATE_TARGET_T_IMPULSE_PLUS)

#include "nrf52_node_app_facade_runtime.h"
#include "platform/nrf52/debug/nrf52_debug_console.h"
#include "sys/clock.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>
#include <ctime>

namespace trailmate::apps::nrf52_node::ui_runtime
{
namespace
{

using target_board::BoardInputEvent;
using target_board::BoardInputKey;

enum class TinyView : uint8_t
{
    Time = 0,
    Protocol,
    SwitchConfirm,
    Notice,
};

bool s_initialized = false;
bool s_display_ok = false;
TinyView s_view = TinyView::Time;
TinyView s_notice_return_view = TinyView::Time;
uint32_t s_last_render_ms = 0;
uint32_t s_notice_until_ms = 0;
bool s_press_active = false;
bool s_long_handled = false;
uint32_t s_press_started_ms = 0;
char s_notice[16] = {};

uint32_t nowMs()
{
    return millis();
}

const char* protocolLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MESHCORE" : "MESHTASTIC";
}

chat::MeshProtocol activeProtocol()
{
    if (!AppFacadeRuntime::instance().isInitialized())
    {
        return chat::MeshProtocol::Meshtastic;
    }
    return AppFacadeRuntime::instance().getMeshProtocol();
}

chat::MeshProtocol oppositeProtocol(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? chat::MeshProtocol::Meshtastic : chat::MeshProtocol::MeshCore;
}

void formatTime(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    const uint32_t epoch = sys::epoch_seconds_now();
    if (epoch == 0)
    {
        std::snprintf(out, out_len, "--:--");
        return;
    }

    const time_t now = static_cast<time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::snprintf(out, out_len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

bool formatPairingPin(char* out, size_t out_len)
{
    if (!out || out_len == 0 || !AppFacadeRuntime::instance().isInitialized())
    {
        return false;
    }

#if !TRAILMATE_NRF52_BLE_DISABLED
    auto* ble_manager = AppFacadeRuntime::instance().getBleManager();
    ble::BlePairingStatus status{};
    if (ble_manager && ble_manager->getPairingStatus(&status) &&
        status.available &&
        status.requires_passkey &&
        status.is_pairing_active &&
        !status.is_connected &&
        status.passkey != 0)
    {
        std::snprintf(out, out_len, "PIN %06lu", static_cast<unsigned long>(status.passkey % 1000000UL));
        return true;
    }
#endif
    return false;
}

void setNotice(const char* text, uint32_t duration_ms)
{
    std::snprintf(s_notice, sizeof(s_notice), "%s", text ? text : "");
    s_notice_return_view = (s_view == TinyView::Notice) ? TinyView::Time : s_view;
    s_view = TinyView::Notice;
    s_notice_until_ms = nowMs() + duration_ms;
    s_last_render_ms = 0;
}

void drawCenteredLine(const char* text, uint8_t size)
{
    auto& board = target_board::instance();
    const int char_w = 6 * size;
    const int char_h = 8 * size;
    const int len = text ? static_cast<int>(std::strlen(text)) : 0;
    int x = (board.displayWidth() - (len * char_w)) / 2;
    if (x < 0)
    {
        x = 0;
    }
    int y = (board.displayHeight() - char_h) / 2;
    if (y < 0)
    {
        y = 0;
    }
    board.drawDisplayText(x, y, text ? text : "", size);
}

void render()
{
    if (!s_display_ok)
    {
        return;
    }

    const uint32_t now = nowMs();
    if (s_view == TinyView::Notice && s_notice_until_ms != 0 && static_cast<int32_t>(now - s_notice_until_ms) >= 0)
    {
        s_view = s_notice_return_view;
        s_notice_until_ms = 0;
    }

    char text[20] = {};
    uint8_t text_size = 1;
    if (formatPairingPin(text, sizeof(text)))
    {
        text_size = 1;
    }
    else
    {
        switch (s_view)
        {
        case TinyView::Time:
            formatTime(text, sizeof(text));
            text_size = 2;
            break;
        case TinyView::Protocol:
            std::snprintf(text, sizeof(text), "%s", protocolLabel(activeProtocol()));
            text_size = 1;
            break;
        case TinyView::SwitchConfirm:
            std::snprintf(text, sizeof(text), "TO %s",
                          oppositeProtocol(activeProtocol()) == chat::MeshProtocol::MeshCore ? "MC" : "MT");
            text_size = 1;
            break;
        case TinyView::Notice:
            std::snprintf(text, sizeof(text), "%s", s_notice);
            text_size = 1;
            break;
        }
    }

    auto& board = target_board::instance();
    board.clearDisplay();
    drawCenteredLine(text, text_size);
    board.presentDisplay();
    s_last_render_ms = now;
}

void switchProtocolAndRestart()
{
    if (!AppFacadeRuntime::instance().isInitialized())
    {
        setNotice("APP WAIT", 1200);
        return;
    }

    const chat::MeshProtocol next = oppositeProtocol(activeProtocol());
    platform::nrf52::debug_console::printf("%s button switch protocol target=%u\n",
                                           target_board::kLogTag,
                                           static_cast<unsigned>(next));
    setNotice("RESTART", 500);
    render();
    (void)AppFacadeRuntime::instance().switchMeshProtocol(next, true);
}

void handleShortPress()
{
    if (s_view == TinyView::SwitchConfirm)
    {
        s_view = TinyView::Protocol;
        platform::nrf52::debug_console::printf("%s button short cancel switch\n", target_board::kLogTag);
    }
    else
    {
        s_view = (s_view == TinyView::Time) ? TinyView::Protocol : TinyView::Time;
        platform::nrf52::debug_console::printf("%s button short view=%u\n",
                                               target_board::kLogTag,
                                               static_cast<unsigned>(s_view));
    }
    s_last_render_ms = 0;
}

void handleLongPress()
{
    if (s_view == TinyView::SwitchConfirm)
    {
        platform::nrf52::debug_console::printf("%s button long confirm switch\n", target_board::kLogTag);
        switchProtocolAndRestart();
        return;
    }

    if (s_view == TinyView::Protocol)
    {
        s_view = TinyView::SwitchConfirm;
        platform::nrf52::debug_console::printf("%s button long enter switch\n", target_board::kLogTag);
    }
    else
    {
        s_view = TinyView::Protocol;
        platform::nrf52::debug_console::printf("%s button long show protocol\n", target_board::kLogTag);
    }
    s_last_render_ms = 0;
}

void handleInput(const BoardInputEvent* event)
{
    if (!event || event->key != BoardInputKey::Function)
    {
        return;
    }

    const uint32_t now = nowMs();
    platform::nrf52::debug_console::printf("%s button event=%s t=%lu\n",
                                           target_board::kLogTag,
                                           event->pressed ? "down" : "up",
                                           static_cast<unsigned long>(now));
    if (event->pressed)
    {
        s_press_active = true;
        s_long_handled = false;
        s_press_started_ms = now;
        return;
    }

    if (!s_press_active)
    {
        return;
    }

    const uint32_t duration = now - s_press_started_ms;
    s_press_active = false;
    platform::nrf52::debug_console::printf("%s button release duration_ms=%lu long=%u\n",
                                           target_board::kLogTag,
                                           static_cast<unsigned long>(duration),
                                           duration >= target_board::instance().inputLongPressMs() ? 1U : 0U);
    if (duration >= target_board::instance().inputLongPressMs())
    {
        s_long_handled = true;
        handleLongPress();
    }
    else if (!s_long_handled)
    {
        handleShortPress();
    }
}

} // namespace

bool initialize()
{
    if (s_initialized)
    {
        return s_display_ok;
    }
    s_initialized = true;
    auto& board = target_board::instance();
    s_display_ok = board.beginDisplay();
    platform::nrf52::debug_console::printf("%s tiny ui init display=%s\n",
                                           target_board::kLogTag,
                                           s_display_ok ? "ok" : "fail");
    render();
    return s_display_ok;
}

void appendBootLog(const char* line)
{
    platform::nrf52::debug_console::printf("%s boot: %s\n", target_board::kLogTag, line ? line : "");
    if (!s_display_ok)
    {
        (void)initialize();
    }
}

void bindChatObservers()
{
}

void tick(const BoardInputEvent* event)
{
    if (!initialize())
    {
        return;
    }
    handleInput(event);
    const uint32_t now = nowMs();
    if (s_last_render_ms == 0 || (now - s_last_render_ms) >= 1000UL)
    {
        render();
    }
}

void showDisplayProbe()
{
    if (!initialize())
    {
        platform::nrf52::debug_console::printf("%s display probe skipped: ui init failed\n", target_board::kLogTag);
        return;
    }
    auto& board = target_board::instance();
    board.clearDisplay();
    board.drawDisplayFrame();
    board.drawDisplayText(17, 12, "TIP", 1);
    board.presentDisplay();
    delay(900);
    s_last_render_ms = 0;
}

} // namespace trailmate::apps::nrf52_node::ui_runtime

#else

#include "nrf52_node_app_facade_runtime.h"
#include "platform/nrf52/arduino_common/internal_fs_utils.h"
#include "platform/nrf52/debug/nrf52_debug_console.h"
#include "platform/ui/compass_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/time_runtime.h"
#include "sys/clock.h"
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
#include "ui/fonts/fusion_pixel_10_font.h"
#else
#include "ui/fonts/fusion_pixel_8_font.h"
#endif
#include "ui/mono/runtime.h"

#include <Arduino.h>
#include <InternalFileSystem.h>
#include <ctime>
#include <malloc.h>

namespace trailmate::apps::nrf52_node::ui_runtime
{
namespace
{
using Adafruit_LittleFS_Namespace::File;
using target_board::BoardInputEvent;
using target_board::BoardInputKey;
constexpr uint32_t kProbeHoldMs = 900;
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
constexpr uint32_t kIdleUiTickIntervalMs = 100;
#else
constexpr uint32_t kIdleUiTickIntervalMs = 500;
#endif
const char kProbeAscii[] = "ABC123";
const char kProbeCjk[] = "\xE4\xB8\xAD\xE6\x96\x87";
const char kProbeSymbols[] = "\xE2\x94\x80\xE2\x96\x88\xE2\x96\xA0";

extern "C"
{
    extern uint32_t __data_start__[];
    extern unsigned char __HeapBase[];
    extern unsigned char __HeapLimit[];
    extern uint32_t __StackTop[];
    extern uint32_t __StackLimit[];
    int dbgStackUsed(void);
}

uint32_t now_ms() { return millis(); }
time_t utc_now() { return static_cast<time_t>(sys::epoch_seconds_now()); }

bool app_ready()
{
    return AppFacadeRuntime::instance().isInitialized();
}

uint32_t active_lora_frequency_hz()
{
    return target_board::instance().activeLoraFrequencyHz();
}

bool format_freq(uint32_t freq_hz, char* out, size_t out_len)
{
    return target_board::instance().formatLoraFrequencyMHz(freq_hz, out, out_len);
}

ui::mono::HostCallbacks::ResourceUsage ram_usage()
{
    ui::mono::HostCallbacks::ResourceUsage usage{};
    usage.available = true;
    const uintptr_t ram_begin = reinterpret_cast<uintptr_t>(__data_start__);
    const uintptr_t heap_begin = reinterpret_cast<uintptr_t>(__HeapBase);
    const uintptr_t heap_end = reinterpret_cast<uintptr_t>(__HeapLimit);
    const uintptr_t stack_begin = reinterpret_cast<uintptr_t>(__StackLimit);
    const uintptr_t stack_end = reinterpret_cast<uintptr_t>(__StackTop);

    if (heap_begin <= ram_begin || heap_end < heap_begin || stack_end <= stack_begin || stack_end <= ram_begin)
    {
        return usage;
    }

    const uint32_t static_bytes = static_cast<uint32_t>(heap_begin - ram_begin);
    const uint32_t heap_total_bytes = static_cast<uint32_t>(heap_end - heap_begin);
    const uint32_t isr_stack_total_bytes = static_cast<uint32_t>(stack_end - stack_begin);
    usage.total_bytes = static_cast<uint32_t>(stack_end - ram_begin);

    struct mallinfo heap_info = mallinfo();
    uint32_t heap_used_bytes = 0;
    if (heap_info.uordblks > 0)
    {
        heap_used_bytes = static_cast<uint32_t>(heap_info.uordblks);
        if (heap_used_bytes > heap_total_bytes)
        {
            heap_used_bytes = heap_total_bytes;
        }
    }

    int isr_stack_used = dbgStackUsed();
    uint32_t isr_stack_used_bytes = 0;
    if (isr_stack_used > 0)
    {
        isr_stack_used_bytes = static_cast<uint32_t>(isr_stack_used);
        if (isr_stack_used_bytes > isr_stack_total_bytes)
        {
            isr_stack_used_bytes = isr_stack_total_bytes;
        }
    }

    usage.used_bytes = static_bytes + heap_used_bytes + isr_stack_used_bytes;
    if (usage.used_bytes > usage.total_bytes)
    {
        usage.used_bytes = usage.total_bytes;
    }
    return usage;
}

ui::mono::HostCallbacks::ResourceUsage flash_usage()
{
    ui::mono::HostCallbacks::ResourceUsage usage{};
    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(false))
    {
        return usage;
    }

    File root = InternalFS.open("/");
    if (!root)
    {
        return usage;
    }

    usage.available = true;
    usage.used_bytes = ::platform::nrf52::arduino_common::internal_fs::accumulateBytes(root);
    usage.total_bytes = target_board::kFsTotalBytes;
    root.close();
    return usage;
}

bool clear_volatile_storage()
{
    return AppFacadeRuntime::instance().clearVolatileStoragePreserveSettings();
}

uint8_t message_tone_volume()
{
    return platform::ui::device::default_message_tone_volume();
}

void set_message_tone_volume(uint8_t volume_percent)
{
    platform::ui::device::set_message_tone_volume(volume_percent);
}

void play_message_tone()
{
    platform::ui::device::play_message_tone();
}

#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
bool message_light_enabled()
{
    return target_board::instance().messageKeyboardLightEnabled();
}

void set_message_light_enabled(bool enabled)
{
    target_board::instance().setMessageKeyboardLightEnabled(enabled);
}

void play_message_light()
{
    target_board::instance().blinkMessageKeyboardLight(2);
}

uint8_t status_led_color_index()
{
    return target_board::instance().statusLedColor();
}

void set_status_led_color_index(uint8_t color_index)
{
    target_board::instance().setStatusLedColor(color_index);
}

uint8_t status_led_color_count()
{
    return target_board::Board::statusLedColorCount();
}

const char* status_led_color_label(uint8_t color_index)
{
    return target_board::Board::statusLedColorLabel(color_index);
}

bool keyboard_light_enabled()
{
    return target_board::instance().keyboardLightEnabled();
}

void set_keyboard_light_enabled(bool enabled)
{
    target_board::instance().setKeyboardLightEnabled(enabled);
}
#endif

ui::mono::InputAction to_input_action(
    const BoardInputEvent* event)
{
    if (!event || !event->pressed)
    {
        return ui::mono::InputAction::None;
    }

    switch (event->key)
    {
    case BoardInputKey::JoystickUp:
        return ui::mono::InputAction::Up;
    case BoardInputKey::JoystickDown:
        return ui::mono::InputAction::Down;
    case BoardInputKey::JoystickLeft:
        return ui::mono::InputAction::Left;
    case BoardInputKey::JoystickRight:
        return ui::mono::InputAction::Right;
    case BoardInputKey::JoystickPress:
        return ui::mono::InputAction::Select;
    case BoardInputKey::PrimaryButton:
        return ui::mono::InputAction::Primary;
    case BoardInputKey::SecondaryButton:
        return ui::mono::InputAction::Secondary;
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
    case BoardInputKey::Up:
        return ui::mono::InputAction::Up;
    case BoardInputKey::Down:
        return ui::mono::InputAction::Down;
    case BoardInputKey::Center:
    case BoardInputKey::Yes:
        return ui::mono::InputAction::Select;
    case BoardInputKey::No:
    case BoardInputKey::Escape:
        return ui::mono::InputAction::Back;
    case BoardInputKey::Home:
        return ui::mono::InputAction::Secondary;
    case BoardInputKey::Mail:
        return ui::mono::InputAction::Primary;
#endif
    default:
        return ui::mono::InputAction::None;
    }
}

bool s_initialized = false;
ui::mono::Runtime* s_runtime = nullptr;
bool s_probe_drawn = false;
uint32_t s_last_idle_ui_tick_ms = 0;

void drawProbePattern(::ui::mono::MonoDisplay& display, const ::ui::mono::MonoFont& font)
{
    ::ui::mono::TextRenderer renderer(font);
    display.clear();

    const int w = display.width();
    const int h = display.height();
    for (int x = 0; x < w; ++x)
    {
        display.drawPixel(x, 0, true);
        display.drawPixel(x, h - 1, true);
    }
    for (int y = 0; y < h; ++y)
    {
        display.drawPixel(0, y, true);
        display.drawPixel(w - 1, y, true);
    }
    for (int d = 0; d < 16; ++d)
    {
        display.drawPixel(2 + d, 2 + d, true);
        display.drawPixel(w - 3 - d, 2 + d, true);
    }

    display.fillRect(96, 8, 16, 8, true);
    display.fillRect(96, 20, 16, 8, false);
    display.drawHLine(8, 31, 40);

    renderer.drawText(display, 6, 8, kProbeAscii);
    renderer.drawText(display, 6, 20, kProbeCjk);
    renderer.drawText(display, 6, 32, kProbeSymbols);
    display.present();
}

void showPendingRuntimeFeedback()
{
    if (!s_runtime)
    {
        return;
    }

    chat::MessageId msg_id = 0;
    bool success = false;
    while (AppFacadeRuntime::instance().takeChatSendResultFeedback(&msg_id, &success))
    {
        (void)msg_id;
        s_runtime->showNotice("MESSAGE",
                              success ? "SENT" : "SEND FAILED",
                              success ? 1500U : 2000U);
    }
}

} // namespace

bool initialize()
{
    if (s_initialized)
    {
        return s_runtime != nullptr;
    }
    s_initialized = true;

    static ui::mono::HostCallbacks callbacks{};
    callbacks.app = &AppFacadeRuntime::instance();
    callbacks.app_ready_fn = app_ready;
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
    callbacks.ui_font = &platform::nrf52::ui::fonts::fusion_pixel_10_font();
#else
    callbacks.ui_font = &platform::nrf52::ui::fonts::fusion_pixel_8_font();
#endif
    callbacks.millis_fn = now_ms;
    callbacks.utc_now_fn = utc_now;
    callbacks.timezone_offset_min_fn = platform::ui::time::timezone_offset_min;
    callbacks.set_timezone_offset_min_fn = platform::ui::time::set_timezone_offset_min;
    callbacks.apply_timezone_offset_fn = platform::ui::time::apply_timezone_offset_for_utc;
    callbacks.active_lora_frequency_hz_fn = active_lora_frequency_hz;
    callbacks.format_frequency_fn = format_freq;
    callbacks.battery_info_fn = platform::ui::device::battery_info;
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
    callbacks.compass_state_fn = platform::ui::compass::get_state;
#endif
    callbacks.gps_data_fn = platform::ui::gps::get_data;
    callbacks.gps_enabled_fn = platform::ui::gps::is_enabled;
    callbacks.gps_powered_fn = platform::ui::gps::is_powered;
    callbacks.gps_acquire_power_lease_fn = platform::ui::gps::acquire_power_lease;
    callbacks.gps_release_power_lease_fn = platform::ui::gps::release_power_lease;
    callbacks.ram_usage_fn = ram_usage;
    callbacks.flash_usage_fn = flash_usage;
    callbacks.clear_volatile_storage_fn = clear_volatile_storage;
    callbacks.message_tone_volume_fn = message_tone_volume;
    callbacks.set_message_tone_volume_fn = set_message_tone_volume;
    callbacks.play_message_tone_fn = play_message_tone;
    callbacks.virtual_keyboard_layout = ui::mono::HostCallbacks::VirtualKeyboardLayout::FullKeyboard;
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
    callbacks.message_light_enabled_fn = message_light_enabled;
    callbacks.set_message_light_enabled_fn = set_message_light_enabled;
    callbacks.play_message_light_fn = play_message_light;
    callbacks.status_led_color_index_fn = status_led_color_index;
    callbacks.set_status_led_color_index_fn = set_status_led_color_index;
    callbacks.status_led_color_count_fn = status_led_color_count;
    callbacks.status_led_color_label_fn = status_led_color_label;
    callbacks.keyboard_light_enabled_fn = keyboard_light_enabled;
    callbacks.set_keyboard_light_enabled_fn = set_keyboard_light_enabled;
    callbacks.physical_text_input = true;
    callbacks.compose_candidate_page_size = 10;
#endif

    static ui::mono::Runtime runtime(target_board::instance().monoDisplay(),
                                     callbacks);
    s_runtime = &runtime;
    const bool ok = s_runtime->begin();
    platform::nrf52::debug_console::printf("%s ui init display=%s\n", target_board::kLogTag, ok ? "ok" : "fail");
    return ok;
}

void appendBootLog(const char* line)
{
    if (initialize() && s_runtime)
    {
        s_runtime->appendBootLog(line);
        s_runtime->tick(ui::mono::InputAction::None);
    }
}

void bindChatObservers()
{
    if (initialize() && s_runtime)
    {
        s_runtime->bindChatObservers();
    }
}

void tick(const BoardInputEvent* event)
{
    if (initialize() && s_runtime)
    {
        showPendingRuntimeFeedback();
        const auto action = to_input_action(event);
        bool has_pressed_input = event && event->pressed && event->key != BoardInputKey::None;
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
        has_pressed_input = has_pressed_input || (event && event->pressed && event->text != '\0');
#endif
        const uint32_t now = millis();
        if (!has_pressed_input && action == ui::mono::InputAction::None)
        {
            if (s_last_idle_ui_tick_ms != 0 && (now - s_last_idle_ui_tick_ms) < kIdleUiTickIntervalMs)
            {
                return;
            }
            s_last_idle_ui_tick_ms = now;
        }
        else
        {
            s_last_idle_ui_tick_ms = now;
        }

#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
        if (event && event->pressed && event->text != '\0')
        {
            s_runtime->typeText(event->text);
            if (action == ui::mono::InputAction::None)
            {
                return;
            }
        }
#endif
        s_runtime->tick(action);
    }
}

void showDisplayProbe()
{
    if (!initialize())
    {
        platform::nrf52::debug_console::printf("%s display probe skipped: ui init failed\n", target_board::kLogTag);
        return;
    }
    if (s_probe_drawn)
    {
        return;
    }

    auto& display = target_board::instance().monoDisplay();
#if defined(TRAILMATE_TARGET_T_ECHO_LITE)
    drawProbePattern(display, platform::nrf52::ui::fonts::fusion_pixel_10_font());
#else
    drawProbePattern(display, platform::nrf52::ui::fonts::fusion_pixel_8_font());
#endif
    s_probe_drawn = true;
    platform::nrf52::debug_console::printf("%s display probe rendered\n", target_board::kLogTag);
    delay(kProbeHoldMs);
}

} // namespace trailmate::apps::nrf52_node::ui_runtime

#endif
