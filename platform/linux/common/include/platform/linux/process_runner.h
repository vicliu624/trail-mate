#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
struct ProcessResult
{
    int exit_code = -1;
    bool timed_out = false;
    std::string output;
    bool ok() const { return exit_code == 0 && !timed_out; }
};

// Executes argv directly, without shell expansion. Output is bounded, locale is
// stable, pipe descriptors are closed, and a timed-out child is reaped.
ProcessResult run_process(const std::vector<std::string>& arguments,
                          std::chrono::milliseconds timeout = std::chrono::seconds(5),
                          const std::atomic_bool* cancel = nullptr);
} // namespace platform::linux_runtime
