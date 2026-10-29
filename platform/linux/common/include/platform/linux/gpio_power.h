#pragma once
#include <memory>
#include <string>

namespace platform::linux_runtime
{
struct GpioPowerOptions
{
    std::string chip;
    int line = -1;
    bool active_low = false;
    unsigned startup_ms = 0;
};
bool valid_gpio_power_options(const GpioPowerOptions&);
// Exclusive line owner. The async controller invokes these only on its worker.
class GpioPowerLine
{
  public:
    virtual ~GpioPowerLine() = default;
    virtual bool enable(const GpioPowerOptions&, int& error) = 0;
    // Drive inactive before releasing. False preserves an explicit fault state.
    virtual bool disable(int& error) = 0;
};
class AsyncGpioPower
{
  public:
    enum class State
    {
        Off,
        Starting,
        Ready,
        Stopping,
        Failed
    };
    struct Snapshot
    {
        State state = State::Off;
        int error = 0;
        bool requested = false;
    };
    explicit AsyncGpioPower(GpioPowerOptions, std::shared_ptr<GpioPowerLine> = {});
    ~AsyncGpioPower();
    void request(bool enabled);
    Snapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
