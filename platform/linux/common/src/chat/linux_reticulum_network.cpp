#include "chat/linux_reticulum_network.h"
#include "chat/infra/reticulum/link_handshake_wire.h"
#include "chat/infra/reticulum/path_request_wire.h"
#include "chat/linux_reticulum_announce.h"
#include "chat/linux_reticulum_calls.h"
#include "platform/ui/reticulum_call_runtime.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace r = ::chat::reticulum;
namespace state = ::chat::lxmf::runtime;
namespace call = ::platform::ui::reticulum_call;
struct LinuxReticulumNetwork::Impl
{
    struct Interface
    {
        char id[r::kInterfaceIdMaxLen + 1]{};
        uint64_t generation = 0;
        uint32_t announced_at = 0, responded_at = 0;
        bool delivery_announced = false, call_announced = false, responded = false;
        bool automatic = false;
    };
    struct Binding
    {
        uint8_t link[16]{};
        uint8_t interface = 0;
        uint64_t generation = 0;
    };
    const platform::linux_runtime::LinuxReticulumIdentity& identity;
    ::chat::IMeshPeerDirectory& directory;
    Hooks hooks;
    LinuxReticulumTcp tcp;
    std::unique_ptr<LinuxReticulumAuto> automatic = std::make_unique<LinuxReticulumAuto>();
    LinuxReticulumIngress ingress;
    LinuxReticulumLinks links;
    LinuxReticulumCalls calls;
    LinuxReticulumAnnounce announce;
    std::array<Interface, r::kMaxTcpClientInterfaces + LinuxReticulumAuto::kMaxPeers> interfaces{};
    std::array<Binding, 8> bindings{};
    struct PendingCall
    {
        bool used = false;
        ::chat::ReticulumPeerIdentity target{};
        uint8_t destination[16]{}, link[16]{};
        uint32_t started = 0, requested_at = 0, retry_ms = 1000;
    } pending_call;
    r::ReticulumNetworkConfig config;
    uint32_t now = 0, utc = 0;
    bool anonymous = false, was_allowed = false;
    char name[33]{};
    int selected_tx = -1;
    uint64_t announcements = 0, responses = 0, rejected = 0;
    explicit Impl(const platform::linux_runtime::LinuxReticulumIdentity& identity, ::chat::IMeshPeerDirectory& directory, Hooks hooks)
        : identity(identity), directory(directory), hooks(hooks), ingress(identity, LinuxReticulumIngress::Options{}, &directory),
          links(identity), calls(links, directory), announce(identity)
    {
        ingress.setPeerProjectionSink(hooks.peers);
        ingress.setPropagationObserver(hooks.propagation, hooks.context);
        ingress.setTrackDirectoryObserver(hooks.track_directory, hooks.context);
        links.setCustomReceiver(hooks.custom, hooks.context);
        links.setCustomOutcomeSink(hooks.custom_outcome, hooks.context);
        links.setOutcomeSink(hooks.outcome, hooks.context);
        links.setIncomingHooks([](void* context, const ::chat::MeshIncomingText& text)
                               {
            auto& self = *static_cast<Impl*>(context);
            return self.hooks.incoming && self.hooks.incoming(self.hooks.context, text); },
                               [](void* context, const uint8_t* destination, uint8_t* key, bool* ignored)
                               {
                                   auto& self = *static_cast<Impl*>(context);
                                   ::chat::MeshPeerRecord peer;
                                   const auto found = self.directory.find(::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumDestinationIdentity(destination)), peer);
                                   *ignored = found.succeeded() ? peer.flags.ignored : found.code != ::chat::MeshPeerDirectoryStatusCode::NotFound;
                                   if (!found.succeeded() || !peer.reticulum.has_public_keys) return false;
                                   std::memcpy(key, peer.reticulum.enc_pub, 32);
                                   std::memcpy(key + 32, peer.reticulum.sig_pub, 32);
                                   return true;
                               },
                               this, [](void* context, const uint8_t* key, uint32_t seconds)
                               { (void)static_cast<Impl*>(context)->ingress.rememberAuthenticatedPeer(key, seconds); });
    }
    bool current() const { return !hooks.context_current || hooks.context_current(hooks.context); }
    bool transmitEnabled() const { return !hooks.transmit_allowed || hooks.transmit_allowed(hooks.context); }
    bool allowed() const { return current() && transmitEnabled(); }
    bool live(size_t index) const
    {
        if (index >= interfaces.size() || !interfaces[index].generation) return false;
        const auto& entry = interfaces[index];
        return entry.automatic ? automatic->online(entry.id, entry.generation) : tcp.online(entry.id, entry.generation);
    }
    bool sendInterface(size_t index, const uint8_t* bytes, size_t size)
    {
        if (!allowed() || !live(index)) return false;
        const auto& entry = interfaces[index];
        return entry.automatic ? automatic->send(entry.id, entry.generation, bytes, size) : tcp.send(entry.id, entry.generation, bytes, size);
    }
    bool ready() const
    {
        if (!identity.ready()) return false;
        for (size_t index = 0; index < interfaces.size(); ++index)
            if (live(index)) return true;
        return false;
    }
    Binding* binding(const uint8_t* id)
    {
        for (auto& entry : bindings)
            if (entry.interface && !std::memcmp(entry.link, id, 16)) return &entry;
        return nullptr;
    }
    Binding* bind(const uint8_t* id, size_t index)
    {
        if (auto* existing = binding(id))
            return existing->interface == index + 1 && existing->generation == interfaces[index].generation ? existing : nullptr;
        for (auto& entry : bindings)
            if (!entry.interface)
            {
                std::memcpy(entry.link, id, 16);
                entry.interface = uint8_t(index + 1);
                entry.generation = interfaces[index].generation;
                return &entry;
            }
        return nullptr;
    }
    static bool send(void* context, const uint8_t* bytes, size_t size)
    {
        auto& self = *static_cast<Impl*>(context);
        if (!self.allowed()) return false;
        r::ParsedPacket packet;
        if (!r::parsePacket(bytes, size, &packet)) return false;
        Binding* route = nullptr;
        if (packet.packet_type == r::PacketType::LinkRequest)
        {
            uint8_t id[16];
            if (!r::computeLinkRequestId(bytes, size, id)) return false;
            route = self.binding(id);
            if (!route && self.selected_tx >= 0 && self.live(size_t(self.selected_tx))) route = self.bind(id, size_t(self.selected_tx));
        }
        else if (packet.destination_type == r::DestinationType::Link) route = self.binding(packet.destination_hash);
        if (!route || !self.live(route->interface - 1) || route->generation != self.interfaces[route->interface - 1].generation) return false;
        return self.sendInterface(route->interface - 1, bytes, size);
    }
    const state::PathEntry* path(const uint8_t* destination) const
    {
        const auto* result = ingress.path(destination, now);
        return result && result->interface_id && live(result->interface_id - 1) ? result : nullptr;
    }
    void retire(size_t index)
    {
        for (auto& entry : bindings)
            if (entry.interface == index + 1)
            {
                (void)links.closeLink(entry.link, now, state::LinkCloseReason::Error, send, this);
                entry = {};
            }
        ingress.retireInterface(uint8_t(index + 1));
        interfaces[index] = {};
    }
    void resetTransport()
    {
        pending_call = {};
        calls.reset(now, send, this);
        links.reset();
        ingress.reset();
        bindings = {};
        interfaces = {};
        tcp.clear();
        automatic->clear();
    }
    void advancePendingCall()
    {
        if (!pending_call.used) return;
        const auto runtime = call::snapshot();
        if (!links.hasLink(pending_call.link) || runtime.state != call::State::Outgoing ||
            std::memcmp(runtime.link_id, pending_call.link, 16))
        {
            (void)links.closeCall(pending_call.link, now, send, this);
            pending_call = {};
            return;
        }
        ::chat::MeshPeerRecord peer;
        const auto found = directory.find(::chat::makeMeshPeerReticulumIdentity(pending_call.target), peer);
        bool valid = found.succeeded() ? !peer.flags.ignored : found.code == ::chat::MeshPeerDirectoryStatusCode::NotFound;
        const bool keys_known = found.succeeded() && peer.reticulum.has_public_keys;
        uint8_t key[64], identity_hash[16], name_hash[10], destination[16];
        if (valid && keys_known)
        {
            std::memcpy(key, peer.reticulum.enc_pub, 32);
            std::memcpy(key + 32, peer.reticulum.sig_pub, 32);
            r::computeIdentityHash(key, identity_hash);
            r::computeNameHash("lxst", "telephony", name_hash);
            r::computeDestinationHash(name_hash, identity_hash, destination);
            valid = !std::memcmp(destination, pending_call.destination, 16);
        }
        if (!valid || !allowed() || anonymous || uint32_t(now - pending_call.started) >= 30000)
        {
            (void)links.closeLink(pending_call.link, now, state::LinkCloseReason::Error, send, this);
            pending_call = {};
            return;
        }
        if (const auto* selected = path(pending_call.destination); selected && keys_known)
        {
            selected_tx = int(selected->interface_id - 1);
            const bool sent = links.transmitPendingCall(pending_call.link, now, send, this,
                                                        selected->hops, selected->hops > 1 ? selected->next_hop_transport : nullptr, key);
            selected_tx = -1;
            if (!sent) (void)links.closeLink(pending_call.link, now, state::LinkCloseReason::Error, send, this);
            pending_call = {};
        }
        else if (uint32_t(now - pending_call.requested_at) >= pending_call.retry_ms)
        {
            const bool sent = requestPath(pending_call.destination);
            pending_call.requested_at = now;
            pending_call.retry_ms = sent ? 20000 : 1000;
        }
    }
    void synchronize()
    {
        for (size_t index = 0; index < interfaces.size(); ++index)
            if (interfaces[index].generation && !live(index)) retire(index);
        const auto status = tcp.snapshot();
        for (size_t index = 0; index < status.count; ++index)
        {
            const auto& endpoint = status.endpoints[index];
            if (endpoint.state != LinuxReticulumTcp::State::Connected) continue;
            bool found = false;
            for (const auto& interface : interfaces) found |= !interface.automatic && interface.generation == endpoint.generation && !std::strcmp(interface.id, endpoint.id);
            if (found) continue;
            for (auto& interface : interfaces)
                if (!interface.generation)
                {
                    std::snprintf(interface.id, sizeof(interface.id), "%s", endpoint.id);
                    interface.generation = endpoint.generation;
                    break;
                }
        }
        const auto discovered = automatic->snapshot();
        for (size_t index = 0; index < discovered.count; ++index)
        {
            const auto& peer = discovered.peers[index];
            bool found = false;
            for (const auto& interface : interfaces) found |= interface.automatic && interface.generation == peer.generation && !std::strcmp(interface.id, peer.id);
            if (found) continue;
            for (auto& interface : interfaces)
                if (!interface.generation)
                {
                    std::snprintf(interface.id, sizeof(interface.id), "%s", peer.id);
                    interface.generation = peer.generation;
                    interface.automatic = true;
                    break;
                }
        }
    }
    bool advertise(size_t index, LinuxReticulumAnnounce::Aspect aspect, bool response = false)
    {
        if (!allowed() || anonymous || !live(index) || !announce.build(aspect, name, utc, response)) return false;
        if (!sendInterface(index, announce.data(), announce.size())) return false;
        ++announcements;
        if (response) ++responses;
        return true;
    }
    void receive(const LinuxReticulumTcp::Frame& frame)
    {
        size_t index = interfaces.size();
        for (size_t at = 0; at < interfaces.size(); ++at)
            if (interfaces[at].generation == frame.connection_generation && !std::strcmp(interfaces[at].id, frame.interface_id)) index = at;
        if (index == interfaces.size() || !identity.ready())
        {
            ++rejected;
            return;
        }
        r::ParsedPacket packet;
        if (!r::parsePacket(frame.data.data(), frame.size, &packet))
        {
            ++rejected;
            return;
        }
        if (packet.packet_type == r::PacketType::LinkRequest)
        {
            uint8_t delivery[16], telephony[16];
            announce.destination(LinuxReticulumAnnounce::Aspect::Delivery, delivery);
            announce.destination(LinuxReticulumAnnounce::Aspect::Telephony, telephony);
            const bool own_delivery = !std::memcmp(packet.destination_hash, delivery, 16);
            const bool own_call = !anonymous && call::snapshot().media_supported && !std::memcmp(packet.destination_hash, telephony, 16);
            if (packet.destination_type != r::DestinationType::Single || packet.context != 0 ||
                (!own_delivery && !own_call) || (packet.payload_len != 64 && packet.payload_len != 67))
            {
                ++rejected;
                return;
            }
            uint8_t id[16];
            if (!r::computeLinkRequestId(frame.data.data(), frame.size, id) || !bind(id, index))
            {
                ++rejected;
                return;
            }
        }
        else if (packet.destination_type == r::DestinationType::Link)
        {
            const auto* route = binding(packet.destination_hash);
            if (!route || route->interface != index + 1 || route->generation != frame.connection_generation)
            {
                ++rejected;
                return;
            }
        }
        ::chat::RxMeta meta{};
        meta.origin = ::chat::RxOrigin::External;
        meta.rx_timestamp_ms = now;
        meta.rx_timestamp_s = utc;
        meta.time_source = utc ? ::chat::RxTimeSource::DeviceUtc : ::chat::RxTimeSource::Uptime;
        if (links.receive(frame.data.data(), frame.size, now, send, this, &meta))
        {
            if (packet.packet_type == r::PacketType::LinkRequest)
            {
                uint8_t id[16];
                if (r::computeLinkRequestId(frame.data.data(), frame.size, id) && !links.hasLink(id))
                    if (auto* entry = binding(id)) *entry = {};
            }
            return;
        }
        if (ingress.receive(frame.data.data(), frame.size, now, utc, uint8_t(index + 1))) return;
        uint8_t requests[16];
        r::computePathRequestDestinationHash(requests);
        if (!anonymous && packet.packet_type == r::PacketType::Data && packet.destination_type == r::DestinationType::Plain &&
            packet.context == 0 && packet.payload_len >= 16 && !std::memcmp(packet.destination_hash, requests, 16))
        {
            auto& interface = interfaces[index];
            if (interface.responded && uint32_t(now - interface.responded_at) < 1000) return;
            uint8_t local[16];
            for (const auto aspect : {LinuxReticulumAnnounce::Aspect::Delivery, LinuxReticulumAnnounce::Aspect::Telephony})
            {
                announce.destination(aspect, local);
                if (std::memcmp(local, packet.payload, 16) || (aspect == LinuxReticulumAnnounce::Aspect::Telephony && !call::snapshot().media_supported)) continue;
                if (advertise(index, aspect, true))
                {
                    interface.responded = true;
                    interface.responded_at = now;
                }
            }
        }
    }
    bool requestPath(const uint8_t* destination)
    {
        if (!allowed() || !destination) return false;
        uint8_t tag[16];
#if defined(TRAIL_MATE_HAS_OPENSSL)
        if (RAND_bytes(tag, sizeof(tag)) != 1) return false;
#else
        return false;
#endif
        struct Broadcast
        {
            Impl& owner;
            static bool send(void* context, const uint8_t* bytes, size_t size)
            {
                auto& self = static_cast<Broadcast*>(context)->owner;
                if (!self.allowed()) return false;
                bool sent = false;
                for (size_t index = 0; index < self.interfaces.size(); ++index)
                    if (self.live(index)) sent |= self.sendInterface(index, bytes, size);
                return sent;
            }
        } broadcast{*this};
        const auto result = ingress.requestPath(destination, tag, now, Broadcast::send, &broadcast);
        return result == LinuxReticulumIngress::RequestResult::Sent || result == LinuxReticulumIngress::RequestResult::CoolingDown;
    }
    void tick(uint32_t milliseconds, uint32_t seconds)
    {
        now = milliseconds;
        utc = seconds;
        if (!current()) return;
        const bool transmit = transmitEnabled();
        if (was_allowed && !transmit)
        {
            resetTransport();
            (void)tcp.configure(config);
            (void)automatic->configure(config);
        }
        was_allowed = transmit;
        tcp.tick(now);
        automatic->tick(now);
        synchronize();
        calls.tick(now, ready() && transmit && !anonymous, send, this);
        LinuxReticulumTcp::Frame frame;
        for (unsigned drained = 0; drained < 32 && tcp.receive(frame); ++drained) receive(frame);
        for (unsigned drained = 0; drained < 32 && automatic->receive(frame); ++drained) receive(frame);
        links.tick(now);
        ingress.tick(now);
        for (auto& entry : bindings)
            if (entry.interface && !links.hasLink(entry.link)) entry = {};
        calls.tick(now, ready() && transmit && !anonymous, send, this);
        advancePendingCall();
        links.processOutbound(now, send, this);
        links.retryAccepted(now, send, this);
        for (size_t index = 0; index < interfaces.size(); ++index)
        {
            auto& interface = interfaces[index];
            if (!live(index) || !transmit || anonymous) continue;
            if (interface.delivery_announced && uint32_t(now - interface.announced_at) > 600000) interface.delivery_announced = interface.call_announced = false;
            if (!interface.delivery_announced && advertise(index, LinuxReticulumAnnounce::Aspect::Delivery))
            {
                interface.delivery_announced = true;
                interface.announced_at = now;
            }
            if (!interface.call_announced && call::snapshot().media_supported && advertise(index, LinuxReticulumAnnounce::Aspect::Telephony)) interface.call_announced = true;
        }
    }
};
LinuxReticulumNetwork::LinuxReticulumNetwork(const platform::linux_runtime::LinuxReticulumIdentity& identity, ::chat::IMeshPeerDirectory& directory, Hooks hooks)
    : impl_(std::make_unique<Impl>(identity, directory, hooks)) {}
