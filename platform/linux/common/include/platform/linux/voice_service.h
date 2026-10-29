#pragma once
#include "chat/linux_voice_control_sink.h"
#include "platform/linux/vmp_session.h"
#include "platform/linux/voice_source.h"
#include <filesystem>
#include <functional>
#include <vector>
namespace platform::linux_runtime
{
struct VoiceServiceOptions
{
    std::filesystem::path database;
    uint32_t self = 0;
    ::chat::MeshProtocol protocol = ::chat::MeshProtocol::Meshtastic;
    std::function<std::unique_ptr<IVoicePcm>()> pcm;
    std::function<bool()> capture_available;
    std::function<const ::chat::voice::vmp::IVerifiedContactSecretProvider*(::chat::MeshProtocol)> contacts;
    std::vector<uint32_t> verified_peers;
    // Cached public trust only; called on the frontend, must never do I/O.
    std::function<bool(uint32_t, ::chat::MeshProtocol)> is_verified;
    std::function<void(uint8_t, uint64_t, bool)> changed;
    IVmpClock* clock = nullptr; // Composition-owned; must outlive shutdown.
    std::function<bool(const uint8_t*, size_t, const std::atomic_bool&)> mqtt_publish;
};
class NativeVoiceService final : public IVoiceSource, public ::trailmate::linux_app::ILinuxVoiceControlSink
{
  public:
    NativeVoiceService(IPacketRadio& foreground, VoiceServiceOptions);
    ~NativeVoiceService();
    void shutdown();
    void setContext(uint32_t self, ::chat::MeshProtocol protocol, bool radio_ready);
    void invalidateContext();
    void setMqttReceiveReady(bool);
    void setMqttSendReady(bool selected, bool ready);
    Snapshot snapshot() const override;
    size_t list(::chat::voice::vmp::VoiceMessageMetadata*, size_t) const override;
    Admission record(uint32_t, uint8_t, uint8_t) override;
    bool stopRecording() override;
    bool play(uint64_t) override;
    bool markRead(uint8_t, uint8_t, uint32_t, bool) override;
    bool enqueueVoiceControl(const uint8_t*, size_t, const ::chat::voice::vmp::ControlRxMetadata&,
                             ::chat::NodeId, ::chat::MeshProtocol) override;
    bool enqueueVoiceMqttEnvelope(const uint8_t*, size_t, ::chat::NodeId, ::chat::MeshProtocol) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
