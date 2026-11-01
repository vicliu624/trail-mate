// Only platform services/data are substituted. No LVGL page layout lives here.
#include "platform/linux/output_power.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/settings_store.h"
#include "ui/widgets/foreground_operation_overlay.h"
#include <string>

namespace platform::linux_runtime
{
// The browser fixture owns no compositor output or device power controller.
OutputPowerSnapshot OutputPowerRuntime::snapshot() const { return {}; }
bool OutputPowerRuntime::request(bool) { return false; }
} // namespace platform::linux_runtime

namespace platform::ui::device
{
BatteryInfo battery_info() { return {true, false, 85}; }
void handle_low_battery(const BatteryInfo&) {}
} // namespace platform::ui::device
namespace ui::widgets::foreground_operation
{
Snapshot make_snapshot(Slot slot, Policy policy, Priority priority, const char*, const char*, int, const char*, uint32_t)
{
    Snapshot s;
    s.slot = slot;
    s.policy = policy;
    s.priority = priority;
    return s;
}
void publish(const Snapshot&) {}
void clear(Slot, uint32_t) {}
} // namespace ui::widgets::foreground_operation
