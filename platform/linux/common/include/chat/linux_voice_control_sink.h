#pragma once
#include "chat/domain/chat_types.h"
#include "chat/infra/voice/vmp_control_ingress.h"
namespace trailmate::linux_app
{
class ILinuxVoiceControlSink
{
  public:
    virtual ~ILinuxVoiceControlSink() = default;
    virtual bool enqueueVoiceControl(const uint8_t*, size_t, const ::chat::voice::vmp::ControlRxMetadata&,
                                     ::chat::NodeId self, ::chat::MeshProtocol protocol) = 0;
    // Owned MQTT envelopes terminate at the local inbox, never at the radio.
    virtual bool enqueueVoiceMqttEnvelope(const uint8_t*, size_t, ::chat::NodeId, ::chat::MeshProtocol) { return false; }
};
} // namespace trailmate::linux_app
