#include <cassert>
#include <cstdlib>

#include "platform/ui/lora_runtime.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"
#include "ui/screens/sstv/sstv_page_runtime.h"
#include "ui/screens/walkie_talkie/walkie_talkie_page_runtime.h"

int main()
{
    // Explicit simulation exercises the page/platform boundary without opening
    // or transmitting through a physical radio. No device identity is defined.
    assert(setenv("TRAIL_MATE_LORA_SIMULATED", "1", 1) == 0);
    assert(platform::ui::lora::is_supported());
    assert(energy_sweep::ui::runtime::is_available());
    assert(platform::ui::walkie::is_supported());
    assert(walkie_page::ui::runtime::is_available());
    assert(platform::ui::sstv::is_supported());
    assert(sstv_page::ui::runtime::is_available());
}
