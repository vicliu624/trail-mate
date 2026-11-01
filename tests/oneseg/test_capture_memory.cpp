#include "platform/linux/oneseg_capture_memory.h"
#include <cassert>
#include <cstdio>
#include <sys/resource.h>
using namespace platform::linux_runtime;
int main()
{
    rlimit original{};
    assert(!getrlimit(RLIMIT_MEMLOCK, &original));
    auto denied = original;
    denied.rlim_cur = 0;
    assert(!setrlimit(RLIMIT_MEMLOCK, &denied));
    OneSegCaptureMemory memory;
    assert(!memory.lock() && memory.error() && memory.bytes() == 0);
    assert(!setrlimit(RLIMIT_MEMLOCK, &original));
    assert(memory.lock());
    assert(memory.bytes() && memory.bytes() <= 48 * 1024 * 1024);
    assert(!memory.lock() && memory.error() == EALREADY);
    std::fprintf(stderr, "Capture runtime memory admission passed, locked=%zu bytes\n", memory.bytes());
}