LinuxReticulumNetwork::~LinuxReticulumNetwork() { clear(); }
bool LinuxReticulumNetwork::configure(const r::ReticulumNetworkConfig& config, bool anonymous, const char* name)
{
    std::unique_ptr<LinuxReticulumAuto> prepared;
    try
    {
        if (!impl_->automatic->configuredAs(config))
        {
            prepared = std::make_unique<LinuxReticulumAuto>();
            if (!prepared->configure(config)) return false;
        }
    }
    catch (...)
    {
        return false;
    }
    if (!impl_->tcp.configure(config)) return false;
    if (prepared) impl_->automatic.swap(prepared);
    impl_->config = config;
    impl_->anonymous = anonymous;
    char label[33]{};
    std::snprintf(label, sizeof(label), "%.*s", 32, name ? name : "");
    if (std::strcmp(label, impl_->name))
    {
        std::memcpy(impl_->name, label, sizeof(label));
        for (auto& interface : impl_->interfaces) interface.delivery_announced = interface.call_announced = false;
    }
    impl_->synchronize();
    return true;
}
void LinuxReticulumNetwork::tick(uint32_t now, uint32_t utc) { impl_->tick(now, utc); }
void LinuxReticulumNetwork::clear()
{
    impl_->resetTransport();
    impl_->config = r::ReticulumNetworkConfig{};
    impl_->was_allowed = false;
}
bool LinuxReticulumNetwork::ready() const { return impl_->ready(); }
bool LinuxReticulumNetwork::canCall() const { return ready() && impl_->allowed() && !impl_->anonymous && call::snapshot().media_supported; }
bool LinuxReticulumNetwork::hasRoute(const uint8_t destination[16]) { return destination && (impl_->path(destination) || impl_->links.hasOpenDestination(destination)); }
bool LinuxReticulumNetwork::hasActiveDelivery(const uint8_t destination[16]) { return impl_->links.hasActiveDelivery(destination); }
bool LinuxReticulumNetwork::requestPath(const uint8_t destination[16]) { return impl_->requestPath(destination); }
LinuxReticulumLinks::OpenResult LinuxReticulumNetwork::openPropagation(const uint8_t destination[16], const uint8_t verified_public_key[64])
{
    using Result = LinuxReticulumLinks::OpenResult;
    if (!destination || !verified_public_key) return Result::Invalid;
    if (!impl_->allowed() || impl_->anonymous) return Result::Pending;
    const auto* route = impl_->path(destination);
    if (!route && !impl_->links.hasPropagationDestination(destination))
    {
        (void)requestPath(destination);
        return Result::Pending;
    }
    impl_->selected_tx = route ? int(route->interface_id - 1) : -1;
    const auto result = impl_->links.openPropagation(destination, verified_public_key, impl_->now, Impl::send, impl_.get(),
                                                     route ? route->hops : 1, route && route->hops > 1 ? route->next_hop_transport : nullptr);
    impl_->selected_tx = -1;
    return result;
}
LinuxReticulumLinks::ServiceRequestResult LinuxReticulumNetwork::requestPropagation(const uint8_t destination[16], const uint8_t path_hash[16],
                                                                                    const uint8_t* packed_data, size_t size, bool data_is_nil,
                                                                                    double requested_at, uint8_t request_id[16])
{
    if (!impl_->allowed() || impl_->anonymous) return LinuxReticulumLinks::ServiceRequestResult::NotReady;
    return impl_->links.requestService(destination, path_hash, packed_data, size, data_is_nil, requested_at,
                                       impl_->now, Impl::send, impl_.get(), request_id);
}
bool LinuxReticulumNetwork::takePropagationResponse(const uint8_t destination[16], const uint8_t request_id[16], LinuxReticulumLinks::ServiceResponse& response)
{
    if (!impl_->allowed() || impl_->anonymous) return false;
    return impl_->links.takeServiceResponse(destination, request_id, response);
}
bool LinuxReticulumNetwork::propagationRequestPending(const uint8_t destination[16], const uint8_t request_id[16]) const
{
    return impl_->allowed() && !impl_->anonymous && impl_->links.serviceRequestPending(destination, request_id);
}
void LinuxReticulumNetwork::closePropagation(const uint8_t destination[16])
{
    impl_->links.closePropagation(destination, impl_->now, Impl::send, impl_.get());
}
LinuxReticulumLinks::ServiceRequestResult LinuxReticulumNetwork::queuePropagationUpload(const uint8_t destination[16], const uint8_t* data, size_t size, uint8_t hash[32])
{
    if (!impl_->allowed() || impl_->anonymous) return LinuxReticulumLinks::ServiceRequestResult::NotReady;
    return impl_->links.queuePropagationUpload(destination, data, size, impl_->now, Impl::send, impl_.get(), hash);
}
bool LinuxReticulumNetwork::takePropagationUploadResult(const uint8_t hash[32], LinuxReticulumLinks::UploadResult& result)
{
    return impl_->current() && impl_->links.takePropagationUploadResult(hash, result);
}
bool LinuxReticulumNetwork::hasPropagationUpload(const uint8_t hash[32]) const
{
    return impl_->current() && impl_->links.hasPropagationUpload(hash);
}
LinuxReticulumLinks::QueueResult LinuxReticulumNetwork::queueText(const ::chat::ReticulumPeerIdentity& target, const std::string& text, ::chat::MessageId id, double timestamp)
{
    const auto* path = impl_->path(target.destination_hash);
    if (!::chat::hasReticulumDestinationIdentity(target)) return LinuxReticulumLinks::QueueResult::Invalid;
    if (!path && !impl_->links.hasOpenDestination(target.destination_hash))
    {
        (void)requestPath(target.destination_hash);
        return LinuxReticulumLinks::QueueResult::Busy;
    }
    ::chat::MeshPeerRecord peer;
    if (!impl_->directory.find(::chat::makeMeshPeerReticulumIdentity(target), peer).succeeded() || peer.flags.ignored || !peer.reticulum.has_public_keys) return LinuxReticulumLinks::QueueResult::Invalid;
    uint8_t key[64];
    std::memcpy(key, peer.reticulum.enc_pub, 32);
    std::memcpy(key + 32, peer.reticulum.sig_pub, 32);
    impl_->selected_tx = path ? int(path->interface_id - 1) : -1;
    const auto result = impl_->links.queueText(target.destination_hash, key, text, id, timestamp, impl_->now, Impl::send, impl_.get(), path ? path->hops : 1, path && path->hops > 1 ? path->next_hop_transport : nullptr);
    impl_->selected_tx = -1;
    return result;
}
LinuxReticulumLinks::QueueResult LinuxReticulumNetwork::queueCustom(const ::chat::ReticulumPeerIdentity& target, const char* type,
                                                                    ::chat::lxmf::ByteSpan data, ::chat::MessageId id, double timestamp, uint8_t accepted_hash[32])
{
    using Result = LinuxReticulumLinks::QueueResult;
    if (!::chat::hasReticulumDestinationIdentity(target)) return Result::Invalid;
    if (!impl_->allowed()) return Result::Busy;
    const auto* path = impl_->path(target.destination_hash);
    if (!path && !impl_->links.hasOpenDestination(target.destination_hash))
    {
        (void)requestPath(target.destination_hash);
        return Result::Busy;
    }
    ::chat::MeshPeerRecord peer;
    if (!impl_->directory.find(::chat::makeMeshPeerReticulumIdentity(target), peer).succeeded() || peer.flags.ignored || !peer.reticulum.has_public_keys) return Result::Invalid;
    std::array<uint8_t, 64> key{};
    std::copy_n(peer.reticulum.enc_pub, 32, key.begin());
    std::copy_n(peer.reticulum.sig_pub, 32, key.begin() + 32);
    impl_->selected_tx = path ? int(path->interface_id - 1) : -1;
    const auto result = impl_->links.queueCustom(target.destination_hash, key.data(), type, data, id, timestamp, impl_->now,
                                                 Impl::send, impl_.get(), path ? path->hops : 1, path && path->hops > 1 ? path->next_hop_transport : nullptr, accepted_hash);
    impl_->selected_tx = -1;
    return result;
}
::chat::MeshActionResult LinuxReticulumNetwork::dial(const ::chat::ReticulumPeerIdentity& target)
{
    using Failure = ::chat::MeshOperationFailure;
    if (impl_->anonymous) return ::chat::MeshActionResult::fail(Failure::Unsupported);
    if (!canCall()) return ::chat::MeshActionResult::fail(Failure::NotReady);
    if (impl_->pending_call.used) return ::chat::MeshActionResult::fail(Failure::Busy);
    ::chat::MeshPeerRecord peer;
    const auto found = impl_->directory.find(::chat::makeMeshPeerReticulumIdentity(target), peer);
    if (!found.succeeded() && found.code != ::chat::MeshPeerDirectoryStatusCode::NotFound) return ::chat::MeshActionResult::fail(Failure::NotReady);
    if (found.succeeded() && peer.flags.ignored) return ::chat::MeshActionResult::fail(Failure::InvalidInput);
    const bool keys_known = found.succeeded() && peer.reticulum.has_public_keys;
    const bool identity_present = std::any_of(std::begin(target.identity_hash), std::end(target.identity_hash), [](uint8_t value)
                                              { return value != 0; });
    if (!keys_known && !identity_present) return ::chat::MeshActionResult::fail(Failure::PeerKeyMissing);
    uint8_t key[64], identity[16], name[10], destination[16];
    if (keys_known)
    {
        std::memcpy(key, peer.reticulum.enc_pub, 32);
        std::memcpy(key + 32, peer.reticulum.sig_pub, 32);
        r::computeIdentityHash(key, identity);
    }
    else std::memcpy(identity, target.identity_hash, 16);
    r::computeNameHash("lxst", "telephony", name);
    r::computeDestinationHash(name, identity, destination);
    const auto* path = impl_->path(destination);
    if (!path || !keys_known)
    {
        uint8_t prepared_link[16];
        const auto reserved = impl_->calls.dial(target, {1, nullptr, true}, impl_->now, Impl::send, impl_.get(), prepared_link);
        if (!reserved.ok) return reserved;
        auto& pending = impl_->pending_call;
        pending = {};
        pending.used = true;
        pending.target = target;
        std::memcpy(pending.destination, destination, 16);
        std::memcpy(pending.link, prepared_link, 16);
        pending.started = pending.requested_at = impl_->now;
        pending.retry_ms = requestPath(destination) ? 20000 : 1000;
        return reserved;
    }
    impl_->selected_tx = int(path->interface_id - 1);
    const auto result = impl_->calls.dial(target, {path->hops, path->hops > 1 ? path->next_hop_transport : nullptr}, impl_->now, Impl::send, impl_.get());
    impl_->selected_tx = -1;
    return result;
}
void LinuxReticulumNetwork::commitIncoming(const ::chat::MeshIncomingText& message, bool durable) { impl_->links.commitIncoming(message, durable, impl_->now, Impl::send, impl_.get()); }
std::vector<::chat::MessageId> LinuxReticulumNetwork::pendingOutboundIds() const { return impl_->links.pendingOutboundIds(); }
std::vector<std::array<uint8_t, 16>> LinuxReticulumNetwork::pathDestinations() const
{
    std::vector<std::array<uint8_t, 16>> result;
    impl_->ingress.forEachPath([&](const auto& path)
                               {
        if (impl_->path(path.destination_hash))
        {
            std::array<uint8_t, 16> destination; std::memcpy(destination.data(), path.destination_hash, 16); result.push_back(destination);
        } });
    return result;
}
LinuxReticulumNetwork::Snapshot LinuxReticulumNetwork::snapshot() const
{
    return {impl_->ready(), impl_->announcements, impl_->responses, impl_->rejected, impl_->tcp.snapshot(), impl_->links.statistics(), impl_->ingress.statistics(), impl_->automatic->snapshot()};
}
} // namespace trailmate::linux_app
