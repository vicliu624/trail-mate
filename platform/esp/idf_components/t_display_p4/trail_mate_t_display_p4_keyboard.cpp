#include "bsp/trail_mate_t_display_p4_keyboard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "bus/i2c/hardware_i2c_1.h"
#include "chip/i2c/tca8418.h"
#include "chip/i2c/xl95x5.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "platform/ui/walkie_runtime.h"
// LVGL 9 exposes callback/user-data getters but no event-filter getter.
// Inspect the descriptor only to distinguish explicit page KEY contracts from
// unrelated DELETE, draw and click callbacks.
#include "src/misc/lv_event_private.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_common.h"

extern "C" void trail_mate_idf_note_user_activity(void);

namespace
{

constexpr const char* kTag = "t-display-p4-kbd";

// P2 has a dedicated HP I2C0 bus. SYS devices use LP I2C0 and the external
// devices use HP I2C1, matching LilyGO's three-bus allocation. The TCA8418
// device borrows the XL9555 bus; it never owns or reconfigures its GPIOs.
constexpr uint32_t kPowerSettleMs = 50;
constexpr uint32_t kResetDelayMs = 10;
constexpr uint32_t kPollIntervalMs = 30;
// IDF converts the SDK timeout to FreeRTOS ticks before waiting for the I2C
// completion interrupt. P4 runs at 100 Hz: the old 5 ms became zero ticks and
// could reject an attached keyboard before its ACK interrupt was delivered.
constexpr uint32_t kI2cProbeTimeoutMs = 20;
static_assert(pdMS_TO_TICKS(kI2cProbeTimeoutMs) >= 2, "Keyboard probe needs at least two RTOS ticks");
constexpr uint32_t kI2cFailureBackoffMs = 500;
constexpr uint32_t kI2cFailureLogIntervalMs = 2000;
constexpr uint32_t kAttachRetryMs = 5000;
constexpr uint8_t kTca8418FifoDepth = 10;
constexpr uint32_t kAltDoublePressMs = 350;

constexpr uint32_t kKeyCaps = 0x8B;
constexpr uint32_t kKeyAlt = 0x8C;
constexpr uint32_t kKeyCtrl = 0x8D;
constexpr uint32_t kKeyFn = 0x8E;
constexpr uint32_t kKeyWin = 0x8F;
constexpr uint32_t kKeyShift = 0x90;
constexpr uint32_t kKeyF11 = 0x91;
constexpr uint32_t kKeyRecord = 0x92;
constexpr size_t kEventQueueCapacity = 32;

// These are the LilyGO TCA8418 scan maps used by the K270 keyboard example.
// The controller reports a key number (1-based), not a matrix coordinate.
constexpr std::array<uint32_t, 68> kKeyMap = {
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    LV_KEY_ESC, LV_KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    kKeyCaps, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    kKeyAlt, 'z', 'x', 'c', 'v', 'b', 'n', 'm',
    kKeyCtrl, LV_KEY_UP,
    kKeyFn, kKeyWin, kKeyShift, LV_KEY_NEXT,
    ' ', ' ', ' ', kKeyFn, LV_KEY_LEFT, LV_KEY_DOWN,
    0x91, '9', LV_KEY_BACKSPACE, LV_KEY_ENTER, 0x92, LV_KEY_ENTER, '0', LV_KEY_RIGHT};

constexpr std::array<uint32_t, 68> kShiftKeyMap = {
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    kKeyCaps, kKeyAlt, '!', '@', '#', '$', '%', '^', '&', '*',
    '\'', '_', '-', '+', '=', '\\', '|', ';', ':', '"',
    kKeyCtrl, '~', '[', ']', '{', '}', ',', '`', '/', '?',
    kKeyFn, kKeyWin, kKeyShift, 0x91, 0x92, '.', '<', '>',
    0x93, LV_KEY_UP,
    0x95, 0x96, kKeyShift, LV_KEY_NEXT,
    ' ', ' ', ' ', 0x9C, LV_KEY_LEFT, LV_KEY_DOWN,
    0x9F, '(', LV_KEY_BACKSPACE, LV_KEY_ENTER, 0xA2, LV_KEY_ENTER, ')', LV_KEY_RIGHT};

bool s_ready = false;
bool s_caps_lock = false;
bool s_shift = false;
bool s_fn = false;
lv_timer_t* s_poll_timer = nullptr;
uint32_t s_next_i2c_attempt_ms = 0;
uint32_t s_last_i2c_failure_log_ms = 0;
uint32_t s_last_alt_press_ms = 0;
struct KeyEvent
{
    uint8_t number = 0;
    bool pressed = false;
};
std::array<KeyEvent, kEventQueueCapacity> s_event_queue{};
std::array<bool, kKeyMap.size()> s_physical_down{};
size_t s_event_queue_head = 0;
size_t s_event_queue_count = 0;
lv_indev_t* s_keypad = nullptr;
lv_group_t* s_route_group = nullptr;
lv_obj_t* s_route_proxy = nullptr;
lv_obj_t* s_pressed_screen = nullptr;
lv_obj_t* s_pressed_focus = nullptr;
lv_obj_t* s_route_page = nullptr;
uint8_t s_pressed_number = 0;
uint32_t s_pressed_key = 0;
bool s_pressed_consumed = false;
bool s_release_pending = false;
uint8_t s_i2c_failures = 0;
uint32_t s_next_attach_ms = 0;
uint8_t s_ptt_number = 0;
lv_obj_t* s_ptt_screen = nullptr;
std::shared_ptr<cpp_bus_driver::HardwareI2c1> s_xl9555_bus;
std::shared_ptr<cpp_bus_driver::HardwareI2c1> s_tca8418_bus;
std::unique_ptr<cpp_bus_driver::Xl95x5> s_xl9555;
std::unique_ptr<cpp_bus_driver::Tca8418> s_tca8418;

const boards::t_display_p4::BoardProfile::KeyboardModule& keyboard_module()
{
    return boards::t_display_p4::TDisplayP4Board::profile().keyboard;
}

void reset_vendor_driver_state()
{
    // The vendor wrappers have no RAII Deinit. Remove the borrowed device
    // before the owning device/bus, including after a failed probe or retry.
    if (s_tca8418_bus) s_tca8418_bus->Deinit(false);
    if (s_xl9555_bus) s_xl9555_bus->Deinit(true);
    s_tca8418.reset();
    s_xl9555.reset();
    s_tca8418_bus.reset();
    s_xl9555_bus.reset();
}

bool reset_tca8418_via_xl9555()
{
    using Xl95x5 = cpp_bus_driver::Xl95x5;
    if (s_xl9555 == nullptr ||
        !s_xl9555->GpioWrite(Xl95x5::Pin::kIo6, 0) ||
        !s_xl9555->SetGpioMode(Xl95x5::Pin::kIo6, Xl95x5::Mode::kOutput))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));

    if (!s_xl9555->GpioWrite(Xl95x5::Pin::kIo6, 1))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));
    return true;
}

