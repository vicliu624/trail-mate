#include "platform/linux/output_power.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
using namespace platform::linux_runtime;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template <class Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        require(std::chrono::steady_clock::now() < deadline, "output power condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
struct Compositor
{
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::string, bool> outputs{{"panel-A", true}};
    std::vector<std::vector<std::string>> writes;
    std::thread::id worker;
    bool fail_off = false;
    bool ignore_off = false;
    bool malformed = false;
    bool block_query = false;
    bool query_entered = false;

    ProcessResult run(const std::vector<std::string>& arguments, const std::atomic_bool* cancel)
    {
        std::unique_lock<std::mutex> lock(mutex);
        worker = std::this_thread::get_id();
        require(arguments.at(0) == "wlopm", "unexpected executable");
        if (arguments.size() == 1)
        {
            if (block_query)
            {
                query_entered = true;
                changed.notify_all();
                changed.wait(lock, [&]
                             { return !block_query || (cancel && *cancel); });
            }
            if (malformed) return {0, false, "panel-A unknown\n"};
            std::string text;
            for (const auto& [name, powered] : outputs) text += name + (powered ? " on\n" : " off\n");
            return {0, false, text};
        }
        require(arguments.size() == 3, "unexpected command arguments");
        writes.push_back(arguments);
        const auto output = outputs.find(arguments[2]);
        if (output == outputs.end()) return {1, false, {}};
        const bool on = arguments[1] == "--on";
        require(on || arguments[1] == "--off", "unexpected power verb");
        if (on || !ignore_off) output->second = on;
        return {(!on && fail_off) ? 1 : 0, false, {}};
    }
    auto runner()
    {
        return [this](const auto& arguments, const auto* cancel)
        { return run(arguments, cancel); };
    }
    bool powered(const std::string& output)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return outputs.at(output);
    }
};
} // namespace

int main()
{
    const auto caller = std::this_thread::get_id();
    Compositor compositor;
    {
        OutputPowerRuntime runtime({}, compositor.runner());
        await([&]
              { return runtime.snapshot().available; });
        require(runtime.snapshot().output == "panel-A" && runtime.snapshot().powered, "initial output not discovered");
        require(runtime.request(false), "sleep request rejected");
        await([&]
              { const auto state = runtime.snapshot(); return state.available && !state.powered && !state.failed; });
        require(!compositor.powered("panel-A"), "sleep was not submitted");
        require(runtime.request(true), "wake rejected");
        await([&]
              { return runtime.snapshot().powered; });
        require(runtime.request(false), "second sleep rejected");
        await([&]
              { return !runtime.snapshot().powered; });
    }
    require(compositor.powered("panel-A"), "shutdown did not restore owned sleep");
    require(compositor.worker != caller, "compositor I/O ran on caller thread");
    {
        std::lock_guard<std::mutex> lock(compositor.mutex);
        compositor.outputs = {{"panel-A", false}, {"external-B", true}};
        compositor.writes.clear();
    }
    {
        OutputPowerRuntime runtime({}, compositor.runner());
        await([&]
              { return runtime.snapshot().selection_required; });
        require(!runtime.request(false), "ambiguous request accepted");
    }
    require(compositor.writes.empty(), "ambiguous selection touched hardware");
    {
        OutputPowerRuntime runtime({"panel-A"}, compositor.runner());
        await([&]
              { return runtime.snapshot().available; });
        require(!runtime.snapshot().powered, "initially-off state lost");
        require(runtime.request(false), "idempotent sleep rejected");
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
    }
    require(!compositor.powered("panel-A") && compositor.writes.empty(), "initially-off output enabled on exit");
    {
        std::lock_guard<std::mutex> lock(compositor.mutex);
        compositor.outputs = {{"panel-A", true}};
        compositor.fail_off = true;
    }
    {
        OutputPowerRuntime runtime({}, compositor.runner());
        await([&]
              { return runtime.snapshot().available; });
        require(runtime.request(false), "failing request not queued");
        await([&]
              { return runtime.snapshot().failed; });
        require(runtime.snapshot().powered && compositor.powered("panel-A"), "partially applied failure not restored");
    }
    {
        std::lock_guard<std::mutex> lock(compositor.mutex);
        compositor.fail_off = false;
        compositor.ignore_off = true;
    }
    {
        OutputPowerRuntime runtime({}, compositor.runner());
        await([&]
              { return runtime.snapshot().available; });
        runtime.request(false);
        await([&]
              { return runtime.snapshot().failed; });
        require(runtime.snapshot().powered, "successful command mistaken for physical state");
    }
    {
        std::lock_guard<std::mutex> lock(compositor.mutex);
        compositor.ignore_off = false;
        compositor.malformed = true;
        compositor.writes.clear();
    }
    {
        OutputPowerRuntime runtime({}, compositor.runner());
        await([&]
              { return runtime.snapshot().failed; });
        require(!runtime.snapshot().available && !runtime.request(false), "malformed protocol accepted");
    }
    {
        std::lock_guard<std::mutex> lock(compositor.mutex);
        compositor.malformed = false;
    }
    {
        OutputPowerRuntime runtime({}, compositor.runner());
        await([&]
              { return runtime.snapshot().available; });
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.block_query = true;
        }
        {
            std::unique_lock<std::mutex> lock(compositor.mutex);
            require(compositor.changed.wait_for(lock, std::chrono::seconds(3), [&]
                                                { return compositor.query_entered; }),
                    "worker did not enter query barrier");
        }
        require(runtime.request(false), "bound request rejected");
        {
            std::lock_guard<std::mutex> lock(compositor.mutex);
            compositor.outputs = {{"replacement-C", true}};
            compositor.block_query = false;
            compositor.changed.notify_all();
        }
        await([&]
              { return runtime.snapshot().failed; });
        require(compositor.powered("replacement-C"), "stale request powered off replacement display");
    }
    require(compositor.writes.empty(), "malformed or stale request issued a power command");
}
