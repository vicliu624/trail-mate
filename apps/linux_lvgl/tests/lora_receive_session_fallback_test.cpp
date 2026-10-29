#undef TRAIL_MATE_PLATFORM_LINUX
#define ESP_PLATFORM 1
#include "platform/ui/lora_receive_session.h"
#include <stdexcept>

namespace
{
bool fail_config = false, fail_tx = false;
unsigned released = 0;
void require(bool value)
{
    if (!value) throw std::runtime_error("Synchronous receive compatibility failed");
}
} // namespace
namespace platform::ui::lora
{
bool acquire() { return true; }
bool configure_receive(float, const ReceiveConfig&) { return !fail_config; }
bool transmit_packet(const uint8_t*, size_t) { return !fail_tx; }
bool poll_received_packet(uint8_t*, size_t, ReceivedPacket*) { return true; }
void release() { ++released; }
} // namespace platform::ui::lora
int main()
{
    using namespace platform::ui::lora;
    ReceiveConfig config;
    const auto first = request_receive_session(433, config);
    require(first && receive_session_snapshot().state == ReceiveSessionState::Ready);
    fail_config = true;
    const auto failed = request_receive_session(434, config);
    require(failed != first && receive_session_snapshot().state == ReceiveSessionState::Failed);
    release_receive_session(first);
    require(!released);
    fail_config = false;
    const auto recovered = request_receive_session(435, config);
    uint8_t byte = 1;
    require(!transmit_receive_session(first, &byte, 1));
    require(transmit_receive_session(recovered, &byte, 1) && !receive_session_snapshot().transmit_pending);
    fail_tx = true;
    require(!transmit_receive_session(recovered, &byte, 1));
    require(receive_session_snapshot().state == ReceiveSessionState::Failed);
    release_receive_session(recovered);
    require(released == 1 && receive_session_snapshot().state == ReceiveSessionState::Idle);
}