esp_err_t probe_keyboard_bus(uint16_t address)
{
    // Hot-plug detection runs on the UI task: never use the vendor's 1 s
    // timeout when an optional accessory or one of its lines is unavailable.
    // Keep the SDK result: NACK and timeout require different diagnosis.
    if (!s_xl9555_bus) return ESP_ERR_INVALID_STATE;
    return i2c_master_probe(s_xl9555_bus->bus_handle(), address, kI2cProbeTimeoutMs);
}

bool initialize_controller()
{
    const auto& keyboard = keyboard_module();
    auto& board = boards::t_display_p4::TDisplayP4Board::instance();
    if (!board.ensureKeyboardLdo4Power())
    {
        ESP_LOGW(kTag, "P4 keyboard cannot prepare GPIO45/46 I/O domain at 3300mV");
        return false;
    }
    // The I/O supply is held for the board's lifetime. Settle it before UI
    // startup only; sleeping for 50 ms on every missing-keyboard retry would
    // stall rendering/touch and cause periodic display-lock timeouts.
    if (s_poll_timer == nullptr) vTaskDelay(pdMS_TO_TICKS(kPowerSettleMs));

    reset_vendor_driver_state();
    s_xl9555_bus = std::make_shared<cpp_bus_driver::HardwareI2c1>(keyboard.sda, keyboard.scl, I2C_NUM_0);
    if (!s_xl9555_bus->InitBus())
    {
        ESP_LOGW(kTag, "Keyboard HP I2C0 bus initialization failed");
        reset_vendor_driver_state();
        return false;
    }
    const esp_err_t xl9555_probe = probe_keyboard_bus(keyboard.xl9555);
    if (xl9555_probe != ESP_OK)
    {
        // Probe before constructing/configuring devices: an absent accessory
        // should not flood the console with five vendor config lines per retry.
        const uint32_t now = lv_tick_get();
        if (!s_last_i2c_failure_log_ms || static_cast<uint32_t>(now - s_last_i2c_failure_log_ms) >= 30000)
        {
            s_last_i2c_failure_log_ms = now ? now : 1;
            const esp_err_t tca8418_probe = probe_keyboard_bus(keyboard.tca8418);
            uint8_t expander_mask = 0;
            // A timed-out bus cannot diagnose address straps. Avoid seven
            // additional timeout waits under the LVGL lock in that case.
            const bool scan_alternates = xl9555_probe == ESP_ERR_NOT_FOUND;
            if (scan_alternates)
            {
                for (unsigned index = 1; index < 8; ++index)
                    if (probe_keyboard_bus(0x20 + index) == ESP_OK) expander_mask |= 1U << index;
            }
            const auto irq = static_cast<gpio_num_t>(keyboard.interrupt);
            (void)gpio_set_direction(irq, GPIO_MODE_INPUT);
            (void)gpio_set_pull_mode(irq, GPIO_PULLDOWN_ONLY);
            ESP_LOGW(kTag, "Keyboard P2 idle SDA%d=%d SCL%d=%d INT%d=%d",
                     keyboard.sda, gpio_get_level(static_cast<gpio_num_t>(keyboard.sda)),
                     keyboard.scl, gpio_get_level(static_cast<gpio_num_t>(keyboard.scl)),
                     keyboard.interrupt, gpio_get_level(irq));
            ESP_LOGW(kTag, "Keyboard probe XL9555=%s TCA8418=%s alternate_expanders=0x%02X scanned=%u wait=%lums/%luticks",
                     esp_err_to_name(xl9555_probe), esp_err_to_name(tca8418_probe),
                     expander_mask, scan_alternates ? 1U : 0U,
                     static_cast<unsigned long>(kI2cProbeTimeoutMs),
                     static_cast<unsigned long>(pdMS_TO_TICKS(kI2cProbeTimeoutMs)));
        }
        reset_vendor_driver_state();
        return false;
    }
    s_tca8418_bus = std::make_shared<cpp_bus_driver::HardwareI2c1>(s_xl9555_bus);
    s_xl9555 = std::make_unique<cpp_bus_driver::Xl95x5>(s_xl9555_bus, keyboard.xl9555);
    s_tca8418 = std::make_unique<cpp_bus_driver::Tca8418>(s_tca8418_bus, keyboard.tca8418);

    // The device protocol is not reimplemented here. HardwareI2c1, Xl95x5,
    // Tca8418::Init(), matrix setup, FIFO decoding, and IRQ clearing are all
    // LilyGO cpp_bus_driver calls. This file only binds that vendor driver to
    // P4 LDO/reset wiring and Trail's event route.
    if (!s_xl9555->Init())
    {
        ESP_LOGI(kTag, "P4 keyboard not attached: LilyGO XL9555 0x%02X did not ACK",
                 static_cast<unsigned>(keyboard.xl9555));
        reset_vendor_driver_state();
        return false;
    }
    if (!reset_tca8418_via_xl9555())
    {
        ESP_LOGW(kTag, "P4 keyboard LilyGO XL9555 reset sequence failed");
        reset_vendor_driver_state();
        return false;
    }
    if (!s_tca8418->Init())
    {
        ESP_LOGW(kTag, "P4 keyboard LilyGO TCA8418 0x%02X did not ACK after reset",
                 static_cast<unsigned>(keyboard.tca8418));
        reset_vendor_driver_state();
        return false;
    }
    if (!s_tca8418->SetKeypadScanWindow(0, 0, keyboard.columns, keyboard.rows) ||
        !s_tca8418->SetInterruptEnable(cpp_bus_driver::Tca8418::IrqMask::kKeyEvents) ||
        !s_tca8418->ClearIrqFlag(cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents))
    {
        ESP_LOGW(kTag, "P4 keyboard LilyGO TCA8418 scan setup failed");
        reset_vendor_driver_state();
        return false;
    }

    s_caps_lock = false;
    s_shift = false;
    s_fn = false;
    s_physical_down.fill(false);
    s_event_queue_head = 0;
    s_event_queue_count = 0;
    s_next_i2c_attempt_ms = 0;
    s_last_i2c_failure_log_ms = 0;
    s_last_alt_press_ms = 0;
    s_i2c_failures = 0;
    board.setKeyboardReady(true);
    // Board startup owns the independent GPIO47 backlight. Input detection
    // must neither gate that output nor reset a brightness already selected.
    ESP_LOGI(kTag,
             "P4 keyboard ready P2=(sda=%d,scl=%d,int=%d,backlight=%d) XL9555=0x%02X TCA8418=0x%02X matrix=%dx%d poll=%lums",
             keyboard.sda,
             keyboard.scl,
             keyboard.interrupt,
             keyboard.backlight,
             static_cast<unsigned>(keyboard.xl9555),
             static_cast<unsigned>(keyboard.tca8418),
             keyboard.columns,
             keyboard.rows,
             static_cast<unsigned long>(kPollIntervalMs));
    return true;
}

