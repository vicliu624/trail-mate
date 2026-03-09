#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::screen
{

struct Hooks
{
    bool (*format_time)(char* out, std::size_t out_len) = nullptr;
    int (*read_unread_count)() = nullptr;
    void (*show_main_menu)() = nullptr;
    void (*on_wake_from_sleep)() = nullptr;
};

uint32_t clamp_timeout_ms(uint32_t timeout_ms);
uint32_t timeout_ms();
uint16_t timeout_secs();
void set_timeout_ms(uint32_t timeout_ms);
void init(const Hooks& hooks);
bool is_sleeping();
bool is_sleep_disabled();
bool is_saver_active();
void wake_saver();
void enter_from_saver();
void update_user_activity();
void disable_sleep();
void enable_sleep();

} // namespace platform::ui::screen
