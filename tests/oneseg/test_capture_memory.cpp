#include "platform/linux/oneseg_capture_memory.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <sys/resource.h>
#include <thread>
using namespace platform::linux_runtime;
static bool locked(const void* pointer)
{
    std::ifstream maps("/proc/self/smaps");
    std::string line;
    bool selected = false;
    while (std::getline(maps, line))
    {
        unsigned long begin = 0, end = 0;
        if (std::sscanf(line.c_str(), "%lx-%lx", &begin, &end) == 2)
            selected = reinterpret_cast<uintptr_t>(pointer) >= begin && reinterpret_cast<uintptr_t>(pointer) < end;
        if (selected && line.compare(0, 8, "VmFlags:") == 0) return line.find(" lo") != std::string::npos;
    }
    return false;
}
int main()
{
    std::thread worker([]
                       {
    rlimit original{};
    assert(!getrlimit(RLIMIT_MEMLOCK, &original));
    auto denied = original;
    denied.rlim_cur = 0;
    assert(!setrlimit(RLIMIT_MEMLOCK, &denied));
    OneSegCaptureMemory memory;
    assert(!memory.lock() && memory.error() && memory.bytes() == 0);
    assert(!setrlimit(RLIMIT_MEMLOCK, &original));
    std::map<unsigned, int> gpio;
    gpio.emplace(24, 7);
    const auto* node = &gpio.begin()->second;
    assert(!locked(node));
    assert(memory.lock());
    assert(locked(node)); // GPIO map nodes are accessed on every BUSY poll.
    assert(memory.bytes() && memory.bytes() <= 48 * 1024 * 1024);
    assert(!memory.lock() && memory.error() == EALREADY);
    std::fprintf(stderr, "Capture runtime memory admission passed, locked=%zu bytes\n", memory.bytes()); });
    worker.join();
}