uint32_t translate_key(uint8_t key_number)
{
    if (key_number == 0 || key_number > kKeyMap.size())
    {
        return 0;
    }

    const uint32_t base = kKeyMap[key_number - 1U];
    switch (base)
    {
    case kKeyCaps:
        s_caps_lock = !s_caps_lock;
        return 0;
    case kKeyShift:
        s_shift = true;
        return 0;
    case kKeyFn:
        s_fn = true;
        return 0;
    case kKeyWin:
    {
        auto& board = boards::t_display_p4::TDisplayP4Board::instance();
        const uint8_t current = board.keyboardGetBrightness();
        const uint8_t next = current == 0 ? DEVICE_MAX_BRIGHTNESS_LEVEL : 0;
        board.keyboardSetBrightness(next);
        return 0;
    }
    case kKeyAlt:
    {
        const uint32_t now = lv_tick_get();
        if (s_last_alt_press_ms != 0 &&
            static_cast<uint32_t>(now - s_last_alt_press_ms) <= kAltDoublePressMs)
        {
            ui_take_screenshot_to_sd();
            s_last_alt_press_ms = 0;
            return 0;
        }
        s_last_alt_press_ms = now;
        return 0;
    }
    case kKeyCtrl:
    case kKeyRecord:
        return 0;
    case kKeyF11:
        return 0x11000BU;
    default:
        break;
    }

    if (base >= 0x81 && base <= 0x8A)
    {
        // Shared page handlers use the same out-of-Unicode F-key namespace
        // as the Linux input adapters (the map's Help action expects F1).
        return 0x110001U + (base - 0x81U);
    }
    if (base == LV_KEY_NEXT && s_shift) return LV_KEY_PREV;

    if (s_fn)
    {
        const uint32_t shifted = kShiftKeyMap[key_number - 1U];
        return shifted >= 0x20 && shifted <= 0x7E ? shifted : base;
    }

    if (base >= 'a' && base <= 'z')
    {
        const bool upper = s_shift != s_caps_lock;
        return upper ? base - 'a' + 'A' : base;
    }

    if (s_shift)
    {
        const uint32_t shifted = kShiftKeyMap[key_number - 1U];
        if (shifted >= 0x20 && shifted <= 0x7E)
        {
            return shifted;
        }
    }
    return base;
}

