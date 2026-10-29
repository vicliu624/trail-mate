#include "ui/voice_runtime_binding.h"
#include <algorithm>
#include <array>

namespace trailmate::linux_ui
{
namespace vmp = ::chat::voice::vmp;
VoiceRuntimeBinding::VoiceRuntimeBinding(::platform::linux_runtime::IVoiceSource* source) : source_(source)
{
    if (source_) ::ui::chat_voice::setRuntime(this);
}
VoiceRuntimeBinding::~VoiceRuntimeBinding() { release(); }
void VoiceRuntimeBinding::release()
{
    if (source_) ::ui::chat_voice::setRuntime(nullptr);
    source_ = nullptr;
}
bool VoiceRuntimeBinding::isAvailable() const { return source_ && source_->snapshot().available; }
bool VoiceRuntimeBinding::canRecordAndSend() const { return source_ && source_->snapshot().can_record; }
::ui::chat_voice::StartResult VoiceRuntimeBinding::requestRecordAndSend(const ::ui::chat_voice::SendRequest& request)
{
    using Admission = ::platform::linux_runtime::IVoiceSource::Admission;
    using Start = ::ui::chat_voice::StartResult;
    if (!source_) return Start::Unsupported;
    switch (source_->record(request.target_id, request.presentation_protocol, request.presentation_channel))
    {
    case Admission::Queued:
        return Start::Queued;
    case Admission::Unsupported:
        return Start::Unsupported;
    case Admission::Busy:
        return Start::Busy;
    case Admission::Unverified:
        return Start::PrivateContactUnverified;
    default:
        return Start::ResourceUnavailable;
    }
}
bool VoiceRuntimeBinding::requestStopRecording() { return source_ && source_->stopRecording(); }
bool VoiceRuntimeBinding::isOutboundActive() const { return source_ && source_->snapshot().outbound; }
size_t VoiceRuntimeBinding::listMessages(::ui::chat_voice::MessageSummary* out, size_t capacity) const
{
    if (!source_ || !out) return 0;
    std::array<vmp::VoiceMessageMetadata, vmp::kVoiceInboxCapacity> rows{};
    const auto count = std::min({source_->list(rows.data(), rows.size()), capacity, rows.size()});
    for (size_t i = 0; i < count; ++i)
    {
        const auto& row = rows[i];
        out[i] = {row.local_id, row.sender_id, row.target_id, row.received_at_seconds,
                  static_cast<uint16_t>(row.encoded_media_len / 7 * 40), row.mode == vmp::DeliveryMode::Private,
                  vmp::voiceMessageHasFlag(row, vmp::VoiceMessageFlagSourceUnverified),
                  vmp::voiceMessageHasFlag(row, vmp::VoiceMessageFlagOutgoing),
                  static_cast<::ui::chat_voice::DeliveryState>(row.delivery),
                  static_cast<uint8_t>(row.presentation_protocol), row.presentation_channel,
                  vmp::voiceMessageHasFlag(row, vmp::VoiceMessageFlagRead)};
    }
    return count;
}
bool VoiceRuntimeBinding::markConversationRead(uint8_t protocol, uint8_t channel, uint32_t peer, bool broadcast)
{
    return source_ && source_->markRead(protocol, channel, peer, broadcast);
}
bool VoiceRuntimeBinding::requestPlayback(uint64_t id) { return source_ && source_->play(id); }
} // namespace trailmate::linux_ui
