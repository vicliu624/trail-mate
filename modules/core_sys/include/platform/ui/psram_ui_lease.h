#pragma once
#include <atomic>

namespace platform::ui
{
// LVGL allocations, including later layout, label and keyboard work, belong
// to PSRAM while a memory-heavy page is open. Nested map/editor visits compose.
inline std::atomic<unsigned> psram_ui_leases{0};
inline bool psramUiRequired() { return psram_ui_leases.load(std::memory_order_relaxed) != 0; }
class PsramUiLease
{
  public:
    PsramUiLease() { psram_ui_leases.fetch_add(1, std::memory_order_relaxed); }
    ~PsramUiLease() { psram_ui_leases.fetch_sub(1, std::memory_order_relaxed); }
    PsramUiLease(const PsramUiLease&) = delete;
    PsramUiLease& operator=(const PsramUiLease&) = delete;
};
} // namespace platform::ui
