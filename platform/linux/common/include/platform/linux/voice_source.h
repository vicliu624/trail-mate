#pragma once
#include "chat/infra/voice/vmp_voice_inbox.h"
#include <string>
namespace platform::linux_runtime
{
class IVoiceSource
{
  public:
    enum class Admission
    {
        Queued,
        Unsupported,
        Busy,
        Unverified,
        Unavailable
    };
    struct Snapshot
    {
        bool initialized = false, available = false, can_record = false, busy = false, outbound = false;
        std::string error;
    };
    virtual ~IVoiceSource() = default;
    virtual Snapshot snapshot() const = 0;
    virtual size_t list(::chat::voice::vmp::VoiceMessageMetadata*, size_t) const = 0;
    virtual Admission record(uint32_t peer, uint8_t protocol, uint8_t channel) = 0;
    virtual bool stopRecording() = 0;
    virtual bool play(uint64_t local_id) = 0;
    virtual bool markRead(uint8_t protocol, uint8_t channel, uint32_t peer, bool broadcast) = 0;
};
} // namespace platform::linux_runtime