void enqueue_key(KeyEvent event)
{
    if (s_event_queue_count == s_event_queue.size())
    {
        return;
    }

    const size_t tail = (s_event_queue_head + s_event_queue_count) % s_event_queue.size();
    s_event_queue[tail] = event;
    ++s_event_queue_count;
}

KeyEvent dequeue_key()
{
    if (s_event_queue_count == 0)
    {
        return {};
    }

    const KeyEvent key = s_event_queue[s_event_queue_head];
    s_event_queue_head = (s_event_queue_head + 1U) % s_event_queue.size();
    --s_event_queue_count;
    return key;
}

bool key_is_text_input(uint32_t key)
{
    return (key >= 0x20U && key <= 0x7EU) ||
           key == LV_KEY_BACKSPACE ||
           key == LV_KEY_ENTER ||
           key == LV_KEY_LEFT ||
           key == LV_KEY_RIGHT ||
           key == LV_KEY_UP ||
           key == LV_KEY_DOWN ||
           key == LV_KEY_HOME ||
           key == LV_KEY_END ||
           key == LV_KEY_ESC;
}

lv_obj_t* find_focused_textarea(lv_obj_t* root)
{
    if (root == nullptr || !lv_obj_is_valid(root) || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN))
    {
        return nullptr;
    }
    if (lv_obj_check_type(root, &lv_textarea_class) &&
        lv_obj_has_state(root, LV_STATE_FOCUSED))
    {
        return root;
    }

    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t index = 0; index < count; ++index)
    {
        if (lv_obj_t* textarea = find_focused_textarea(lv_obj_get_child(root, index)))
        {
            return textarea;
        }
    }
    return nullptr;
}

struct TextareaSearch
{
    lv_obj_t* only = nullptr;
    uint32_t count = 0;
};

