#pragma once
#include "platform/linux/voice_source.h"
#include "ui/chat_voice_runtime.h"

namespace trailmate::linux_ui
{
// UI-thread binding. The composition must release it before destroying source.
class VoiceRuntimeBinding final : public ::ui::chat_voice::IVoiceMessageRuntime
{
  public:
    explicit VoiceRuntimeBinding(::platform::linux_runtime::IVoiceSource*);
    ~VoiceRuntimeBinding() override;
    void release();
    bool isAvailable() const override;
    bool canRecordAndSend() const override;
    ::ui::chat_voice::StartResult requestRecordAndSend(const ::ui::chat_voice::SendRequest&) override;
    bool requestStopRecording() override;
    bool isOutboundActive() const override;
    size_t listMessages(::ui::chat_voice::MessageSummary*, size_t) const override;
    bool markConversationRead(uint8_t, uint8_t, uint32_t, bool) override;
    bool requestPlayback(uint64_t) override;

  private:
    ::platform::linux_runtime::IVoiceSource* source_;
};
} // namespace trailmate::linux_ui
