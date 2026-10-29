#include "platform/linux/retry_backoff.h"
#include <cassert>

int main()
{
    platform::linux_runtime::RetryBackoff retry(250, 1000);
    assert(retry.ready(0));
    retry.failed(1000);
    assert(retry.delay() == 250 && !retry.ready(1249) && retry.ready(1250));
    retry.failed(1250);
    assert(retry.delay() == 500 && !retry.ready(1749) && retry.ready(1750));
    retry.failed(1750);
    assert(retry.delay() == 1000 && !retry.ready(2749) && retry.ready(2750));
    retry.failed(2750);
    assert(retry.delay() == 1000);
    retry.reset();
    assert(retry.ready(0));
    retry.failed(0xFFFFFFF0U);
    assert(!retry.ready(0x10U) && retry.ready(0xEAU));
    retry.reset();
    // The owner reports the end of a slow operation, not its start.
    retry.failed(3000);
    assert(!retry.ready(3001) && retry.ready(3250));
}
