#include "platform/linux/oneseg_tv_session.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_mode.h"
#include "platform/ui/lora_receive_session.h"

namespace platform::linux_runtime
{
std::unique_ptr<OneSegTvSession> make_oneseg_tv_session(OneSegTvSession::Config config)
{
    const auto mode = resolve_runtime_mode();
    if (demo_world_enabled(mode)) return nullptr;
    const char* simulated = std::getenv("TRAIL_MATE_LORA_SIMULATED");
    if (simulated && (!std::strcmp(simulated, "1") || !std::strcmp(simulated, "true") ||
                      !std::strcmp(simulated, "TRUE") || !std::strcmp(simulated, "yes") || !std::strcmp(simulated, "YES"))) return nullptr;
    platform::ui::lora::detail::prepare_receive_session_worker();
    return std::make_unique<OneSegTvSession>(foreground_packet_radio(), std::move(config),
                                             platform::ui::lora::wait_receive_session_released);
}
} // namespace platform::linux_runtime
