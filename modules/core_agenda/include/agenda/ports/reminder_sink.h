#pragma once

#include <cstdint>

namespace agenda
{
struct Reminder
{
    int64_t occurrence_start = 0;
    int64_t trigger_time = 0;
    uint32_t event_id = 0;
    bool valid = false;
};

class IReminderSink
{
  public:
    virtual ~IReminderSink() = default;
    // The sink may defer while another modal owns input. Never call scheduler
    // actions reentrantly from present(). No LVGL type crosses this port.
    virtual bool present(const Reminder& reminder) = 0;
    virtual void withdraw() = 0;
};
} // namespace agenda
