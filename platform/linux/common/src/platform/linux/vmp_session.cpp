#include "platform/linux/vmp_session.h"
#include "chat/infra/voice/vmp_control_auth.h"
#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/infra/voice/vmp_receive_block.h"
#include "chat/infra/voice/vmp_session_state_machine.h"
#include "platform/linux/vmp_radio.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace vmp = ::chat::voice::vmp;
using Result = NativeVmpSession::Result;
constexpr unsigned receive_ms = 5000, accept_ms = 1500, ready_ms = 40, probes = 3;
class Clock final : public IVmpClock
{
  public:
    uint64_t nowMs() const override { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void sleepMs(unsigned ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};
void wipe(void* data, size_t size)
{
    auto* bytes = static_cast<volatile uint8_t*>(data);
    while (size--) *bytes++ = 0;
}
bool random(void* data, size_t size)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    return RAND_bytes(static_cast<unsigned char*>(data), static_cast<int>(size)) == 1;
#else
    (void)data;
    (void)size;
    return false;
#endif
}
bool reservationId(const uint8_t* key, std::array<uint8_t, 32>& out)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    unsigned length = 0;
    return EVP_Digest(key, vmp::kPrivateKeySize, out.data(), &length, EVP_sha256(), nullptr) == 1 && length == out.size();
#else
    (void)key;
    (void)out;
    return false;
#endif
}
bool profile(const vmp::ControlFrame& control)
{
    return control.phy_profile_id == 1 && control.channel_index <= 39 && control.codec == vmp::Codec::Codec2_1300 &&
           control.encoded_media_len && control.encoded_media_len <= VoiceClip::bytes_per_frame * VoiceClip::max_frames &&
           control.encoded_media_len % VoiceClip::bytes_per_frame == 0 && control.data_start_delay_ms > 0 && control.data_start_delay_ms < receive_ms;
}
bool acceptMatches(const vmp::ControlFrame& offer, const vmp::ControlFrame& accept)
{
    return accept.type == vmp::ControlType::Accept && accept.sender_id == offer.target_id && accept.target_id == offer.sender_id &&
           accept.flags == offer.flags && accept.session_id == offer.session_id && accept.object_fingerprint == offer.object_fingerprint &&
           accept.codec == offer.codec && accept.encoded_media_len == offer.encoded_media_len && accept.phy_profile_id == offer.phy_profile_id &&
           accept.channel_index == offer.channel_index && accept.conversation_channel == offer.conversation_channel &&
           accept.fec_layout == offer.fec_layout && accept.total_blocks == offer.total_blocks && accept.data_start_delay_ms == offer.data_start_delay_ms &&
           !std::memcmp(accept.session_nonce, offer.session_nonce, vmp::kSessionNonceSize);
}
} // namespace
struct NativeVmpSession::Impl
{
    Impl(IPacketRadio& radio, const vmp::IVerifiedContactSecretProvider* provider, IVmpClock* timer)
        : lease(radio), contacts(provider), clock(timer ? *timer : default_clock) {}
    ~Impl() { reset(); }
    void clearSecrets()
    {
        vmp::clearPrivateSessionKeys(&keys);
        wipe(secret.data(), secret.size());
        wipe(&ephemeral, sizeof(ephemeral));
        wipe(plaintext.data(), plaintext.size());
        wipe(reservation.data(), reservation.size());
    }
    void reset()
    {
        lease.end();
        clearSecrets();
        clearMedia();
        outgoing = {};
        incoming = {};
        candidate = {};
        machine = {};
        prepared = private_mode = false;
        attempted_radio = false;
    }
    void clearMedia()
    {
        transmit.clear();
        receive.clear();
        clip.clear();
        wipe(wire.data(), wire.size());
        wipe(packet.data.data(), packet.data.size());
        packet.size = 0;
    }
    Result finish(Result result)
    {
        const bool restored = lease.end();
        const bool attempted = attempted_radio;
        clearSecrets();
        clearMedia();
        prepared = false;
        attempted_radio = false;
        return attempted && !restored ? Result::RestoreFailure : result;
    }
    bool wait(unsigned duration, const std::atomic_bool& cancel)
    {
        const auto deadline = clock.nowMs() + duration;
        while (clock.nowMs() < deadline)
        {
            if (cancel) return false;
            clock.sleepMs(static_cast<unsigned>(std::min<uint64_t>(2, deadline - clock.nowMs())));
        }
        return !cancel;
    }
    bool openRadio(uint8_t channel)
    {
        attempted_radio = true;
        return lease.begin(channel);
    }
    bool sendControl(const vmp::ControlFrame& value, bool private_mode, vmp::PrivateFrameDirection direction)
    {
        size_t size = wire.size();
        const bool built = private_mode ? vmp::encodePrivateControlFrame(value, keys, direction, wire.data(), &size)
                                        : vmp::encodePublicControlFrame(value, wire.data(), &size);
        return built && lease.transmit(wire.data(), size);
    }
    bool buildReady(vmp::DataType type, uint64_t session, const uint8_t* nonce, bool private_mode, vmp::PrivateFrameDirection direction)
    {
        header = {};
        header.type = type;
        header.session_id = session;
        size_t size = wire.size();
        if (private_mode)
        {
            size = vmp::kDataHeaderSize;
            if (!vmp::encodeDataHeader(header, wire.data(), &size) || !vmp::tagPrivateReady(keys, nonce, direction, header, wire.data() + size)) return false;
            size += vmp::kPrivateDataAuthTagSize;
        }
        else if (!vmp::buildPublicReadyFrame(header, wire.data(), &size)) return false;
        return lease.transmit(wire.data(), size);
    }
    bool privateReady(const RadioPacket& packet, vmp::DataType type, const vmp::ControlFrame& control, vmp::PrivateFrameDirection direction)
    {
        return packet.size == vmp::kDataHeaderSize + vmp::kPrivateDataAuthTagSize &&
               vmp::decodeDataHeader(packet.data.data(), vmp::kDataHeaderSize, &header) && header.type == type &&
               header.session_id == control.session_id && vmp::verifyPrivateReadyTag(keys, control.session_nonce, direction, header, packet.data.data() + vmp::kDataHeaderSize);
    }
    Result sendBody(const std::atomic_bool& cancel)
    {
        if (!prepared) return Result::Invalid;
        if (cancel) return Result::Cancelled;
        const auto mode = private_mode ? vmp::DeliveryMode::Private : vmp::DeliveryMode::Broadcast;
        const auto start = machine.startSender(mode);
        if (!start.accepted || !openRadio(outgoing.channel_index)) return Result::RadioFailure;
        if (cancel) return Result::Cancelled;
        if (!sendControl(outgoing, private_mode, vmp::PrivateFrameDirection::SenderToReceiver)) return Result::RadioFailure;
        if (private_mode)
        {
            const auto deadline = clock.nowMs() + accept_ms;
            bool accepted = false;
            while (!cancel && clock.nowMs() < deadline)
            {
                if (lease.receive(packet) && vmp::decodePrivateControlFrame(packet.data.data(), packet.size, keys, vmp::PrivateFrameDirection::ReceiverToSender, &candidate) && acceptMatches(outgoing, candidate))
                {
                    accepted = true;
                    break;
                }
                clock.sleepMs(2);
            }
            if (cancel) return Result::Cancelled;
            if (!accepted)
            {
                machine.dispatch(vmp::SessionEvent::ControlDeadlineExpired);
                return Result::Timeout;
            }
            if (!vmp::derivePrivateSessionKeys(secret.data(), ephemeral.private_key, candidate.ephemeral_public_key,
                                               outgoing.session_nonce, outgoing.session_id, &keys)) return Result::CryptoFailure;
            if (!machine.dispatch(vmp::SessionEvent::SubGhzAcceptAuthenticated).hasAction(vmp::SessionAction::SwitchTo2GhzTx)) return Result::Invalid;
        }
        else
        {
            if (!wait(outgoing.data_start_delay_ms, cancel)) return Result::Cancelled;
            if (!machine.dispatch(vmp::SessionEvent::BroadcastDataWindowReady).hasAction(vmp::SessionAction::BeginVoiceMediaTx)) return Result::Invalid;
        }
        if (!lease.switchToMedia()) return Result::RadioFailure;
        if (private_mode && !wait(outgoing.data_start_delay_ms, cancel)) return Result::Cancelled;
        bool ready = !private_mode;
        for (unsigned index = 0; index < probes; ++index)
        {
            if (cancel) return Result::Cancelled;
            if (!buildReady(vmp::DataType::ReadyProbe, outgoing.session_id, outgoing.session_nonce, private_mode,
                            vmp::PrivateFrameDirection::SenderToReceiver)) return Result::RadioFailure;
            if (private_mode)
            {
                const auto deadline = clock.nowMs() + ready_ms;
                while (!cancel && clock.nowMs() < deadline)
                {
                    if (lease.receive(packet) && privateReady(packet, vmp::DataType::Ready, outgoing, vmp::PrivateFrameDirection::ReceiverToSender))
                    {
                        ready = true;
                        break;
                    }
                    clock.sleepMs(2);
                }
                if (ready) break;
            }
            if (!wait(ready_ms, cancel)) return Result::Cancelled;
        }
        if (cancel) return Result::Cancelled;
        if (!ready)
        {
            machine.dispatch(vmp::SessionEvent::ReadyDeadlineExpired);
            return Result::Timeout;
        }
        if (private_mode && !machine.dispatch(vmp::SessionEvent::TwoGhzReadyAuthenticated).hasAction(vmp::SessionAction::BeginVoiceMediaTx)) return Result::Invalid;
        for (uint8_t index = 0; index < vmp::kTotalShardsPerBlock; ++index)
        {
            if (cancel) return Result::Cancelled;
            size_t size = wire.size();
            const bool built = private_mode ? transmit.buildPrivateShardFrame(keys, outgoing.session_nonce, outgoing.session_id, index, wire.data(), &size)
                                            : transmit.buildPublicShardFrame(outgoing.session_id, index, wire.data(), &size);
            if (!built || !lease.transmit(wire.data(), size)) return Result::RadioFailure;
        }
        return machine.dispatch(vmp::SessionEvent::VoiceDataTrainComplete).hasAction(vmp::SessionAction::RestoreSubGhzRx) ? Result::Complete : Result::Invalid;
    }
    Result receiveBody(const uint8_t* bytes, size_t size, uint32_t self, IVmpIncomingCommit& commit, const std::atomic_bool& cancel)
    {
        if (cancel) return Result::Cancelled;
        const auto accept_deadline = clock.nowMs() + accept_ms;
        if (!self || !vmp::decodeControlFrame(bytes, size, &candidate)) return Result::Invalid;
        if (candidate.sender_id == self) return Result::Ignored;
        vmp::DeliveryMode mode;
        if (!vmp::deliveryModeFor(candidate, &mode)) return Result::Invalid;
        private_mode = mode == vmp::DeliveryMode::Private;
        if (private_mode ? candidate.type != vmp::ControlType::Offer || candidate.target_id != self
                         : candidate.type != vmp::ControlType::Announce) return Result::Ignored;
        if (!profile(candidate) || !lease.supports(candidate.channel_index)) return Result::Unsupported;
        if (private_mode)
        {
            if (!contacts || !contacts->lookupVerifiedContactSecret(candidate.sender_id, secret.data())) return Result::Unverified;
            if (!vmp::derivePrivateControlKey(secret.data(), candidate.session_nonce, candidate.session_id, keys.control_key) ||
                !vmp::decodePrivateControlFrame(bytes, size, keys, vmp::PrivateFrameDirection::SenderToReceiver, &incoming) ||
                !reservationId(keys.control_key, reservation)) return Result::CryptoFailure;
            if (cancel) return Result::Cancelled;
            IVmpIncomingCommit::Reservation reserved;
            try
            {
                reserved = commit.reservePrivateControl(reservation);
            }
            catch (...)
            {
                return Result::StorageFailure;
            }
            if (reserved == IVmpIncomingCommit::Reservation::Replayed) return Result::Ignored;
            if (reserved != IVmpIncomingCommit::Reservation::Reserved) return Result::StorageFailure;
            if (cancel) return Result::Cancelled;
            if (clock.nowMs() >= accept_deadline) return Result::Timeout;
            if (!vmp::generateEphemeralKeyPair(&ephemeral) || !vmp::derivePrivateSessionKeys(secret.data(), ephemeral.private_key, incoming.ephemeral_public_key, incoming.session_nonce, incoming.session_id, &keys)) return Result::CryptoFailure;
        }
        else if (!vmp::decodePublicControlFrame(bytes, size, &incoming)) return Result::CryptoFailure;
        vmp::MediaLayout layout;
        if (!vmp::planMediaLayout(incoming.encoded_media_len, &layout) || !receive.begin(layout)) return Result::Invalid;
        const auto start = machine.startReceiver(mode);
        if (cancel) return Result::Cancelled;
        if (!start.accepted || !openRadio(incoming.channel_index)) return Result::RadioFailure;
        if (cancel) return Result::Cancelled;
        if (private_mode)
        {
            if (clock.nowMs() >= accept_deadline) return Result::Timeout;
            outgoing = incoming;
            outgoing.type = vmp::ControlType::Accept;
            outgoing.sender_id = self;
            outgoing.target_id = incoming.sender_id;
            std::copy_n(ephemeral.public_key, sizeof(ephemeral.public_key), outgoing.ephemeral_public_key);
            if (!sendControl(outgoing, true, vmp::PrivateFrameDirection::ReceiverToSender)) return Result::RadioFailure;
        }
        if (cancel) return Result::Cancelled;
        if (!lease.switchToMedia()) return Result::RadioFailure;
        const auto deadline = clock.nowMs() + receive_ms;
        unsigned ready_replies = 0;
        while (!cancel && clock.nowMs() < deadline)
        {
            if (!lease.receive(packet))
            {
                clock.sleepMs(2);
                continue;
            }
            if (cancel) return Result::Cancelled;
            const auto state = machine.state();
            if (private_mode && privateReady(packet, vmp::DataType::ReadyProbe, incoming, vmp::PrivateFrameDirection::SenderToReceiver))
            {
                if (state == vmp::SessionState::Awaiting2GhzProbe) machine.dispatch(vmp::SessionEvent::TwoGhzReadyProbeAuthenticated);
                // Duplicate probes may need the same READY again after RF loss;
                // do not restart the state/timer or reply beyond the probe bound.
                if ((state == vmp::SessionState::Awaiting2GhzProbe || state == vmp::SessionState::AwaitingVoiceMedia) && ready_replies < probes)
                {
                    if (!buildReady(vmp::DataType::Ready, incoming.session_id, incoming.session_nonce, true,
                                    vmp::PrivateFrameDirection::ReceiverToSender)) return Result::RadioFailure;
                    ++ready_replies;
                }
                clock.sleepMs(2);
                continue;
            }
            if (!private_mode && vmp::parsePublicReadyFrame(packet.data.data(), packet.size, &header) &&
                header.session_id == incoming.session_id && header.type == vmp::DataType::ReadyProbe)
            {
                machine.dispatch(vmp::SessionEvent::TwoGhzReadyProbeAuthenticated);
                clock.sleepMs(2);
                continue;
            }
            const uint8_t* shard = nullptr;
            if (private_mode)
            {
                if (packet.size == vmp::kPrivateShardFrameSize && vmp::decodeDataHeader(packet.data.data(), vmp::kDataHeaderSize, &header) &&
                    header.type == vmp::DataType::Shard && header.session_id == incoming.session_id &&
                    vmp::openPrivateShard(keys, incoming.session_nonce, vmp::PrivateFrameDirection::SenderToReceiver, header,
                                          packet.data.data() + vmp::kDataHeaderSize, vmp::kMaxShardPayloadSize,
                                          packet.data.data() + vmp::kDataHeaderSize + vmp::kMaxShardPayloadSize, plaintext.data())) shard = plaintext.data();
            }
            else if (!vmp::parsePublicShardFrame(packet.data.data(), packet.size, &header, &shard) || header.session_id != incoming.session_id) shard = nullptr;
            if (shard)
            {
                if (machine.state() != vmp::SessionState::ReceivingVoiceMedia &&
                    !machine.dispatch(vmp::SessionEvent::TwoGhzVoiceShardAuthenticated).accepted)
                {
                    clock.sleepMs(2);
                    continue;
                }
                if (receive.accept(header, shard, vmp::kMaxShardPayloadSize) == vmp::ReceiveBlockResult::Complete)
                {
                    size_t length = 0;
                    if (!receive.recover(clip.bytes.data(), clip.bytes.size(), &length)) return Result::Invalid;
                    clip.size = length;
                    if (!clip.valid() || !machine.dispatch(vmp::SessionEvent::FecBlockRecovered).hasAction(vmp::SessionAction::CommitCompleteIncomingVoice)) return Result::Invalid;
                    if (cancel) return Result::Cancelled;
                    try
                    {
                        return commit.commit(incoming, clip) ? Result::Complete : Result::StorageFailure;
                    }
                    catch (...)
                    {
                        return Result::StorageFailure;
                    }
                }
            }
            clock.sleepMs(2);
        }
        if (cancel) return Result::Cancelled;
        machine.dispatch(vmp::SessionEvent::MediaDeadlineExpired);
        return Result::Timeout;
    }
    VmpRadioLease lease;
    const vmp::IVerifiedContactSecretProvider* contacts;
    Clock default_clock;
    IVmpClock& clock;
    vmp::ControlFrame outgoing{}, incoming{}, candidate{};
    vmp::DataHeader header{};
    vmp::EphemeralKeyPair ephemeral{};
    vmp::PrivateSessionKeys keys{};
    std::array<uint8_t, vmp::kPrivateKeySize> secret{};
    std::array<uint8_t, 32> reservation{};
    std::array<uint8_t, vmp::kMaxShardPayloadSize> plaintext{};
    std::array<uint8_t, 255> wire{};
    RadioPacket packet;
    vmp::TransmitBlock transmit;
    vmp::ReceiveBlock receive;
    vmp::SessionStateMachine machine;
    VoiceClip clip;
    bool prepared = false, private_mode = false, attempted_radio = false;
};
NativeVmpSession::NativeVmpSession(IPacketRadio& radio, const vmp::IVerifiedContactSecretProvider* contacts, IVmpClock* clock)
    : impl_(std::make_unique<Impl>(radio, contacts, clock)) {}
