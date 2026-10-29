#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace platform::linux_runtime
{
struct RadioPowerSnapshot
{
    bool available = false, enabled = false, ready = false;
    bool changing = false, failed = false;
    uint64_t generation = 0;
    std::string error;
};
struct RadioPowerWork
{
    uint64_t generation = 0;
    bool enabled = false;
};
// Latest-intent mailbox shared by UI and the existing radio owner. Contains no
// controller pointer and remains safe to read after its owner has stopped.
class RadioPowerRequests
{
  public:
    void activate(bool available, bool enabled, bool ready);
    void refresh(bool available, bool enabled, bool ready);
    bool request(bool enabled);
    bool take(RadioPowerWork& work);
    bool current(uint64_t generation) const;
    void complete(const RadioPowerWork& work, bool enabled, bool ready, bool success, const std::string& error);
    void deactivate();
    RadioPowerSnapshot snapshot() const;

  private:
    mutable std::mutex mutex_;
    RadioPowerSnapshot state_;
    bool active_ = false, pending_ = false, inflight_ = false, target_ = false;
};
void bind_radio_power(const std::shared_ptr<RadioPowerRequests>& owner);
void unbind_radio_power(const std::shared_ptr<RadioPowerRequests>& owner);
bool request_radio_enabled(bool enabled);
RadioPowerSnapshot radio_power_snapshot();
} // namespace platform::linux_runtime