void find_visible_textareas(lv_obj_t* root, TextareaSearch* result)
{
    if (root == nullptr || result == nullptr || !lv_obj_is_valid(root) ||
        lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN) || result->count > 1)
    {
        return;
    }
    if (lv_obj_check_type(root, &lv_textarea_class))
    {
        result->only = root;
        ++result->count;
        return;
    }

    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t index = 0; index < count; ++index)
    {
        find_visible_textareas(lv_obj_get_child(root, index), result);
    }
}

bool route_to_textarea(uint32_t key)
{
    if (!key_is_text_input(key))
    {
        return false;
    }

    lv_obj_t* textarea = find_focused_textarea(lv_screen_active());
    if (textarea == nullptr && key >= 0x20U && key <= 0x7EU)
    {
        TextareaSearch search{};
        find_visible_textareas(lv_screen_active(), &search);
        if (search.count == 1)
        {
            textarea = search.only;
            lv_obj_add_state(textarea, LV_STATE_FOCUSED);
            lv_obj_send_event(textarea, LV_EVENT_FOCUSED, nullptr);
        }
    }
    if (textarea == nullptr)
    {
        return false;
    }

    if (key == LV_KEY_ESC)
    {
        return lv_obj_send_event(textarea, LV_EVENT_CANCEL, nullptr) == LV_RESULT_OK;
    }
    uint32_t parameter = key;
    return lv_obj_send_event(textarea, LV_EVENT_KEY, &parameter) == LV_RESULT_OK;
}

lv_group_t* active_key_group()
{
    // Pages temporarily install modal groups. Respect that owner before the
    // app/menu fallback so keys never activate controls behind a dialog.
    lv_group_t* candidates[] = {lv_group_get_default(), ui_get_active_app() != nullptr ? app_g : menu_g};
    for (auto* group : candidates)
    {
        lv_obj_t* focus = group != nullptr ? lv_group_get_focused(group) : nullptr;
        if (focus == nullptr || !lv_obj_is_valid(focus)) continue;
        lv_obj_t* root = focus;
        bool hidden = false;
        while (root != nullptr)
        {
            hidden = hidden || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
            if (lv_obj_get_parent(root) == nullptr) break;
            root = lv_obj_get_parent(root);
        }
        if (!hidden && (root == lv_screen_active() || root == lv_layer_top())) return group;
    }
    return nullptr;
}

bool route_to_group(uint32_t key)
{
    lv_group_t* group = active_key_group();
    if (group == nullptr) return false;
    (void)key;
    lv_indev_set_group(s_keypad, group);
    return true;
}

bool has_key_handler(lv_obj_t* object)
{
    if (object == nullptr || !lv_obj_is_valid(object)) return false;
    for (uint32_t index = 0; index < lv_obj_get_event_count(object); ++index)
    {
        const auto filter = lv_obj_get_event_dsc(object, index)->filter & ~LV_EVENT_PREPROCESS;
        if (filter == LV_EVENT_KEY) return true;
    }
    return false;
}

bool route_to_screen(uint32_t key)
{
    if (!has_key_handler(s_route_page))
    {
        return false;
    }

    uint32_t parameter = key;
    (void)lv_obj_send_event(s_route_page, LV_EVENT_KEY, &parameter);
    return true;
}

void route_key(uint32_t key)
{
    if (key == 0)
    {
        return;
    }

    lv_display_trigger_activity(nullptr);
    trail_mate_idf_note_user_activity();

    // The menu has established physical-key shortcuts. Keep them P4-local at
    // the adapter boundary and only while the menu is actually active.
    if (ui_get_active_app() == nullptr && key <= 0x7F)
    {
        const char character = static_cast<char>(key);
        if (ui::menu_runtime::handleWalkieKey(character, 1) ||
            ui::menu_runtime::handleShortcutKey(character, 1))
        {
            s_pressed_consumed = true;
            return;
        }
    }

    // Pager/Deck pages own navigation depth: ESC first closes a dialog or
    // returns from compose/conversation before the application itself exits.
    if (key == LV_KEY_ESC)
    {
        auto* group = active_key_group();
        auto* focus = group != nullptr ? lv_group_get_focused(group) : nullptr;
        if (has_key_handler(focus))
        {
            s_pressed_consumed = true;
            uint32_t parameter = key;
            (void)lv_obj_send_event(focus, LV_EVENT_KEY, &parameter);
            return;
        }
        if (route_to_screen(key))
        {
            s_pressed_consumed = true;
            return;
        }
    }

    if (route_to_textarea(key)) return;

    // Pages without a back handler still have an unambiguous app-exit key.
    if (key == LV_KEY_ESC && ui_get_active_app() != nullptr)
    {
        s_pressed_consumed = true;
        ui_request_exit_to_menu();
        return;
    }

    (void)route_to_screen(key);
}

