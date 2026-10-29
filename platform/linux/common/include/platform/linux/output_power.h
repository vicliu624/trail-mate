#pragma once

#include "platform/linux/process_runner.h"
#include <functional>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
struct OutputPowerOptions
{
    // Empty selects the sole compositor output. Multiple outputs require an
    // explicit selection; output names belong to deployment configuration.
    std::string output;
};
struct OutputPowerSnapshot
{
    bool available = false;
    bool selection_required = false;
    bool failed = false;
    bool powered = false;
    std::string output;
};

// All compositor I/O runs on an owned worker, including shutdown restoration.
// Construct only in a composition that explicitly enables Wayland power control.
class OutputPowerRuntime
{
  public:
    using Runner = std::function<ProcessResult(const std::vector<std::string>&,
                                               const std::atomic_bool*)>;
    explicit OutputPowerRuntime(OutputPowerOptions options = {}, Runner runner = {});
    ~OutputPowerRuntime();
    OutputPowerRuntime(const OutputPowerRuntime&) = delete;
    OutputPowerRuntime& operator=(const OutputPowerRuntime&) = delete;
    OutputPowerSnapshot snapshot() const;
    // Binds the request to the currently selected output. Missing or ambiguous
    // outputs reject requests rather than queueing a command for future hardware.
    bool request(bool powered);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
