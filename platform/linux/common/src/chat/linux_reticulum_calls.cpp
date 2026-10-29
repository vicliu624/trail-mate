#include "chat/linux_reticulum_calls.h"
#include "chat/infra/reticulum/lxst_call_state_machine.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "platform/ui/reticulum_call_runtime.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace trailmate::linux_app
{
namespace r = ::chat::reticulum;
namespace lxst = r::lxst;
namespace machine = lxst::call;
namespace runtime = ::platform::ui::reticulum_call;
namespace
{
void destinationFor(const char* application, const char* aspect, const uint8_t identity[16], uint8_t destination[16])
{
    uint8_t name[10];
    r::computeNameHash(application, aspect, name);
    r::computeDestinationHash(name, identity, destination);
}
bool same(const uint8_t* lhs, const uint8_t* rhs) { return !std::memcmp(lhs, rhs, 16); }
runtime::Codec2Mode runtimeMode(uint16_t profile)
{
    return profile == lxst::kProfileBandwidthUltraLow ? runtime::Codec2Mode::Mode700C : profile == lxst::kProfileBandwidthVeryLow ? runtime::Codec2Mode::Mode1600
                                                                                                                                  : runtime::Codec2Mode::Mode3200;
}
} // namespace
struct LinuxReticulumCalls::Impl
{
    struct Session
    {
        bool used = false, validated = false, identified = false, closed = false, failed = false, runtime_started = false;
        LinuxReticulumLinks::CallPeer peer;
        machine::State state;
    };
    struct Packet
    {
        uint8_t link[16]{};
        std::array<uint8_t, r::kReticulumMtu> bytes{};
        size_t size = 0;
    };
    LinuxReticulumLinks& links;
    ::chat::IMeshPeerDirectory& directory;
    std::array<Session, 8> sessions{};
    std::array<Packet, 16> packets{};
    size_t head = 0, count = 0;
    uint32_t now = 0;
    bool ready = false;
    Statistics counters;
    runtime::AudioPacket audio;
    std::array<uint8_t, r::kReticulumMtu> scratch{};
    LinuxReticulumLinks::Sender sender = nullptr;
    void* sender_context = nullptr;

    Impl(LinuxReticulumLinks& links, ::chat::IMeshPeerDirectory& directory) : links(links), directory(directory)
    {
        LinuxReticulumLinks::CallHooks hooks;
        hooks.context = this;
        hooks.validated = [](void* context, const auto& peer)
        {
            auto& self = *static_cast<Impl*>(context);
            if (auto* session = self.ensure(peer)) session->validated = true;
        };
        hooks.admit = [](void* context, const auto& peer)
        {
            auto& self = *static_cast<Impl*>(context);
            if (!self.ready || !self.allowed(peer))
            {
                ++self.counters.rejected;
                return false;
            }
            auto* session = self.ensure(peer);
            if (!session)
            {
                ++self.counters.rejected;
                return false;
            }
            session->peer = peer;
            session->identified = true;
            return true;
        };
        hooks.receive = [](void* context, const auto& peer, const uint8_t* data, size_t size)
        {
            auto& self = *static_cast<Impl*>(context);
            auto* session = self.find(peer.link);
            if (!session || !self.ready || !data || !size || size > self.packets[0].bytes.size()) return false;
            if (self.count == self.packets.size())
            {
                // Control packets must not silently disappear behind audio.
                session->failed = true;
                ++self.counters.dropped;
                return false;
            }
            auto& packet = self.packets[(self.head + self.count) % self.packets.size()];
            std::memcpy(packet.link, peer.link, 16);
            std::memcpy(packet.bytes.data(), data, size);
            packet.size = size;
            ++self.count;
            return true;
        };
        hooks.closed = [](void* context, const auto& peer)
        {
            if (auto* session = static_cast<Impl*>(context)->find(peer.link)) session->closed = true;
        };
        links.setCallHooks(hooks);
    }
    Session* find(const uint8_t link[16])
    {
        for (auto& session : sessions)
            if (session.used && same(session.peer.link, link)) return &session;
        return nullptr;
    }
    Session* ensure(const LinuxReticulumLinks::CallPeer& peer)
    {
        if (auto* session = find(peer.link)) return session;
        for (auto& session : sessions)
            if (!session.used)
            {
                session = {};
                session.used = true;
                session.peer = peer;
                session.state = peer.initiator ? machine::makeCaller(lxst::kProfileBandwidthLow, now) : machine::makeCallee(lxst::kProfileBandwidthLow, now);
                return &session;
            }
        return nullptr;
    }
    bool allowed(const LinuxReticulumLinks::CallPeer& peer)
    {
        uint8_t delivery[16];
        destinationFor("lxmf", "delivery", peer.identity, delivery);
        ::chat::MeshPeerRecord record;
        const auto result = directory.find(::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumPeerIdentity(delivery, peer.identity)), record);
        // An authenticated unknown caller may ring, as in the shared client.
        // Storage failures never turn an ignored peer into an unknown caller.
        return result.code == ::chat::MeshPeerDirectoryStatusCode::NotFound || (result.succeeded() && !record.flags.ignored);
    }
    runtime::Peer runtimePeer(const Session& session, char (&name)[runtime::kDisplayNameSize])
    {
        runtime::Peer peer;
        std::memcpy(peer.link_id, session.peer.link, 16);
        std::memcpy(peer.identity_hash, session.peer.identity, 16);
        destinationFor("lxmf", "delivery", session.peer.identity, peer.destination_hash);
        peer.incoming = !session.peer.initiator;
        peer.codec2_mode = runtimeMode(session.state.profile);
        ::chat::MeshPeerRecord record;
        if (directory.find(::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumPeerIdentity(peer.destination_hash, peer.identity_hash)), record).succeeded())
            std::snprintf(name, sizeof(name), "%s", record.user_alias[0] ? record.user_alias : record.display_name);
        peer.display_name = name[0] ? name : nullptr;
        return peer;
    }
    void finish(Session& session)
    {
        if (session.runtime_started) runtime::notify_link_closed(session.peer.link);
        ++counters.closed;
        session = {};
    }
    void close(Session& session)
    {
        (void)links.closeCall(session.peer.link, now, sender, sender_context);
        finish(session);
    }
    bool signal(Session& session, uint16_t value) { return links.sendCallSignal(session.peer.link, value, now, sender, sender_context); }
    bool dispatch(Session& session, machine::Event event, unsigned depth = 0)
    {
        if (!session.used || depth > 8) return false;
        const auto transition = machine::dispatch(&session.state, event, now);
        if (!transition.accepted) return false;
        const bool closing = std::find(transition.actions, transition.actions + transition.action_count, machine::Action::CloseLink) != transition.actions + transition.action_count;
        for (size_t index = 0; index < transition.action_count; ++index)
        {
            bool ok = true;
            switch (transition.actions[index])
            {
            case machine::Action::SendAvailable:
                ok = signal(session, lxst::kStatusAvailable);
                break;
            case machine::Action::SendIdentify:
                ok = links.identifyCall(session.peer.link, now, sender, sender_context);
                break;
            case machine::Action::BeginRinging:
            {
                char name[runtime::kDisplayNameSize]{};
                const auto peer = runtimePeer(session, name);
                const bool admitted = ready && allowed(session.peer) && runtime::snapshot().media_supported && runtime::begin_incoming(peer);
                session.runtime_started = admitted;
                if (admitted)
                {
                    runtime::mark_link_active(session.peer.link);
                    runtime::set_wifi_ready(ready);
                    ++counters.admitted;
                }
                return dispatch(session, {admitted ? machine::EventType::AdmissionGranted : machine::EventType::AdmissionDenied}, depth + 1);
            }
            case machine::Action::SendRinging:
                ok = signal(session, lxst::kStatusRinging);
                break;
            case machine::Action::SendPreferredProfile:
                ok = signal(session, uint16_t(lxst::kPreferredProfile + session.state.profile));
                break;
            case machine::Action::SendPreferences:
            {
                const uint16_t values[] = {uint16_t(lxst::kPreferredProfile + session.state.profile), uint16_t(lxst::kPreferredMode + session.state.mode)};
                size_t size = scratch.size();
                ok = lxst::encodeSignalling(values, 2, scratch.data(), &size) && links.sendCall(session.peer.link, scratch.data(), size, now, sender, sender_context);
                break;
            }
            case machine::Action::SendPreferredMode:
                ok = signal(session, uint16_t(lxst::kPreferredMode + session.state.mode));
                break;
            case machine::Action::SendConnecting:
                ok = signal(session, lxst::kStatusConnecting);
                break;
            case machine::Action::PrepareMedia:
            {
                const bool prepared = runtime::prepare_media(session.peer.link);
                return dispatch(session, {prepared ? machine::EventType::MediaReady : machine::EventType::OperationFailed}, depth + 1);
            }
            case machine::Action::SendEstablished:
                ok = signal(session, lxst::kStatusEstablished);
                break;
            case machine::Action::ActivateMedia:
                ok = runtime::mark_call_active(session.peer.link);
                break;
            case machine::Action::SendBusy:
                ok = signal(session, lxst::kStatusBusy);
                break;
            case machine::Action::SendRejected:
                ok = signal(session, lxst::kStatusRejected);
                break;
            case machine::Action::CloseLink:
                close(session);
                return true;
            }
            if (!ok && !closing)
            {
                (void)dispatch(session, {machine::EventType::OperationFailed}, depth + 1);
                return false;
            }
        }
        return true;
    }
    void receive(Session& session, const Packet& packet)
    {
        lxst::DecodedPacket decoded;
        if (!lxst::decodePacket(packet.bytes.data(), packet.size, &decoded))
        {
            session.failed = true;
            return;
        }
        for (size_t index = 0; session.used && index < decoded.signal_count; ++index)
        {
            const auto value = decoded.signals[index];
            (void)dispatch(session, machine::Event::remoteSignal(value));
            if (session.used && session.runtime_started && same(runtime::snapshot().link_id, session.peer.link) && value >= lxst::kPreferredMode && value < lxst::kPreferredProfile &&
                (value - lxst::kPreferredMode == lxst::kModeFullDuplex || value - lxst::kPreferredMode == lxst::kModeHalfDuplex))
                runtime::apply_remote_duplex_mode(value - lxst::kPreferredMode == lxst::kModeHalfDuplex ? runtime::DuplexMode::Half : runtime::DuplexMode::Full);
        }
        r::audio_call::Codec2Mode expected;
        if (!session.used || session.state.phase != machine::Phase::Active || !lxst::profileToCodec2Mode(session.state.profile, &expected)) return;
        for (size_t index = 0; index < decoded.frame_count; ++index)
        {
            const auto& frame = decoded.frames[index];
            size_t size = scratch.size();
            if (frame.codec != lxst::kCodec2 || !frame.codec2_mode_valid || frame.codec2_mode != expected || !frame.encoded_len ||
                !r::audio_call::encodePayload(expected, frame.encoded, frame.encoded_len, scratch.data(), &size) ||
                !runtime::enqueue_inbound_audio(session.peer.link, scratch.data(), size)) ++counters.dropped;
            else ++counters.received_audio;
        }
    }
    void tick(uint32_t value, bool transport_ready, LinuxReticulumLinks::Sender send, void* context)
    {
        now = value;
        ready = transport_ready;
        sender = send;
        sender_context = context;
        runtime::set_wifi_ready(ready);
        uint8_t hung_up[16];
        const auto current_link = runtime::snapshot();
        if (find(current_link.link_id) && runtime::consume_hangup_request(hung_up))
        {
            if (auto* session = find(hung_up)) (void)dispatch(*session, {machine::EventType::LocalHangup});
            else runtime::notify_link_closed(hung_up);
        }
        for (auto& session : sessions)
        {
            if (!session.used) continue;
            if (session.closed)
            {
                finish(session);
                continue;
            }
            if (!ready || session.failed)
            {
                close(session);
                continue;
            }
            if (session.runtime_started)
            {
                const auto current = runtime::snapshot();
                if (!same(current.link_id, session.peer.link) || current.state == runtime::State::Ended || current.state == runtime::State::Idle)
                {
                    // A replacement runtime belongs to another call. Retire
                    // this transport before any of its queued controls run.
                    close(session);
                    continue;
                }
            }
            if (session.validated)
            {
                session.validated = false;
                (void)dispatch(session, {machine::EventType::LinkActive});
            }
            if (session.used && session.identified)
            {
                session.identified = false;
                if (session.peer.initiator)
                {
                    runtime::mark_link_active(session.peer.link);
                    (void)dispatch(session, {machine::EventType::LinkActive});
                }
                else (void)dispatch(session, {machine::EventType::RemoteIdentified});
            }
        }
        while (count)
        {
            auto& packet = packets[head];
            if (auto* session = find(packet.link)) receive(*session, packet);
            packet = {};
            head = (head + 1) % packets.size();
            --count;
        }
        for (auto& session : sessions)
        {
            if (!session.used) continue;
            if (session.closed)
            {
                finish(session);
                continue;
            }
            if (session.failed || machine::phaseTimedOut(session.state, now))
            {
                (void)dispatch(session, {machine::EventType::Timeout});
                continue;
            }
            const auto current = runtime::snapshot();
            if (session.runtime_started && same(current.link_id, session.peer.link))
            {
                if (current.state == runtime::State::Failed || current.realtime_phase == runtime::RealtimePhase::ClosingCall)
                {
                    (void)dispatch(session, {machine::EventType::OperationFailed});
                    continue;
                }
                if (!session.peer.initiator && current.accepted && session.state.phase == machine::Phase::CalleeRinging)
                    (void)dispatch(session, {machine::EventType::LocalAccepted});
                runtime::DuplexMode mode;
                if (session.used && session.state.phase == machine::Phase::Active && runtime::consume_duplex_mode_request(&mode))
                    (void)dispatch(session, {machine::EventType::LocalModeSwitch, uint16_t(mode)});
            }
            if (session.used) (void)dispatch(session, {machine::EventType::ControlRetry});
        }
        const auto media = runtime::snapshot();
        auto* media_session = find(media.link_id);
        for (unsigned sent = 0; media_session && media_session->state.phase == machine::Phase::Active && sent < 2 && runtime::dequeue_outbound_audio(&audio); ++sent)
        {
            auto* session = find(audio.link_id);
            r::audio_call::DecodedPayload decoded;
            r::audio_call::Codec2Mode expected;
            size_t size = scratch.size();
            if (!session || session->state.phase != machine::Phase::Active || !lxst::profileToCodec2Mode(session->state.profile, &expected) ||
                !r::audio_call::decodePayload(audio.data, audio.len, &decoded) || decoded.mode != expected ||
                !lxst::encodeCodec2Frames(decoded.mode, decoded.encoded, decoded.encoded_len, scratch.data(), &size))
            {
                ++counters.dropped;
                continue;
            }
            if (links.sendCall(audio.link_id, scratch.data(), size, now, sender, sender_context))
            {
                ++counters.sent_audio;
                runtime::note_tx_sent(audio.len);
            }
            else (void)dispatch(*session, {machine::EventType::OperationFailed});
        }
    }
};
LinuxReticulumCalls::LinuxReticulumCalls(LinuxReticulumLinks& links, ::chat::IMeshPeerDirectory& directory) : impl_(std::make_unique<Impl>(links, directory)) {}
LinuxReticulumCalls::~LinuxReticulumCalls()
{
    reset(impl_->now, nullptr, nullptr);
    impl_->links.setCallHooks({});
}
void LinuxReticulumCalls::tick(uint32_t now, bool ready, LinuxReticulumLinks::Sender sender, void* context) { impl_->tick(now, ready, sender, context); }
void LinuxReticulumCalls::reset(uint32_t now, LinuxReticulumLinks::Sender sender, void* context)
{
    impl_->now = now;
    impl_->sender = sender;
    impl_->sender_context = context;
    impl_->ready = false;
    for (auto& session : impl_->sessions)
        if (session.used) impl_->close(session);
    impl_->packets = {};
    impl_->head = impl_->count = 0;
}
LinuxReticulumCalls::Statistics LinuxReticulumCalls::statistics() const
{
    auto result = impl_->counters;
    for (const auto& session : impl_->sessions)
        if (session.used) ++result.sessions;
    return result;
}
::chat::MeshActionResult LinuxReticulumCalls::dial(const ::chat::ReticulumPeerIdentity& target, const Route& route,
                                                   uint32_t now, LinuxReticulumLinks::Sender sender, void* context, uint8_t* prepared_link)
{
    if (prepared_link) std::memset(prepared_link, 0, 16);
    using Failure = ::chat::MeshOperationFailure;
    using Result = ::chat::MeshActionResult;
    if (!::chat::hasReticulumDestinationIdentity(target) || std::all_of(std::begin(target.destination_hash), std::end(target.destination_hash), [](uint8_t byte)
                                                                        { return byte == 0; })) return Result::fail(Failure::InvalidInput);
    if (!impl_->ready || !runtime::snapshot().media_supported) return Result::fail(Failure::NotReady);
    if (runtime::modal_active()) return Result::fail(Failure::Busy);
    ::chat::MeshPeerRecord record;
    const auto found = impl_->directory.find(::chat::makeMeshPeerReticulumIdentity(target), record);
    if (!found.succeeded() && found.code != ::chat::MeshPeerDirectoryStatusCode::NotFound) return Result::fail(Failure::NotReady);
    if (found.succeeded() && record.flags.ignored) return Result::fail(Failure::InvalidInput);
    const bool keys_known = found.succeeded() && record.reticulum.has_public_keys;
    const bool identity_present = std::any_of(std::begin(target.identity_hash), std::end(target.identity_hash), [](uint8_t byte)
                                              { return byte != 0; });
    if (!keys_known && (!route.waiting_for_path || !identity_present)) return Result::fail(Failure::PeerKeyMissing);
    uint8_t public_key[64], identity[16], delivery[16];
    if (keys_known)
    {
        std::memcpy(public_key, record.reticulum.enc_pub, 32);
        std::memcpy(public_key + 32, record.reticulum.sig_pub, 32);
        r::computeIdentityHash(public_key, identity);
    }
    else std::memcpy(identity, target.identity_hash, 16);
    destinationFor("lxmf", "delivery", identity, delivery);
    if (!same(delivery, target.destination_hash) || (identity_present && !same(identity, target.identity_hash))) return Result::fail(Failure::InvalidInput);
    LinuxReticulumLinks::CallPeer peer;
    destinationFor("lxst", "telephony", identity, peer.destination);
    std::memcpy(peer.identity, identity, 16);
    peer.initiator = true;
    impl_->now = now;
    impl_->sender = sender;
    impl_->sender_context = context;
    const auto opened = impl_->links.openCall(peer.destination, keys_known ? public_key : nullptr, peer.link, now, sender, context, route.hops, route.next_hop, route.waiting_for_path, identity);
    if (opened != LinuxReticulumLinks::OpenResult::Opening) return Result::fail(opened == LinuxReticulumLinks::OpenResult::Full || opened == LinuxReticulumLinks::OpenResult::Pending || opened == LinuxReticulumLinks::OpenResult::Active ? Failure::Busy : opened == LinuxReticulumLinks::OpenResult::Invalid ? Failure::InvalidInput
                                                                                                                                                                                                                                                                                                                : Failure::RadioTxFailed);
    auto* session = impl_->ensure(peer);
    if (!session)
    {
        (void)impl_->links.closeCall(peer.link, now, sender, context);
        return Result::fail(Failure::Busy);
    }
    char name[runtime::kDisplayNameSize]{};
    if (!runtime::begin_outgoing(impl_->runtimePeer(*session, name)))
    {
        impl_->close(*session);
        return Result::fail(Failure::Busy);
    }
    session->runtime_started = true;
    runtime::set_wifi_ready(impl_->ready);
    ++impl_->counters.dialled;
    if (prepared_link) std::memcpy(prepared_link, peer.link, 16);
    return Result::success();
}
} // namespace trailmate::linux_app