void route_proxy_cb(lv_event_t* event)
{
    if (!s_pressed_consumed) route_key(lv_event_get_key(event));
}

void reset_pressed_key()
{
    if (s_ptt_number != 0)
    {
        platform::ui::walkie::set_ptt(false);
        s_ptt_number = 0;
        s_ptt_screen = nullptr;
    }
    if (s_keypad != nullptr)
    {
        lv_indev_reset(s_keypad, nullptr);
        lv_indev_wait_release(s_keypad);
        s_release_pending = true;
    }
    if (s_pressed_focus != nullptr && lv_obj_is_valid(s_pressed_focus))
    {
        lv_obj_remove_state(s_pressed_focus, LV_STATE_PRESSED);
    }
    s_pressed_number = 0;
    s_pressed_key = 0;
    s_pressed_screen = nullptr;
    s_pressed_focus = nullptr;
    s_pressed_consumed = false;
    s_route_page = nullptr;
}

lv_obj_t* find_page_key_target(lv_obj_t* root)
{
    if (root == nullptr || !lv_obj_is_valid(root) || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    // Page roots are plain containers with explicit KEY handlers. Do not pick
    // arbitrary buttons or textareas, and do not steal a grouped input target.
    if (lv_obj_check_type(root, &lv_obj_class) && lv_obj_get_group(root) == nullptr && has_key_handler(root)) return root;
    for (uint32_t index = lv_obj_get_child_count(root); index > 0; --index)
    {
        if (auto* target = find_page_key_target(lv_obj_get_child(root, index - 1))) return target;
    }
    return nullptr;
}

void select_key_route(uint32_t key)
{
    s_route_page = find_page_key_target(lv_screen_active());
    lv_indev_set_group(s_keypad, s_route_group);
    auto* active_group = active_key_group();
    if (active_group != nullptr && active_group != app_g && active_group != menu_g && key != LV_KEY_ESC)
    {
        (void)route_to_group(key);
        return;
    }
    // Explicit text and menu shortcuts retain their Trail semantics through a
    // proxy. All other controls use LVGL's real keypad state machine, including
    // Enter click/release, Tab focus movement and long-press repetition.
    if ((key_is_text_input(key) && find_focused_textarea(lv_screen_active()) != nullptr) ||
        (key == LV_KEY_ESC && ui_get_active_app() != nullptr)) return;
    if (key >= 0x20U && key <= 0x7EU)
    {
        TextareaSearch search{};
        find_visible_textareas(lv_screen_active(), &search);
        if (search.count == 1 || ui_get_active_app() == nullptr) return;
    }
    lv_group_t* group = active_key_group();
    lv_obj_t* focus = group != nullptr ? lv_group_get_focused(group) : nullptr;
    // A root KEY listener owns ordinary keys when the focused control has no
    // KEY listener. Activation and focus traversal still belong to the group.
    if (key != LV_KEY_ENTER && key != LV_KEY_NEXT && key != LV_KEY_PREV &&
        s_route_page != nullptr && !has_key_handler(focus)) return;
    (void)route_to_group(key);
}

void keypad_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    data->continue_reading = false;
    data->key = s_pressed_key;
    data->state = LV_INDEV_STATE_RELEASED;
    if (s_release_pending)
    {
        s_release_pending = false;
        return;
    }
    if (!s_ready) return;

    if (s_ptt_number != 0 &&
        (!platform::ui::walkie::is_active() || s_ptt_screen != lv_screen_active()))
    {
        reset_pressed_key();
        s_release_pending = false;
        return;
    }

    if (s_pressed_number != 0 &&
        (s_pressed_screen != lv_screen_active() ||
         (s_route_page != nullptr && s_route_page != find_page_key_target(lv_screen_active())) ||
         (s_pressed_key != LV_KEY_NEXT && s_pressed_key != LV_KEY_PREV &&
          s_pressed_focus != lv_group_get_focused(lv_indev_get_group(indev)))))
    {
        // Do not deliver a release/click to a replacement page or focus target.
        reset_pressed_key();
        s_release_pending = false;
        return;
    }

    if (s_event_queue_count != 0)
    {
        const KeyEvent next = s_event_queue[s_event_queue_head];
        const bool modifier = next.number > 0 && next.number <= kKeyMap.size() &&
                              (kKeyMap[next.number - 1U] == kKeyShift || kKeyMap[next.number - 1U] == kKeyFn);
        if (next.pressed && s_pressed_number != 0 && !modifier && next.number != s_pressed_number)
        {
            // LVGL keypad is a single-key device. Release the previous key on
            // its own turn before accepting a second key; retain the FIFO item.
            s_pressed_number = 0;
            return;
        }
        const KeyEvent event = dequeue_key();
        const bool valid = event.number > 0 && event.number <= s_physical_down.size();
        const bool duplicate = valid && s_physical_down[event.number - 1U] == event.pressed;
        if (valid) s_physical_down[event.number - 1U] = event.pressed;
        s_shift = s_physical_down[52];
        s_fn = s_physical_down[50] || s_physical_down[57];
        // Walkie's PTT needs both physical edges, independent of LVGL's
        // single-key repeat/click processing. Do not also activate Monitor.
        if (valid && kKeyMap[event.number - 1U] == ' ' &&
            (s_ptt_number != 0 || platform::ui::walkie::is_active()))
        {
            if (event.pressed && !duplicate && s_ptt_number == 0)
            {
                s_ptt_number = event.number;
                s_ptt_screen = lv_screen_active();
                platform::ui::walkie::set_ptt(true);
                lv_display_trigger_activity(nullptr);
                trail_mate_idf_note_user_activity();
            }
            else if (!event.pressed && s_ptt_number == event.number)
            {
                platform::ui::walkie::set_ptt(false);
                s_ptt_number = 0;
                s_ptt_screen = nullptr;
            }
            return;
        }
        if (event.pressed && !duplicate)
        {
            const uint32_t key = translate_key(event.number);
            if (key != 0)
            {
                s_pressed_number = event.number;
                s_pressed_key = key;
                select_key_route(key);
                s_pressed_screen = lv_screen_active();
                s_pressed_focus = lv_group_get_focused(lv_indev_get_group(indev));
                s_pressed_consumed = false;
                lv_display_trigger_activity(nullptr);
                trail_mate_idf_note_user_activity();
            }
        }
        else if (!event.pressed && event.number == s_pressed_number)
        {
            s_pressed_number = 0;
        }
    }
    data->key = s_pressed_key;
    data->state = s_pressed_number != 0 && !s_pressed_consumed
                      ? LV_INDEV_STATE_PRESSED
                      : LV_INDEV_STATE_RELEASED;
}

