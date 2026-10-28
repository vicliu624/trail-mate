#include "vendor.h"
#include <cassert>
#include <cstring>
#include <iostream>

// Exercise the production adapter with real LVGL and fake hardware boundaries.
#include "../../platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_keyboard.cpp"

namespace
{
int clicks = 0;
int presses = 0;
int releases = 0;
int page_keys = 0;
uint32_t last_page_key = 0;

void count_event(lv_event_t* event)
{
    switch (lv_event_get_code(event))
    {
    case LV_EVENT_CLICKED:
        ++clicks;
        break;
    case LV_EVENT_PRESSED:
        ++presses;
        break;
    case LV_EVENT_RELEASED:
        ++releases;
        break;
    default:
        break;
    }
}

void page_event(lv_event_t* event)
{
    ++page_keys;
    last_page_key = lv_event_get_key(event);
}

void modal_back(lv_event_t* event)
{
    if (lv_event_get_key(event) == LV_KEY_ESC) ++page_keys;
}

void read()
{
    lv_tick_inc(30);
    lv_indev_read(s_keypad);
}

void edge(uint8_t number, bool pressed)
{
    cpp_bus_driver::Tca8418::fifo.push_back({number, pressed});
    drain_fifo();
    read();
}

void tap(uint8_t number)
{
    edge(number, true);
    edge(number, false);
}

void fresh_page(bool app)
{
    reset_pressed_key();
    lv_indev_set_group(s_keypad, s_route_group);
    s_event_queue_head = s_event_queue_count = 0;
    cpp_bus_driver::Tca8418::fifo.clear();
    lv_group_remove_all_objs(menu_g);
    lv_group_remove_all_objs(app_g);
    lv_obj_clean(lv_screen_active());
    fake::app_active = app;
    lv_group_set_default(app ? app_g : menu_g);
    clicks = presses = releases = page_keys = 0;
    last_page_key = 0;
    read();
}

lv_obj_t* button(lv_group_t* group)
{
    auto* object = lv_button_create(lv_screen_active());
    lv_obj_add_event_cb(object, count_event, LV_EVENT_ALL, nullptr);
    lv_group_add_obj(group, object);
    return object;
}
} // namespace

