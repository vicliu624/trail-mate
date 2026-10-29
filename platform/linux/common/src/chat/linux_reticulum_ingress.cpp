#include "chat/linux_reticulum_ingress.h"
#include "chat/infra/reticulum/path_request_wire.h"
#include <algorithm>
#include <cstring>

namespace trailmate::linux_app
{
namespace rt = ::chat::reticulum;
namespace runtime = ::chat::lxmf::runtime;
LinuxReticulumIngress::LinuxReticulumIngress(const platform::linux_runtime::LinuxReticulumIdentity& identity)
    : LinuxReticulumIngress(identity, Options{}) {}
LinuxReticulumIngress::LinuxReticulumIngress(const platform::linux_runtime::LinuxReticulumIdentity& identity, Options options, ::chat::IMeshPeerDirectory* directory)
    : identity_(identity), options_(options), directory_(directory)
{
    options_.paths = std::clamp<size_t>(options_.paths, 1, 96);
    options_.peers = std::clamp<size_t>(options_.peers, 1, 64);
    if (directory_) directory_service_ = std::make_unique<runtime::PeerDirectoryService>(directory_);
    options_.packet_filter = std::clamp<size_t>(options_.packet_filter, 1, 128);
    options_.pending_paths = std::clamp<size_t>(options_.pending_paths, 1, 32);
    options_.request_interval_ms = std::max(1U, options_.request_interval_ms);
    options_.request_ttl_ms = std::max(options_.request_interval_ms, options_.request_ttl_ms);
}
void LinuxReticulumIngress::reset()
{
    paths_.clear();
    destinations_.clear();
    projection_ = {};
    stats_ = {};
}
void LinuxReticulumIngress::tick(uint32_t now)
{
    now = std::max(1U, now);
    runtime::TransportRuntimeLimits limits;
    limits.max_paths = options_.paths;
    limits.max_packet_filter = options_.packet_filter;
    limits.path_ttl_ms = options_.path_ttl_ms;
    limits.packet_filter_ttl_ms = options_.packet_ttl_ms;
    limits.max_pending_path_requests = options_.pending_paths;
    limits.pending_path_request_ttl_ms = options_.request_ttl_ms;
    paths_.cull(now, limits);
    stats_.paths = 0;
    paths_.forEachPath([&](const auto&)
                       { ++stats_.paths; });
    stats_.peers = destinations_.size();
}
const runtime::PeerInfo* LinuxReticulumIngress::peer(const uint8_t destination[16]) const { return destinations_.findByDestinationHash(destination); }
const runtime::PathEntry* LinuxReticulumIngress::path(const uint8_t hash[16], uint32_t now) const
{
    return paths_.findPath(hash, std::max(1U, now), options_.path_ttl_ms);
}
bool LinuxReticulumIngress::pendingPath(const uint8_t destination[16]) const
{
    const auto* request = paths_.findPendingPathRequest(destination);
    return request && !request->resolved;
}
LinuxReticulumIngress::RequestResult LinuxReticulumIngress::requestPath(const uint8_t destination[16], const uint8_t tag[16], uint32_t now, PacketSender sender, void* context)
{
    now = std::max(1U, now);
    tick(now);
    if (!identity_.ready() || !sender) return RequestResult::NotReady;
    if (!destination || !tag || !std::any_of(destination, destination + 16, [](uint8_t byte)
                                             { return byte != 0; })) return RequestResult::Invalid;
    if (paths_.pendingPathRequestCoolingDown(destination, now, options_.request_interval_ms)) return RequestResult::CoolingDown;
    uint8_t frame[rt::kPathRequestPacketSize];
    size_t size = sizeof(frame);
    if (!rt::buildPathRequest(destination, tag, frame, &size)) return RequestResult::Invalid;
    if (!sender(context, frame, size))
    {
        ++stats_.request_errors;
        return RequestResult::TxFailed;
    }
    paths_.notePendingPathRequest(destination, now, options_.pending_paths);
    ++stats_.requests_sent;
    return RequestResult::Sent;
}
bool LinuxReticulumIngress::receive(const uint8_t* bytes, size_t size, uint32_t now_ms, uint32_t now_s, uint8_t interface_id)
{
    now_ms = std::max(1U, now_ms);
    rt::ParsedPacket packet;
    if (!identity_.ready() || !bytes || size > rt::kReticulumMtu || !rt::parsePacket(bytes, size, &packet))
    {
        ++stats_.rejected;
        return false;
    }
    tick(now_ms);
    if (packet.hops < 0xff) ++packet.hops; // Same ingress hop accounting as the existing client.
    if (router_.route(packet) != runtime::PacketRoute::Announce)
    {
        ++stats_.ignored;
        return false;
    }
    uint8_t hash[32];
    rt::computePacketHash(bytes, size, hash);
    const bool requested_response = packet.context == static_cast<uint8_t>(rt::PacketContext::PathResponse) && pendingPath(packet.destination_hash);
    if (paths_.isDuplicatePacket(hash) && !requested_response)
    {
        ++stats_.replays;
        return false;
    }
    rt::ParsedAnnounce announce;
    uint8_t identity[16], destination[16];
    const auto verify = [](const uint8_t* public_key, const uint8_t* signature, const uint8_t* data, size_t length)
    { return platform::linux_runtime::LinuxReticulumIdentity::verify(public_key, data, length, signature); };
    if (verifier_.verify(packet, announce, identity, destination, verify))
    {
        ++stats_.rejected;
        return false;
    }
    if (!std::memcmp(identity, identity_.identityHash().data(), sizeof(identity)) || packet.hops > options_.max_hops)
    {
        ++stats_.ignored;
        return false;
    }
    const auto decision = runtime::evaluatePathAnnounce(paths_.findAnyPath(destination), packet.hops, announce.random_hash, now_ms, options_.path_ttl_ms);
    if (!runtime::pathAnnounceAccepted(decision))
    {
        // A solicited cached response can resolve the request after verification,
        // without replacing the cached route or extending its freshness.
        if (requested_response && decision == runtime::PathAnnounceDecision::RejectReplay && path(destination, now_ms))
        {
            paths_.resolvePendingPathRequest(destination);
            ++stats_.responses;
            observeTrackDirectory(announce, identity, destination, now_s);
            return true;
        }
        ++stats_.replays;
        return false;
    }
    if (!paths_.observeAnnouncePath(destination, packet.hops, announce.random_hash, now_ms, now_s, interface_id ? interface_id : options_.interface_id,
                                    packet.transport_id, bytes, size, options_.paths))
    {
        ++stats_.rejected;
        return false;
    }
    paths_.rememberPacket(hash, now_ms, options_.packet_filter);
    ++stats_.accepted;
    if (requested_response) ++stats_.responses;
    projection_ = {};
    projection_.announce = announce;
    std::memcpy(projection_.identity_hash, identity, sizeof(identity));
    std::memcpy(projection_.expected_destination_hash, destination, sizeof(destination));
    runtime::VerifiedAnnounceProjectionOptions projection_options;
    projection_options.now_s = now_s;
    projection_options.max_destinations = options_.peers;
    runtime::detail::projectVerifiedAnnounce(packet, projection_, destinations_, projection_options);
    observeTrackDirectory(announce, identity, destination, now_s);
    if (projection_.propagation_announce && propagation_observer_)
    {
        PropagationAnnounce event;
        std::memcpy(event.destination.data(), destination, event.destination.size());
        std::memcpy(event.identity.data(), identity, event.identity.size());
        std::memcpy(event.public_key.data(), announce.public_key, event.public_key.size());
        uint8_t delivery_name[10];
        rt::computeNameHash("lxmf", "delivery", delivery_name);
        rt::computeDestinationHash(delivery_name, identity, event.delivery.data());
        ::chat::MeshPeerRecord record;
        const auto peer_id = ::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumPeerIdentity(event.delivery.data(), identity));
        const bool ignored = directory_ && directory_->find(peer_id, record).succeeded() && record.flags.ignored;
        if (!ignored && ::chat::lxmf::decodePropagationAnnounceAppData(announce.app_data, announce.app_data_len, &event.data))
        {
            event.received_ms = now_ms;
            event.received_s = now_s;
            event.hops = packet.hops;
            event.interface_id = interface_id ? interface_id : options_.interface_id;
            try
            {
                propagation_observer_(propagation_context_, event);
            }
            catch (...)
            {
                ++stats_.projection_errors;
            }
        }
    }
    if (projection_.learned_peer && directory_service_)
    {
        const auto stored = directory_service_->recordPeer(*projection_.learned_peer, ::chat::MeshPeerSource::RuntimeRx, false, false, now_s);
        if (!stored.succeeded()) ++stats_.directory_errors;
        else if (peer_sink_)
        {
            ::chat::MeshPeerRecord record;
            const auto id = ::chat::makeMeshPeerReticulumIdentity(runtime::reticulumIdentityForPeer(*projection_.learned_peer));
            if (directory_->find(id, record).succeeded() && !record.flags.ignored)
                try
                {
                    peer_sink_->publishPeerUpdate(*projection_.learned_peer);
                }
                catch (...)
                {
                    ++stats_.projection_errors;
                }
        }
    }
    tick(now_ms);
    return true;
}
void LinuxReticulumIngress::observeTrackDirectory(const rt::ParsedAnnounce& announce, const uint8_t identity[16], const uint8_t destination[16], uint32_t now_s)
{
    if (track_directory_observer_ && announce.app_data && announce.app_data_len && announce.app_data_len <= 128)
    {
        uint8_t service[10];
        rt::computeNameHash("trailmate", "track.directory", service);
        if (!std::memcmp(service, announce.name_hash, sizeof(service)))
        {
            uint8_t delivery[16];
            rt::computeNameHash("lxmf", "delivery", service);
            rt::computeDestinationHash(service, identity, delivery);
            ::chat::MeshPeerRecord record;
            const auto peer_id = ::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumPeerIdentity(delivery, identity));
            const bool ignored = directory_ && directory_->find(peer_id, record).succeeded() && record.flags.ignored;
            const ::chat::lxmf::TrackDirectoryAnnouncementView view{{destination, 16}, {delivery, 16}, {announce.public_key, 64}, {announce.app_data, announce.app_data_len}};
            try
            {
                if (!ignored && track_directory_observer_(track_directory_context_, view))
                    (void)rememberAuthenticatedPeer(announce.public_key, now_s);
            }
            catch (...)
            {
                ++stats_.projection_errors;
            }
        }
    }
}
bool LinuxReticulumIngress::rememberAuthenticatedPeer(const uint8_t public_key[64], uint32_t now_s)
{
    if (!public_key || !directory_service_) return false;
    uint8_t hash[16], destination[16], name[10];
    ::chat::reticulum::computeIdentityHash(public_key, hash);
    ::chat::reticulum::computeNameHash("lxmf", "delivery", name);
    ::chat::reticulum::computeDestinationHash(name, hash, destination);
    auto& peer = destinations_.upsertDestinationBounded(destination, options_.peers);
    std::memcpy(peer.identity_hash, hash, 16);
    std::memcpy(peer.enc_pub, public_key, 32);
    std::memcpy(peer.sig_pub, public_key + 32, 32);
    peer.last_seen_s = now_s;
    ::chat::MeshPeerRecord existing;
    const auto id = ::chat::makeMeshPeerReticulumIdentity(::chat::makeReticulumPeerIdentity(destination, hash));
    if (!peer.display_name[0] && directory_->find(id, existing).succeeded())
        ::chat::copyMeshPeerText(peer.display_name, sizeof(peer.display_name), existing.display_name);
    const auto stored = directory_service_->recordPeer(peer, ::chat::MeshPeerSource::RuntimeRx, false, false, now_s);
    if (!stored.succeeded())
    {
        ++stats_.directory_errors;
        return false;
    }
    if (peer_sink_ && directory_->find(id, existing).succeeded() && !existing.flags.ignored)
        try
        {
            peer_sink_->publishPeerUpdate(peer);
        }
        catch (...)
        {
            ++stats_.projection_errors;
        }
    return true;
}
} // namespace trailmate::linux_app