NativeVmpSession::~NativeVmpSession() = default;
void NativeVmpSession::discard() { impl_->reset(); }
const vmp::ControlFrame& NativeVmpSession::control() const { return impl_->outgoing; }
const VoiceClip& NativeVmpSession::media() const { return impl_->clip; }
NativeVmpSession::Result NativeVmpSession::prepare(const VmpSendRequest& request, const VoiceClip& clip)
{
    impl_->reset();
    if (!request.self || request.self == vmp::kBroadcastTargetId || request.peer == request.self || request.conversation_channel > 7 || !clip.valid()) return Result::Invalid;
    if (!impl_->lease.supports(request.media_channel)) return Result::Unsupported;
    impl_->private_mode = request.peer && request.peer != vmp::kBroadcastTargetId;
    if (impl_->private_mode)
    {
        bool verified = false;
        try
        {
            verified = impl_->contacts && impl_->contacts->lookupVerifiedContactSecret(request.peer, impl_->secret.data());
        }
        catch (...)
        {
            verified = false;
        }
        if (!verified)
        {
            impl_->reset();
            return Result::Unverified;
        }
    }
    auto& control = impl_->outgoing;
    control.type = impl_->private_mode ? vmp::ControlType::Offer : vmp::ControlType::Announce;
    control.flags = impl_->private_mode ? vmp::ControlFlagPrivate : vmp::ControlFlagBroadcast | vmp::ControlFlagPublicBroadcast;
    control.sender_id = request.self;
    control.target_id = impl_->private_mode ? request.peer : vmp::kBroadcastTargetId;
    control.conversation_channel = request.conversation_channel;
    control.channel_index = request.media_channel;
    control.phy_profile_id = 1;
    control.encoded_media_len = static_cast<uint16_t>(clip.size);
    control.total_blocks = 1;
    control.data_start_delay_ms = impl_->private_mode ? 120 : 700;
    if (!random(&control.session_id, sizeof(control.session_id)) || !random(control.session_nonce, sizeof(control.session_nonce)) ||
        !random(&control.object_fingerprint, sizeof(control.object_fingerprint)))
    {
        impl_->reset();
        return Result::CryptoFailure;
    }
    if (impl_->private_mode)
    {
        if (!vmp::generateEphemeralKeyPair(&impl_->ephemeral) || !vmp::derivePrivateControlKey(impl_->secret.data(), control.session_nonce,
                                                                                               control.session_id, impl_->keys.control_key))
        {
            impl_->reset();
            return Result::CryptoFailure;
        }
        std::copy_n(impl_->ephemeral.public_key, sizeof(impl_->ephemeral.public_key), control.ephemeral_public_key);
    }
    if (!vmp::isValidControlFrame(control) || !impl_->transmit.prepare(clip.bytes.data(), clip.size))
    {
        impl_->reset();
        return Result::Invalid;
    }
    impl_->clip = clip;
    impl_->prepared = true;
    return Result::Prepared;
}
NativeVmpSession::Result NativeVmpSession::send(const std::atomic_bool& cancel)
{
    Result result;
    try
    {
        result = impl_->sendBody(cancel);
    }
    catch (...)
    {
        result = Result::RadioFailure;
    }
    return impl_->finish(result);
}
NativeVmpSession::Result NativeVmpSession::receive(const uint8_t* bytes, size_t size, uint32_t self,
                                                   IVmpIncomingCommit& commit, const std::atomic_bool& cancel)
{
    impl_->reset();
    Result result;
    try
    {
        result = impl_->receiveBody(bytes, size, self, commit, cancel);
    }
    catch (...)
    {
        result = Result::RadioFailure;
    }
    return impl_->finish(result);
}
} // namespace platform::linux_runtime