int main()
{
    lv_init();
    auto* display = lv_display_create(540, 1168);
    assert(display != nullptr);
    menu_g = lv_group_create();
    app_g = lv_group_create();

    // An absent keyboard still installs the adapter and retries only after the
    // cooldown. It becomes ready without a UI restart when hardware appears.
    fake::attached = false;
    assert(!trail_mate_t_display_p4_keyboard_initialize());
    assert(fake::probes == 1);
    assert((fake::delays == std::vector<unsigned>{pdMS_TO_TICKS(kPowerSettleMs)}));
    assert(!trail_mate_t_display_p4_keyboard_start());
    assert(s_keypad != nullptr && s_poll_timer != nullptr);
    lv_timer_pause(s_poll_timer);
    lv_timer_pause(lv_indev_get_read_timer(s_keypad));
    poll_timer_cb(nullptr);
    assert(fake::probes == 1);
    fake::attached = true;
    fake::delays.clear();
    lv_tick_inc(kAttachRetryMs);
    poll_timer_cb(nullptr);
    assert(trail_mate_t_display_p4_keyboard_is_ready());
    assert(fake::probes == 2);
    assert((fake::delays == std::vector<unsigned>{pdMS_TO_TICKS(kResetDelayMs), pdMS_TO_TICKS(kResetDelayMs)}));
    assert((fake::reset_steps == std::vector<int>{0, 2, 1}));
    assert(trail_mate_t_display_p4_keyboard_start());

    // A quick press+release read in one hardware FIFO must reach LVGL as two
    // separate samples, producing exactly one native button click.
    fresh_page(false);
    auto* first = button(menu_g);
    cpp_bus_driver::Tca8418::fifo = {{64, true}, {64, false}};
    drain_fifo();
    read();
    assert(presses == 1 && clicks == 0);
    read();
    assert(releases == 1 && clicks == 1);
    auto* second = button(menu_g);
    lv_group_focus_obj(first);
    tap(54); // Tab / LV_KEY_NEXT
    assert(lv_group_get_focused(menu_g) == second);
    tap(64);
    assert(clicks == 2);
    edge(53, true);
    tap(54); // Shift+Tab traverses backward.
    edge(53, false);
    assert(lv_group_get_focused(menu_g) == first);

    // Releasing an unrelated key does not release the held key. Native LVGL
    // repetition remains available while a text key is held.
    fresh_page(true);
    auto* textarea = lv_textarea_create(lv_screen_active());
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    edge(21, true); // q
    assert(std::strcmp(lv_textarea_get_text(textarea), "q") == 0);
    edge(49, false); // unrelated Ctrl release
    assert(s_pressed_number == 21);
    lv_tick_inc(600);
    read();
    lv_tick_inc(200);
    read();
    assert(std::strlen(lv_textarea_get_text(textarea)) >= 2);
    edge(21, false);
    auto length = std::strlen(lv_textarea_get_text(textarea));
    lv_tick_inc(1000);
    read();
    assert(std::strlen(lv_textarea_get_text(textarea)) == length);

    // Modifiers follow physical edges: hold Shift across several letters,
    // release Fn without typing, and ignore a duplicated hardware press.
    fresh_page(true);
    textarea = lv_textarea_create(lv_screen_active());
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    edge(53, true); // Shift
    tap(21);
    tap(22);
    edge(53, false);
    tap(21);
    assert(std::strcmp(lv_textarea_get_text(textarea), "QWq") == 0);
    edge(51, true); // Fn
    edge(51, false);
    tap(21);
    assert(std::strcmp(lv_textarea_get_text(textarea), "QWqq") == 0);
    edge(21, true);
    edge(21, true);
    assert(s_pressed_number == 21);
    edge(21, false);
    assert(std::strcmp(lv_textarea_get_text(textarea), "QWqqq") == 0);

    // A page root is a child of the screen. Ordinary keys must reach it even
    // when the current group focuses a control with no KEY listener.
    fresh_page(true);
    button(app_g);
    auto* page = lv_obj_create(lv_screen_active());
    lv_obj_add_event_cb(page, page_event, LV_EVENT_KEY, nullptr);
    tap(21);
    assert(page_keys == 1 && last_page_key == 'q');
    const auto exits_before_back = fake::exits;
    tap(11);
    assert(page_keys == 2 && last_page_key == LV_KEY_ESC && fake::exits == exits_before_back);
    tap(1); // Physical F1 opens map Help through the shared function-key code.
    assert(page_keys == 3 && last_page_key == 0x110001U);

    // Modal groups replace the app group while a dialog is open. Enter must
    // activate the dialog and ESC must reach its KEY back handler.
    fresh_page(true);
    button(app_g);
    auto* modal_group = lv_group_create();
    auto* modal_button = button(modal_group);
    lv_obj_set_parent(modal_button, lv_layer_top());
    lv_obj_add_event_cb(modal_button, modal_back, LV_EVENT_KEY, nullptr);
    lv_group_set_default(modal_group);
    tap(64);
    assert(clicks == 1);
    tap(11);
    assert(page_keys == 1 && fake::exits == exits_before_back);
    lv_group_set_default(app_g);
    lv_group_delete(modal_group);
    lv_obj_delete(modal_button);

    // A focused object from another screen must not swallow Enter or receive
    // a click. The current page root receives the key instead.
    fresh_page(true);
    auto* stale_screen = lv_obj_create(nullptr);
    auto* stale_button = lv_button_create(stale_screen);
    lv_obj_add_event_cb(stale_button, count_event, LV_EVENT_ALL, nullptr);
    lv_group_add_obj(app_g, stale_button);
    page = lv_obj_create(lv_screen_active());
    lv_obj_add_event_cb(page, page_event, LV_EVENT_KEY, nullptr);
    tap(64);
    assert(page_keys == 1 && last_page_key == LV_KEY_ENTER && clicks == 0);
    lv_obj_delete(stale_screen);

    // Changing focus while Enter is down must not click the replacement.
    fresh_page(true);
    first = button(app_g);
    second = button(app_g);
    lv_group_focus_obj(first);
    edge(64, true);
    lv_group_focus_obj(second);
    edge(64, false);
    assert(clicks == 0);

    // Shortcuts and ESC execute once even if the key stays held while the
    // callback switches the active application.
    fresh_page(false);
    tap(21);
    assert(fake::shortcuts == 1);
    fake::app_active = true;
    edge(11, true);
    assert(fake::exits == 1);
    lv_tick_inc(1000);
    read();
    read();
    assert(fake::exits == 1);
    edge(11, false);

    // Persistent transfer failure releases the key and restarts setup after
    // cooldown rather than remaining ready with a permanently dead device.
    fresh_page(true);
    textarea = lv_textarea_create(lv_screen_active());
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    edge(21, true);
    fake::transfer_ok = false;
    for (int i = 0; i < 3; ++i)
    {
        drain_fifo();
        lv_tick_inc(kI2cFailureBackoffMs);
    }
    assert(!trail_mate_t_display_p4_keyboard_is_ready());
    assert(s_pressed_number == 0 && s_event_queue_count == 0);
    fake::transfer_ok = true;
    lv_tick_inc(kAttachRetryMs);
    poll_timer_cb(nullptr);
    assert(trail_mate_t_display_p4_keyboard_is_ready());
    read(); // consume the cancellation release before the next physical key
    tap(21);
    assert(std::strcmp(lv_textarea_get_text(textarea), "qq") == 0);

    // Overflow can discard a release edge. Cancel the incomplete chord and
    // accept subsequent input without a stuck repeat or a firmware restart.
    edge(21, true);
    cpp_bus_driver::Tca8418::irq_flags = 8;
    drain_fifo();
    assert(s_pressed_number == 0 && s_event_queue_count == 0);
    read();
    const auto before_overflow_tap = std::strlen(lv_textarea_get_text(textarea));
    tap(21);
    assert(std::strlen(lv_textarea_get_text(textarea)) == before_overflow_tap + 1);

    // PTT is a physical hold, never a repeated LVGL click on Monitor.
    fresh_page(true);
    button(app_g);
    platform::ui::walkie::active = true;
    edge(55, true);
    assert(platform::ui::walkie::ptt && clicks == 0);
    edge(55, false);
    assert(!platform::ui::walkie::ptt && clicks == 0);
    edge(55, true);
    cpp_bus_driver::Tca8418::irq_flags = 8;
    drain_fifo();
    assert(!platform::ui::walkie::ptt);
    read();
    edge(55, true);
    assert(platform::ui::walkie::ptt);
    platform::ui::walkie::active = false;
    read();
    assert(!platform::ui::walkie::ptt);
    read();
    edge(55, false);
    platform::ui::walkie::active = true;
    edge(55, true);
    assert(platform::ui::walkie::ptt);
    fake::transfer_ok = false;
    for (int i = 0; i < 3; ++i)
    {
        drain_fifo();
        lv_tick_inc(kI2cFailureBackoffMs);
    }
    assert(!platform::ui::walkie::ptt && !s_ready);
    fake::transfer_ok = true;
    platform::ui::walkie::active = false;

    // A bus timeout is not an address NACK. The diagnostic must not scan seven
    // additional expander addresses while blocking the LVGL task on timeouts.
    fake::probe_timeout = true;
    fake::probe_addresses.clear();
    s_last_i2c_failure_log_ms = 0;
    lv_tick_inc(kAttachRetryMs);
    assert(!trail_mate_t_display_p4_keyboard_initialize());
    assert((fake::probe_addresses == std::vector<uint16_t>{0x20, 0x34}));
    fake::probe_timeout = false;
    lv_tick_inc(kAttachRetryMs);
    assert(trail_mate_t_display_p4_keyboard_initialize());
    std::cout << "P4 keyboard: native click, focus, repeat, page routes and recovery passed\n";
}
