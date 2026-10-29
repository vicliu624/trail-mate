#pragma once
#include "chat/infra/voice/vmp_contact_secrets.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/voice_audio.h"

namespace platform::linux_runtime
{
class IVmpClock
{
  public:
    virtual ~IVmpClock() = default;
    virtual uint64_t nowMs() const = 0;
    virtual void sleepMs(unsigned) = 0;
};
class IVmpIncomingCommit
{
  public:
    enum class Reservation
    {
        Reserved,
        Replayed,
        Unavailable
    };
    virtual ~IVmpIncomingCommit() = default;
    // Durable one-time reservation before generating a private ACCEPT. The
    // identifier hashes a verified control key; it contains no key material.
    // Default denial prevents stateless receivers from reusing an AEAD nonce.
    virtual Reservation reservePrivateControl(const std::array<uint8_t, 32>&) { return Reservation::Unavailable; }
    // Synchronous worker callback. Copy/persist before returning; true means
    // the local attachment is durable. No borrowed pointer may be retained.
    virtual bool commit(const ::chat::voice::vmp::ControlFrame&, const VoiceClip&) = 0;
};
struct VmpSendRequest
{
    uint32_t self = 0, peer = 0; // peer==0 selects public broadcast.
    uint8_t conversation_channel = 0, media_channel = 0;
};
// One worker-owned direct-RF operation. No UI, mesh TX queue, or relay API.
// The contact provider supplies only previously verified K_contact values.
// Borrowed contacts/clock must outlive the session. The service owner cancels
// an operation if its identity/protocol context changes before RF admission.
class NativeVmpSession
{
  public:
    enum class Result
    {
        Prepared,
        Complete,
        Ignored,
        Invalid,
        Unverified,
        Unsupported,
        CryptoFailure,
        Cancelled,
        Timeout,
        RadioFailure,
        StorageFailure,
        RestoreFailure
    };
    explicit NativeVmpSession(IPacketRadio& foreground,
                              const ::chat::voice::vmp::IVerifiedContactSecretProvider* contacts = nullptr,
                              IVmpClock* clock = nullptr);
    ~NativeVmpSession();
    Result prepare(const VmpSendRequest&, const VoiceClip&);
    // Valid only after Prepared, until the next mutating call. Persist the
    // outgoing attachment before send(), and discard() if that commit fails.
    const ::chat::voice::vmp::ControlFrame& control() const;
    const VoiceClip& media() const;
    Result send(const std::atomic_bool& cancel);
    // RestoreFailure may follow a successful durable commit; callers preserve
    // that attachment and report radio health independently.
    Result receive(const uint8_t* control, size_t size, uint32_t self,
                   IVmpIncomingCommit&, const std::atomic_bool& cancel);
    void discard();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
