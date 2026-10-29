#pragma once
#include "chat/infra/voice/vmp_voice_inbox.h"
#include "platform/linux/voice_audio.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// Worker-owned local attachments only. This type cannot transmit or forward.
// Ingress must already have passed the VMP receiver's authentication checks.
class LinuxVoiceStore
{
  public:
    enum class Result
    {
        Ready,
        Duplicate,
        Missing,
        Invalid,
        Conflict,
        Corrupt,
        Unsupported,
        Error,
        Exhausted
    };
    explicit LinuxVoiceStore(std::filesystem::path database);
    ~LinuxVoiceStore();
    Result open();
    // Permanent until the corresponding static contact keys are retired.
    // Never delete/roll back these reservations while retaining those keys.
    Result reservePrivateControl(const std::array<uint8_t, 32>& key_identifier);
    // Cold hydration: interrupted Sending entries become Failed. Quiesce any
    // active carrier operation before replacing/reopening a store instance.
    Result append(const ::chat::voice::vmp::ControlFrame&, const VoiceClip&, bool outgoing,
                  uint32_t time, ::chat::voice::vmp::VoicePresentationProtocol, uint8_t channel, uint64_t& local_id);
    Result delivery(uint64_t local_id, ::chat::voice::vmp::VoiceDeliveryState);
    Result markRead(::chat::voice::vmp::VoicePresentationProtocol, uint8_t channel, uint32_t peer, bool broadcast);
    Result erase(uint64_t local_id);
    size_t list(::chat::voice::vmp::VoiceMessageMetadata*, size_t capacity) const;
    bool media(uint64_t local_id, VoiceClip&) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
