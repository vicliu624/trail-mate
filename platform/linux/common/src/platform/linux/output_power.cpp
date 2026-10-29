#include "platform/linux/output_power.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
bool parse_outputs(const ProcessResult& result, std::map<std::string, bool>& outputs)
{
    if (!result.ok() || result.output.size() > 16384) return false;
    std::istringstream lines(result.output);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty()) continue;
        const auto separator = line.rfind(' ');
        if (separator == std::string::npos || separator == 0 || separator > 255) return false;
        const auto name = line.substr(0, separator);
        if (name.find_first_of("\r\n\t\0", 0, 4) != std::string::npos) return false;
        const auto state = line.substr(separator + 1);
        if (state != "on" && state != "off") return false;
        if (outputs.size() >= 64 || !outputs.emplace(name, state == "on").second) return false;
    }
    return true;
}
OutputPowerSnapshot select_output(const OutputPowerOptions& options,
                                  const std::map<std::string, bool>& outputs)
{
    OutputPowerSnapshot next;
    if (options.output.empty() && outputs.size() > 1)
    {
        next.selection_required = true;
        return next;
    }
    const auto selected = options.output.empty() ? outputs.begin() : outputs.find(options.output);
    if (selected != outputs.end())
    {
        next.available = true;
        next.output = selected->first;
        next.powered = selected->second;
    }
    return next;
}
} // namespace

struct OutputPowerRuntime::Impl
{
    struct Request
    {
        std::string output;
        bool powered;
    };
    Impl(OutputPowerOptions value, Runner execute)
        : options(std::move(value)), runner(std::move(execute))
    {
        if (!runner)
            runner = [](const auto& arguments, const auto* cancel)
            { return run_process(arguments, std::chrono::seconds(3), cancel); };
        worker = std::thread([this]
                             { run(); });
    }
    ~Impl()
    {
        stopping = true;
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    bool read(std::map<std::string, bool>& outputs)
    {
        return parse_outputs(runner({"wlopm"}, &stopping), outputs);
    }
    void run()
    {
        std::set<std::string> restore;
        bool failed = false;
        while (!stopping)
        {
            std::optional<Request> request;
            {
                std::lock_guard<std::mutex> lock(mutex);
                request.swap(pending);
            }
            std::map<std::string, bool> outputs;
            OutputPowerSnapshot next;
            if (!read(outputs)) failed = true;
            else
            {
                next = select_output(options, outputs);
                if (request)
                {
                    failed = !next.available || request->output != next.output;
                    if (!failed)
                    {
                        // Record ownership before the command: a failing command
                        // can still have changed the physical output.
                        if (!request->powered && next.powered) restore.insert(next.output);
                        const bool submitted = next.powered == request->powered ||
                                               runner({"wlopm", request->powered ? "--on" : "--off", next.output}, &stopping).ok();
                        outputs.clear();
                        const bool observed = read(outputs);
                        next = observed ? select_output(options, outputs) : OutputPowerSnapshot{};
                        failed = !submitted || !next.available || next.output != request->output || next.powered != request->powered;
                        if (observed)
                        {
                            const auto actual = outputs.find(request->output);
                            if (actual != outputs.end() && actual->second) restore.erase(request->output);
                        }
                        if (failed && restore.count(request->output))
                        {
                            // Never leave a failed sleep operation as an accepted
                            // screen-off state. Keep ownership until readback.
                            runner({"wlopm", "--on", request->output}, nullptr);
                            outputs.clear();
                            if (read(outputs))
                            {
                                next = select_output(options, outputs);
                                const auto actual = outputs.find(request->output);
                                if (actual != outputs.end() && actual->second) restore.erase(request->output);
                            }
                        }
                    }
                }
            }
            next.failed = failed;
            {
                std::unique_lock<std::mutex> lock(mutex);
                published = next;
                wake.wait_for(lock, std::chrono::milliseconds(500), [this]
                              { return stopping || pending.has_value(); });
            }
        }
        // Target only outputs this owner turned off. Initially-off outputs and
        // newly attached connectors are never enabled by destruction.
        for (const auto& output : restore) runner({"wlopm", "--on", output}, nullptr);
    }
    OutputPowerOptions options;
    Runner runner;
    mutable std::mutex mutex;
    std::condition_variable wake;
    OutputPowerSnapshot published;
    std::optional<Request> pending;
    std::atomic_bool stopping{false};
    std::thread worker;
};

OutputPowerRuntime::OutputPowerRuntime(OutputPowerOptions options, Runner runner)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(runner))) {}
OutputPowerRuntime::~OutputPowerRuntime() = default;
OutputPowerSnapshot OutputPowerRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published;
}
bool OutputPowerRuntime::request(bool powered)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || !impl_->published.available) return false;
    impl_->pending = Impl::Request{impl_->published.output, powered};
    impl_->wake.notify_all();
    return true;
}
} // namespace platform::linux_runtime
