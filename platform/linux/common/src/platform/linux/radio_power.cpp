#include "platform/linux/radio_power.h"

namespace platform::linux_runtime
{
void RadioPowerRequests::activate(bool available, bool enabled, bool ready)
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = true;
    pending_ = inflight_ = false;
    state_.available = available;
    state_.enabled = enabled;
    state_.ready = ready;
    state_.changing = state_.failed = false;
    state_.error.clear();
}
void RadioPowerRequests::refresh(bool available, bool enabled, bool ready)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.available = active_ && available;
    state_.enabled = enabled;
    state_.ready = ready;
}
bool RadioPowerRequests::request(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !state_.available) return false;
    if (state_.changing && target_ == enabled) return true;
    if (++state_.generation == 0) ++state_.generation;
    target_ = enabled;
    pending_ = true;
    state_.changing = true;
    state_.failed = false;
    state_.error.clear();
    return true;
}
bool RadioPowerRequests::take(RadioPowerWork& work)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !pending_ || inflight_) return false;
    work = {state_.generation, target_};
    pending_ = false;
    inflight_ = true;
    return true;
}
bool RadioPowerRequests::current(uint64_t generation) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.generation == generation;
}
void RadioPowerRequests::complete(const RadioPowerWork& work, bool enabled, bool ready, bool success, const std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_ = false;
    state_.enabled = enabled;
    state_.ready = ready;
    if (work.generation != state_.generation) return;
    state_.changing = false;
    state_.failed = !success;
    state_.error = success ? "" : error;
}
void RadioPowerRequests::deactivate()
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = pending_ = false;
    state_.available = state_.changing = state_.ready = false;
}
RadioPowerSnapshot RadioPowerRequests::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
namespace
{
struct Registry
{
    std::mutex mutex;
    std::weak_ptr<RadioPowerRequests> owner;
};
Registry& registry()
{
    static Registry value;
    return value;
}
std::shared_ptr<RadioPowerRequests> owner()
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.owner.lock();
}
} // namespace
void bind_radio_power(const std::shared_ptr<RadioPowerRequests>& value)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.owner = value;
}
void unbind_radio_power(const std::shared_ptr<RadioPowerRequests>& value)
{
    auto& state = registry();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.owner.lock() == value) state.owner.reset();
}
bool request_radio_enabled(bool enabled)
{
    const auto value = owner();
    return value && value->request(enabled);
}
RadioPowerSnapshot radio_power_snapshot()
{
    const auto value = owner();
    return value ? value->snapshot() : RadioPowerSnapshot{};
}
} // namespace platform::linux_runtime