void note_i2c_failure(const char* operation)
{
    const uint32_t now = lv_tick_get();
    s_next_i2c_attempt_ms = now + kI2cFailureBackoffMs;
    if (++s_i2c_failures >= 3)
    {
        s_ready = false;
        boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
        reset_pressed_key();
        s_event_queue_count = 0;
        s_event_queue_head = 0;
        reset_vendor_driver_state();
        s_next_attach_ms = now + kAttachRetryMs;
    }
    if (s_last_i2c_failure_log_ms == 0 ||
        static_cast<uint32_t>(now - s_last_i2c_failure_log_ms) >= kI2cFailureLogIntervalMs)
    {
        s_last_i2c_failure_log_ms = now;
        ESP_LOGW(kTag, "P4 keyboard %s failed; retrying after %lums", operation,
                 static_cast<unsigned long>(kI2cFailureBackoffMs));
    }
}

void drain_fifo()
{
    const uint32_t now = lv_tick_get();
    if (static_cast<int32_t>(now - s_next_i2c_attempt_ms) < 0)
    {
        return;
    }
    // Leave hardware events in the controller until there is space for a full
    // FIFO. Dropping a release here would leave LVGL repeating a stuck key.
    if (s_event_queue.size() - s_event_queue_count < kTca8418FifoDepth) return;

    if (s_tca8418 == nullptr)
    {
        note_i2c_failure("LilyGO TCA8418 driver state");
        return;
    }

    // Do not read TCA8418 registers in Trail. The vendor driver's API owns
    // FIFO count, event decoding, and interrupt acknowledgement semantics.
    const uint8_t irq_flags = s_tca8418->GetIrqFlag();
    cpp_bus_driver::Tca8418::IrqStatus irq_status{};
    if (!s_tca8418->ParseIrqStatus(irq_flags, irq_status))
    {
        note_i2c_failure("LilyGO TCA8418 IRQ status");
        return;
    }
    if (irq_status.fifo_overflow_flag)
    {
        // An overflow can lose a release edge. Discard that incomplete chord
        // instead of leaving a key repeating forever.
        reset_pressed_key();
        s_event_queue_head = s_event_queue_count = 0;
        s_shift = s_fn = false;
        s_physical_down.fill(false);
        if (!s_tca8418->ClearEventFifo() || !s_tca8418->ClearIrqFlag(irq_flags))
        {
            note_i2c_failure("LilyGO TCA8418 FIFO overflow recovery");
        }
        ESP_LOGW(kTag, "P4 keyboard FIFO overflow; cancelled incomplete key state");
        return;
    }
    const uint8_t reported_count = s_tca8418->GetFingerCount();
    if (reported_count == 0xFFU)
    {
        note_i2c_failure("LilyGO TCA8418 FIFO count");
        return;
    }

    if (reported_count == 0)
    {
        s_i2c_failures = 0;
        return;
    }
    if (reported_count > kTca8418FifoDepth)
    {
        note_i2c_failure("LilyGO TCA8418 invalid FIFO count");
        return;
    }

    for (uint8_t index = 0; index < reported_count; ++index)
    {
        cpp_bus_driver::Tca8418::TouchInfo event{};
        if (!s_tca8418->ReadKeyEvent(&event))
        {
            note_i2c_failure("LilyGO TCA8418 event FIFO transfer");
            return;
        }

        if (event.event_type != cpp_bus_driver::Tca8418::EventType::kKeypad)
        {
            continue;
        }
        enqueue_key({event.num, event.press_flag});
    }

    if (!s_tca8418->ClearIrqFlag(cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents))
    {
        note_i2c_failure("LilyGO TCA8418 IRQ acknowledgement");
    }
    else
    {
        s_i2c_failures = 0;
    }
}

