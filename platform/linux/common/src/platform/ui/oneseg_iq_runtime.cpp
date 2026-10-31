#include "platform/linux/oneseg_receive_worker.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_mode.h"
#include "platform/ui/lora_receive_session.h"
#include <cstdlib>
#include <cstring>

namespace platform::linux_runtime
{
std::unique_ptr<OneSegReceiveWorker> make_oneseg_receive_worker(OneSegIqSink& sink)
{
    if (resolve_runtime_mode() == LinuxRuntimeMode::SimulatorDemo) return nullptr;
    const char* simulated = std::getenv("TRAIL_MATE_LORA_SIMULATED");
    if (simulated && (!std::strcmp(simulated, "1") || !std::strcmp(simulated, "true") ||
                      !std::strcmp(simulated, "TRUE") || !std::strcmp(simulated, "yes") || !std::strcmp(simulated, "YES"))) return nullptr;
    // Establish receive-worker/coordinator lifetimes before creating TV threads.
    platform::ui::lora::detail::prepare_receive_session_worker();
    return std::make_unique<OneSegReceiveWorker>(foreground_packet_radio(), sink,
                                                 platform::ui::lora::wait_receive_session_released);
}
} // namespace platform::linux_runtime