void poll_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!s_ready)
    {
        if (static_cast<int32_t>(lv_tick_get() - s_next_attach_ms) >= 0)
        {
            (void)trail_mate_t_display_p4_keyboard_initialize();
        }
        return;
    }
    drain_fifo();
}

} // namespace

extern "C" bool trail_mate_t_display_p4_keyboard_initialize(void)
{
    if (s_ready)
    {
        return true;
    }

    if (!boards::t_display_p4::TDisplayP4Board::profile().supports_keyboard_module)
    {
        return false;
    }

    if (static_cast<int32_t>(lv_tick_get() - s_next_attach_ms) < 0)
    {
        return false;
    }

    boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
    s_ready = initialize_controller();
    if (!s_ready) s_next_attach_ms = lv_tick_get() + kAttachRetryMs;
    return s_ready;
}

extern "C" bool trail_mate_t_display_p4_keyboard_start(void)
{
    if (s_poll_timer != nullptr)
    {
        return s_ready;
    }

    if (!boards::t_display_p4::TDisplayP4Board::profile().supports_keyboard_module)
    {
        return false;
    }

    // Keep the UI adapter alive when an optional keyboard is absent. Recovery
    // and FIFO access share the LVGL task, so no vendor driver is used concurrently.
    (void)trail_mate_t_display_p4_keyboard_initialize();
    s_route_group = lv_group_create();
    s_route_proxy = lv_obj_create(lv_layer_sys());
    s_keypad = lv_indev_create();
    if (s_route_group != nullptr && s_route_proxy != nullptr && s_keypad != nullptr)
    {
        lv_obj_add_flag(s_route_proxy, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(s_route_proxy, route_proxy_cb, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(s_route_group, s_route_proxy);
        lv_group_focus_obj(s_route_proxy);
        lv_indev_set_type(s_keypad, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(s_keypad, keypad_read_cb);
        lv_indev_set_group(s_keypad, s_route_group);
        lv_timer_set_period(lv_indev_get_read_timer(s_keypad), kPollIntervalMs);
        s_poll_timer = lv_timer_create(poll_timer_cb, kPollIntervalMs, nullptr);
    }
    if (s_poll_timer == nullptr)
    {
        boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
        s_ready = false;
        if (s_keypad != nullptr) lv_indev_delete(s_keypad);
        if (s_route_proxy != nullptr) lv_obj_delete(s_route_proxy);
        if (s_route_group != nullptr) lv_group_delete(s_route_group);
        s_keypad = nullptr;
        s_route_proxy = nullptr;
        s_route_group = nullptr;
        reset_vendor_driver_state();
        ESP_LOGW(kTag, "P4 keyboard could not create the LVGL polling timer");
        return false;
    }
    return s_ready;
}

extern "C" bool trail_mate_t_display_p4_keyboard_is_ready(void)
{
    return s_ready;
}
