#include "chat/linux_reticulum_links.h"
#include "chat/infra/lxmf/lxmf_delivery_runtime.h"
#include "chat/infra/lxmf/lxmf_incoming_envelope.h"
#include "chat/infra/lxmf/lxmf_resource_wire.h"
#include "chat/infra/reticulum/link_handshake_wire.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include <algorithm>
#include <cmath>
#include <cstring>
extern "C"
{
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
}
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace r = ::chat::reticulum;
namespace state = ::chat::lxmf::runtime;
namespace
{
void wipe(void* data, size_t size)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    OPENSSL_cleanse(data, size);
#else
    auto* p = static_cast<volatile uint8_t*>(data);
    while (size--) *p++ = 0;
#endif
}
bool generateEncryptionKey(state::LinkSession& session)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (RAND_bytes(session.local_enc_priv, 32) != 1) return false;
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, session.local_enc_priv, 32);
    size_t size = 32;
    const bool ok = key && EVP_PKEY_get_raw_public_key(key, session.local_enc_pub, &size) == 1 && size == 32;
    EVP_PKEY_free(key);
    return ok;
#else
    return false;
#endif
}
bool deriveEphemeral(state::LinkSession& session)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_PKEY* local = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, session.local_enc_priv, 32);
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, session.peer_enc_pub, 32);
    EVP_PKEY_CTX* context = local ? EVP_PKEY_CTX_new(local, nullptr) : nullptr;
    uint8_t secret[32]{};
    size_t secret_size = sizeof(secret);
    const bool ok = local && peer && context &&
                    EVP_PKEY_derive_init(context) == 1 && EVP_PKEY_derive_set_peer(context, peer) == 1 &&
                    EVP_PKEY_derive(context, secret, &secret_size) == 1 && secret_size == 32 &&
                    std::any_of(std::begin(secret), std::end(secret), [](uint8_t b)
                                { return b != 0; }) &&
                    r::hkdfSha256(secret, sizeof(secret), session.link_id, 16, nullptr, 0, session.derived_key, 64);
    wipe(secret, sizeof(secret));
    EVP_PKEY_CTX_free(context);
    EVP_PKEY_free(peer);
    EVP_PKEY_free(local);
    return ok;
#else
    return false;
#endif
}
} // namespace
LinuxReticulumLinks::CallPeer LinuxReticulumLinks::callPeer(const Session& session) const
{
    CallPeer peer;
    std::memcpy(peer.link, session.link_id, 16);
    std::memcpy(peer.destination, session.remote_destination_hash, 16);
    std::memcpy(peer.identity, session.remote_identity_hash, 16);
    peer.initiator = session.initiator;
    return peer;
}
bool LinuxReticulumLinks::callAdmitted(const Session& session) const
{
    return std::any_of(admitted_calls_.begin(), admitted_calls_.end(), [&](const auto& link)
                       { return !std::memcmp(link.data(), session.link_id, 16); });
}
bool LinuxReticulumLinks::admitCall(Session& session)
{
    if (session.destination != state::LocalDestinationKind::CallAudio || !session.validated ||
        session.state != state::LinkState::Active || !session.remote_identity_known) return false;
    if (callAdmitted(session)) return true;
    auto slot = std::find_if(admitted_calls_.begin(), admitted_calls_.end(), [](const auto& link)
                             { return std::all_of(link.begin(), link.end(), [](uint8_t byte)
                                                  { return byte == 0; }); });
    if (slot == admitted_calls_.end() || !call_hooks_.admit || !call_hooks_.receive) return false;
    bool accepted = false;
    try
    {
        accepted = call_hooks_.admit(call_hooks_.context, callPeer(session));
    }
    catch (...)
    {
    }
    if (accepted) std::memcpy(slot->data(), session.link_id, 16);
    return accepted;
}
LinuxReticulumLinks::OpenResult LinuxReticulumLinks::openCall(const uint8_t destination[16], const uint8_t public_key[64], uint8_t link[16],
                                                              uint32_t now, Sender sender, void* user, uint8_t expected_hops, const uint8_t* next_hop, bool wait_for_route, const uint8_t* expected_identity)
{
    if (!link) return OpenResult::Invalid;
    std::memset(link, 0, 16);
    if (!call_hooks_.admit || !call_hooks_.receive) return OpenResult::Invalid;
    const auto result = openDestination(destination, public_key, state::LocalDestinationKind::CallAudio, now, sender, user, expected_hops, next_hop, wait_for_route, expected_identity);
    if (result == OpenResult::Opening || result == OpenResult::Pending || result == OpenResult::Active)
        if (auto* session = links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::CallAudio))
            std::memcpy(link, session->link_id, 16);
    return result;
}
bool LinuxReticulumLinks::transmitPendingCall(const uint8_t link[16], uint32_t now, Sender sender, void* user,
                                              uint8_t expected_hops, const uint8_t* next_hop, const uint8_t* verified_public_key)
{
    auto* session = link ? links_.findSession(link) : nullptr;
    if (!session || session->destination != state::LocalDestinationKind::CallAudio || !session->initiator ||
        session->state != state::LinkState::Pending || !sender || !expected_hops || expected_hops >= 128 ||
        (expected_hops > 1 && !next_hop)) return false;
    if (verified_public_key)
    {
        uint8_t identity_hash[16];
        r::computeIdentityHash(verified_public_key, identity_hash);
        if (std::memcmp(identity_hash, session->remote_identity_hash, 16)) return false;
        std::memcpy(session->peer_identity_sig_pub, verified_public_key + 32, 32);
    }
    if (std::all_of(std::begin(session->peer_identity_sig_pub), std::end(session->peer_identity_sig_pub), [](uint8_t value)
                    { return value == 0; })) return false;
    size_t size = packet_.size();
    uint8_t prepared_id[16];
    if (!buildLinkRequest(*session, expected_hops, next_hop, size) ||
        !r::computeLinkRequestId(packet_.data(), size, prepared_id) || std::memcmp(prepared_id, session->link_id, 16)) return false;
    session->expected_hops = expected_hops;
    session->created_ms = session->request_ms = now;
    session->state = state::LinkState::Handshake;
    if (sender(user, packet_.data(), size)) return true;
    ++counters_.tx_errors;
    close(*session, state::LinkCloseReason::Error, now);
    return false;
}
bool LinuxReticulumLinks::sendCall(const uint8_t link[16], const uint8_t* data, size_t size, uint32_t now, Sender sender, void* user)
{
    auto* session = link ? links_.findSession(link) : nullptr;
    if (!session || session->destination != state::LocalDestinationKind::CallAudio || !session->validated ||
        session->state != state::LinkState::Active || !callAdmitted(*session) || !data || !size || size > session->mdu) return false;
    if (!send(*session, r::PacketContext::None, data, size, true, sender, user)) return false;
    links_.touchOutbound(*session, now);
    return true;
}
bool LinuxReticulumLinks::closeCall(const uint8_t link[16], uint32_t now, Sender sender, void* user)
{
    auto* session = link ? links_.findSession(link) : nullptr;
    if (!session || session->destination != state::LocalDestinationKind::CallAudio || session->state == state::LinkState::Closed) return false;
    if (session->validated) (void)send(*session, r::PacketContext::LinkClose, session->link_id, 16, true, sender, user);
    close(*session, state::LinkCloseReason::LocalClose, now);
    return true;
}
bool LinuxReticulumLinks::sendCallSignal(const uint8_t link[16], uint16_t signal, uint32_t now, Sender sender, void* user)
{
    auto* session = link ? links_.findSession(link) : nullptr;
    if (!session || session->destination != state::LocalDestinationKind::CallAudio || !session->validated ||
        session->state != state::LinkState::Active) return false;
    uint8_t payload[16];
    size_t size = sizeof(payload);
    if (!r::lxst::encodeSignalling(signal, payload, &size) || !send(*session, r::PacketContext::None, payload, size, true, sender, user)) return false;
    links_.touchOutbound(*session, now);
    return true;
}
LinuxReticulumLinks::~LinuxReticulumLinks() { reset(); }
bool LinuxReticulumLinks::hasLink(const uint8_t link[16])
{
    const auto* session = link ? links_.findSession(link) : nullptr;
    return session && session->state != state::LinkState::Closed;
}
bool LinuxReticulumLinks::closeLink(const uint8_t link[16], uint32_t now, state::LinkCloseReason reason, Sender sender, void* user)
{
    auto* session = link ? links_.findSession(link) : nullptr;
    if (!session || session->state == state::LinkState::Closed) return false;
    if (session->validated) (void)send(*session, r::PacketContext::LinkClose, session->link_id, 16, true, sender, user);
    close(*session, reason, now);
    return true;
}
void LinuxReticulumLinks::close(Session& session, state::LinkCloseReason reason, uint32_t now)
{
    const bool notify_call = session.destination == state::LocalDestinationKind::CallAudio && session.state != state::LinkState::Closed;
    const auto peer = callPeer(session);
    for (auto& admitted : admitted_calls_)
        if (!std::memcmp(admitted.data(), session.link_id, 16)) admitted = {};
    for (auto& io : resource_io_)
        if (io.used && !std::memcmp(io.link, session.link_id, 16)) io = {};
    const auto failure = reason == state::LinkCloseReason::Timeout ? ::chat::delivery::SendFailureKind::AckTimeout : reason == state::LinkCloseReason::Error ? ::chat::delivery::SendFailureKind::RadioSendFailed
                                                                                                                                                             : ::chat::delivery::SendFailureKind::Rejected;
    links_.forEachDeferredPayload(session, [&](const auto& item)
                                  { outcome(item.message_id, ::chat::MessageStatus::Failed, failure); });
    attempts_.takeReceiptsForLink(session.link_id, [&](const auto& item)
                                  { outcome(item.message_id, ::chat::MessageStatus::Failed, failure); });
    wipe(session.local_enc_priv, sizeof(session.local_enc_priv));
    wipe(session.local_sig_priv, sizeof(session.local_sig_priv));
    wipe(session.derived_key, sizeof(session.derived_key));
    links_.closeSession(session, reason, now);
    if (notify_call && call_hooks_.closed)
        try
        {
            call_hooks_.closed(call_hooks_.context, peer);
        }
        catch (...)
        {
        }
}
void LinuxReticulumLinks::reset()
{
    incoming_ = {};
    upload_receipts_ = {};
    resource_io_ = {};
    resource_cursor_ = 0;
    resource_turn_ = false;
    links_.forEachSession([&](Session& session)
                          { close(session, state::LinkCloseReason::LocalClose, 0); });
    links_.clear();
    attempts_.clear();
    custom_message_ids_.fill(0);
    wipe(plain_.data(), plain_.size());
    wipe(token_.data(), token_.size());
    counters_ = {};
}
LinuxReticulumLinks::Statistics LinuxReticulumLinks::statistics() const
{
    auto result = counters_;
    for (const auto& pending : incoming_)
        if (pending.used) ++result.pending_incoming;
    result.sessions = links_.size();
    result.pending_outbound = pendingOutboundIds().size();
    links_.forEachSession([&](const Session& session)
                          { if (session.state == state::LinkState::Active) ++result.active; });
    return result;
}
bool LinuxReticulumLinks::send(Session& session, r::PacketContext context, const uint8_t* payload, size_t len, bool encrypted, Sender sender, void* user)
{
    if (!sender) return false;
    return preparePacket(session, context, payload, len, encrypted) && transmitPrepared(sender, user);
}
bool LinuxReticulumLinks::preparePacket(Session& session, r::PacketContext context, const uint8_t* payload, size_t len, bool encrypted)
{
    if (encrypted)
    {
        uint8_t iv[r::kTokenIvSize]{};
#if defined(TRAIL_MATE_HAS_OPENSSL)
        if (RAND_bytes(iv, sizeof(iv)) != 1) return false;
#else
        return false;
#endif
        size_t token_len = token_.size();
        if (!r::tokenEncrypt(session.derived_key, iv, payload, len, token_.data(), &token_len)) return false;
        payload = token_.data();
        len = token_len;
    }
    size_t size = packet_.size();
    const auto type = context == r::PacketContext::LrProof ? r::PacketType::Proof : r::PacketType::Data;
    if (!r::buildHeader1Packet(type, r::DestinationType::Link, context, false, session.link_id, payload, len, packet_.data(), &size) || size > session.mtu) return false;
    last_packet_size_ = size;
    return true;
}
bool LinuxReticulumLinks::transmitPrepared(Sender sender, void* user)
{
    if (sender && sender(user, packet_.data(), last_packet_size_)) return true;
    ++counters_.tx_errors;
    return false;
}
bool LinuxReticulumLinks::proof(Session& session, Sender sender, void* user)
{
    uint8_t signed_data[83]{}, payload[99]{};
    std::memcpy(signed_data, session.link_id, 16);
    std::memcpy(signed_data + 16, session.local_enc_pub, 32);
    std::memcpy(signed_data + 48, identity_.publicKey().data() + 32, 32);
    r::buildLinkSignalling(session.mtu, signed_data + 80);
    if (!identity_.sign(signed_data, sizeof(signed_data), payload)) return false;
    std::memcpy(payload + 64, session.local_enc_pub, 32);
    std::memcpy(payload + 96, signed_data + 80, 3);
    if (!send(session, r::PacketContext::LrProof, payload, sizeof(payload), false, sender, user)) return false;
    ++counters_.proofs;
    return true;
}
bool LinuxReticulumLinks::request(const uint8_t* raw, size_t len, const r::ParsedPacket& packet, uint32_t now, Sender sender, void* user)
{
    uint8_t call_name[10], call_destination[16];
    r::computeNameHash("lxst", "telephony", call_name);
    r::computeDestinationHash(call_name, identity_.identityHash().data(), call_destination);
    const bool call = !std::memcmp(packet.destination_hash, call_destination, 16);
    if (!identity_.ready() || packet.destination_type != r::DestinationType::Single || packet.context != 0 ||
        (call ? !call_hooks_.admit || !call_hooks_.receive : std::memcmp(packet.destination_hash, identity_.destinationHash().data(), 16) != 0) ||
        (packet.transport_id && std::memcmp(packet.transport_id, identity_.identityHash().data(), 16)) ||
        (packet.payload_len != 64 && packet.payload_len != 67)) return false;
    uint16_t mtu = r::kReticulumMtu;
    if (packet.payload_len == 67)
    {
        const auto* p = packet.payload + 64;
        if ((p[0] >> 5) != 1) return false;
        const uint32_t offered = (uint32_t(p[0] & 0x1f) << 16) | (uint32_t(p[1]) << 8) | p[2];
        mtu = static_cast<uint16_t>(std::min<uint32_t>(offered, r::kReticulumMtu));
    }
    if (mtu < r::kPacketHeader1Size + 99 || !r::linkPayloadMdu(mtu)) return false;
    uint8_t id[16];
    if (!r::computeLinkRequestId(raw, len, id)) return false;
    if (auto* existing = links_.findSession(id))
        return existing->state == state::LinkState::Handshake && proof(*existing, sender, user);
    tick(now);
    if (links_.size() >= 8) return false; // Untrusted requests never evict live sessions.
    state::LinkSessionSpec spec;
    spec.destination = call ? state::LocalDestinationKind::CallAudio : state::LocalDestinationKind::Delivery;
    spec.link_id = id;
    spec.local_sig_pub = identity_.publicKey().data() + 32;
    spec.peer_enc_pub = packet.payload;
    spec.peer_link_sig_pub = packet.payload + 32;
    spec.now_ms = now;
    spec.mtu = mtu;
    spec.mdu = r::linkPayloadMdu(mtu);
    spec.state = state::LinkState::Handshake;
    spec.expected_hops = static_cast<uint8_t>(packet.hops + 1U);
    auto* session = links_.openSession(8, spec);
    if (!session) return false;
    if (generateEncryptionKey(*session) && deriveEphemeral(*session) && proof(*session, sender, user)) return true;
    close(*session, state::LinkCloseReason::Error, now);
    links_.discardSession(*session);
    return false;
}
LinuxReticulumLinks::OpenResult LinuxReticulumLinks::open(const uint8_t destination[16], const uint8_t public_key[64],
                                                          uint32_t now, Sender sender, void* user, uint8_t expected_hops, const uint8_t* next_hop)
{
    return openDestination(destination, public_key, state::LocalDestinationKind::Delivery, now, sender, user, expected_hops, next_hop);
}
LinuxReticulumLinks::OpenResult LinuxReticulumLinks::openPropagation(const uint8_t destination[16], const uint8_t public_key[64],
                                                                     uint32_t now, Sender sender, void* user, uint8_t expected_hops, const uint8_t* next_hop)
{
    return openDestination(destination, public_key, state::LocalDestinationKind::Propagation, now, sender, user, expected_hops, next_hop);
}
LinuxReticulumLinks::OpenResult LinuxReticulumLinks::openDestination(const uint8_t destination[16], const uint8_t public_key[64],
                                                                     state::LocalDestinationKind kind, uint32_t now, Sender sender, void* user, uint8_t expected_hops, const uint8_t* next_hop, bool wait_for_route, const uint8_t* expected_identity)
{
    if (!identity_.ready() || !destination || (!public_key && (!wait_for_route || !expected_identity)) || !sender || !expected_hops || expected_hops >= 128 ||
        (expected_hops > 1 && !next_hop) || (wait_for_route && kind != state::LocalDestinationKind::CallAudio)) return OpenResult::Invalid;
    uint8_t identity_hash[16], name_hash[10], bound_destination[16];
    if (public_key) r::computeIdentityHash(public_key, identity_hash);
    else std::memcpy(identity_hash, expected_identity, 16);
    if (expected_identity && std::memcmp(expected_identity, identity_hash, 16)) return OpenResult::Invalid;
    if (std::all_of(std::begin(identity_hash), std::end(identity_hash), [](uint8_t value)
                    { return value == 0; })) return OpenResult::Invalid;
    r::computeNameHash(kind == state::LocalDestinationKind::CallAudio ? "lxst" : "lxmf",
                       kind == state::LocalDestinationKind::CallAudio ? "telephony" : kind == state::LocalDestinationKind::Propagation ? "propagation"
                                                                                                                                       : "delivery",
                       name_hash);
    r::computeDestinationHash(name_hash, identity_hash, bound_destination);
    if (std::memcmp(destination, bound_destination, 16) || !std::memcmp(identity_hash, identity_.identityHash().data(), 16))
        return OpenResult::Invalid;
    if (auto* existing = links_.findOpenSessionByDestination(destination, kind))
    {
        if (std::memcmp(existing->remote_identity_hash, identity_hash, 16) ||
            (public_key && std::memcmp(existing->peer_identity_sig_pub, public_key + 32, 32))) return OpenResult::Invalid;
        return existing->state == state::LinkState::Active ? OpenResult::Active : OpenResult::Pending;
    }
    tick(now);
    if (links_.size() >= 8) return OpenResult::Full;
    state::LinkSessionSpec spec;
    spec.destination = kind;
    spec.remote_destination_hash = destination;
    spec.remote_identity_hash = identity_hash;
    spec.peer_identity_sig_pub = public_key ? public_key + 32 : nullptr;
    spec.now_ms = now;
    spec.expected_hops = expected_hops;
    spec.mtu = r::kReticulumMtu;
    spec.mdu = r::linkPayloadMdu(spec.mtu);
    spec.initiator = true;
    spec.remote_identity_known = true;
    spec.state = wait_for_route ? state::LinkState::Pending : state::LinkState::Handshake;
    auto* session = links_.openSession(8, spec);
    if (!session) return OpenResult::Full;
    bool ready = generateEncryptionKey(*session);
    uint8_t seed[32]{};
#if defined(TRAIL_MATE_HAS_OPENSSL)
    ready = ready && RAND_bytes(seed, sizeof(seed)) == 1;
#else
    ready = false;
#endif
    if (ready) ed25519_create_keypair(session->local_sig_pub, session->local_sig_priv, seed);
    wipe(seed, sizeof(seed));
    size_t size = packet_.size();
    ready = ready && buildLinkRequest(*session, expected_hops, next_hop, size);
    ready = ready && r::computeLinkRequestId(packet_.data(), size, session->link_id);
    if (ready && (wait_for_route || sender(user, packet_.data(), size))) return OpenResult::Opening;
    ++counters_.tx_errors;
    close(*session, state::LinkCloseReason::Error, now);
    links_.discardSession(*session);
    return OpenResult::TxFailed;
}
bool LinuxReticulumLinks::buildLinkRequest(const Session& session, uint8_t expected_hops, const uint8_t* next_hop, size_t& size)
{
    uint8_t payload[67];
    std::memcpy(payload, session.local_enc_pub, 32);
    std::memcpy(payload + 32, session.local_sig_pub, 32);
    r::buildLinkSignalling(session.mtu, payload + 64);
    return expected_hops > 1
               ? r::buildHeader2Packet(r::PacketType::LinkRequest, r::DestinationType::Single, r::PacketContext::None, false,
                                       next_hop, session.remote_destination_hash, payload, sizeof(payload), packet_.data(), &size)
               : r::buildHeader1Packet(r::PacketType::LinkRequest, r::DestinationType::Single, r::PacketContext::None, false,
                                       session.remote_destination_hash, payload, sizeof(payload), packet_.data(), &size);
}
bool LinuxReticulumLinks::acceptProof(Session& session, const r::ParsedPacket& packet, uint32_t now, Sender sender, void* user)
{
    if (!session.initiator || session.state != state::LinkState::Handshake ||
        (packet.payload_len != 96 && packet.payload_len != 99)) return false;
    uint16_t mtu = r::kReticulumMtu;
    if (packet.payload_len == 99)
    {
        const auto* signal = packet.payload + 96;
        if ((signal[0] >> 5) != 1) return false;
        const uint32_t offered = (uint32_t(signal[0] & 0x1f) << 16) | (uint32_t(signal[1]) << 8) | signal[2];
        mtu = static_cast<uint16_t>(std::min<uint32_t>(offered, r::kReticulumMtu));
    }
    if (mtu < r::kPacketHeader1Size + 99 || mtu > session.mtu) return false;
    uint8_t domain[83];
    std::memcpy(domain, session.link_id, 16);
    std::memcpy(domain + 16, packet.payload + 64, 32);
    std::memcpy(domain + 48, session.peer_identity_sig_pub, 32);
    const size_t signalling_size = packet.payload_len - 96;
    if (signalling_size) std::memcpy(domain + 80, packet.payload + 96, signalling_size);
    if (!platform::linux_runtime::LinuxReticulumIdentity::verify(session.peer_identity_sig_pub, domain, 80 + signalling_size, packet.payload)) return false;
    std::memcpy(session.peer_enc_pub, packet.payload + 64, 32);
    if (!deriveEphemeral(session)) return false;
    session.mtu = mtu;
    session.mdu = r::linkPayloadMdu(mtu);
    const double rtt = double(uint32_t(now - session.request_ms)) / 1000.0;
    uint64_t bits;
    std::memcpy(&bits, &rtt, sizeof(bits));
    uint8_t rtt_payload[9] = {0xcb};
    for (size_t i = 0; i < 8; ++i) rtt_payload[8 - i] = static_cast<uint8_t>(bits >> (i * 8));
    if (!send(session, r::PacketContext::LrRtt, rtt_payload, sizeof(rtt_payload), true, sender, user))
    {
        close(session, state::LinkCloseReason::Error, now);
        return false;
    }
    links_.markSessionValidatedActive(session, static_cast<float>(rtt), 15000);
    links_.touchOutbound(session, now);
    links_.touchInbound(session, now);
    session.local_identity_sent = identify(session, sender, user);
    if (session.destination == state::LocalDestinationKind::CallAudio && (!session.local_identity_sent || !admitCall(session)))
    {
        closeCall(session.link_id, now, sender, user);
        return false;
    }
    return true;
}
bool LinuxReticulumLinks::identify(Session& session, Sender sender, void* user)
{
    uint8_t payload[128], domain[80];
    std::memcpy(payload, identity_.publicKey().data(), 64);
    std::memcpy(domain, session.link_id, 16);
    std::memcpy(domain + 16, payload, 64);
    return identity_.sign(domain, sizeof(domain), payload + 64) &&
           send(session, r::PacketContext::LinkIdentify, payload, sizeof(payload), true, sender, user);
}
bool LinuxReticulumLinks::identifyCall(const uint8_t link[16], uint32_t now, Sender sender, void* user)
{
    auto* session = link ? links_.findSession(link) : nullptr;
    if (!session || session->destination != state::LocalDestinationKind::CallAudio || !session->validated ||
        session->state != state::LinkState::Active || !identify(*session, sender, user)) return false;
    session->local_identity_sent = true;
    links_.touchOutbound(*session, now);
    return true;
}
void LinuxReticulumLinks::setIncomingHooks(TextReceiver receiver, PeerResolver resolver, void* context, PeerObserver observer)
{
    text_receiver_ = receiver;
    peer_resolver_ = resolver;
    peer_observer_ = observer;
    incoming_context_ = context;
}
bool LinuxReticulumLinks::packetProof(Session& session, const uint8_t hash[32], Sender sender, void* user)
{
    if (!sender || !session.validated || session.state != state::LinkState::Active) return false;
    uint8_t payload[96];
    std::memcpy(payload, hash, 32);
    if (session.initiator) ed25519_sign(payload + 32, hash, 32, session.local_sig_pub, session.local_sig_priv);
    else if (!identity_.sign(hash, 32, payload + 32)) return false;
    size_t size = packet_.size();
    if (!r::buildHeader1Packet(r::PacketType::Proof, r::DestinationType::Link, r::PacketContext::None, false,
                               session.link_id, payload, sizeof(payload), packet_.data(), &size)) return false;
    if (!sender(user, packet_.data(), size))
    {
        ++counters_.tx_errors;
        return false;
    }
    ++counters_.delivery_proofs;
    return true;
}
void LinuxReticulumLinks::commitIncoming(const ::chat::MeshIncomingText& message, bool durable, uint32_t now, Sender sender, void* user)
{
    if (!::chat::hasReticulumLxmfMessageHash(message) || !::chat::hasReticulumDestinationIdentity(message.reticulum_identity)) return;
    for (auto& pending : incoming_)
    {
        if (!pending.used || std::memcmp(pending.message_hash, message.reticulum_lxmf_hash, 32) ||
            std::memcmp(pending.destination, message.reticulum_identity.destination_hash, 16) ||
            std::memcmp(pending.identity, message.reticulum_identity.identity_hash, 16)) continue;
        if (!durable)
        {
            if (pending.resource)
            {
                for (auto& io : resource_io_)
                    if (io.used && !io.outgoing && !std::memcmp(io.link, pending.link, 16) && !std::memcmp(io.hash, pending.resource_hash, 32))
                        io.last_request = now;
            }
            pending = {};
            continue;
        }
        pending.durable = true;
        pending.last_try = now;
        auto* session = links_.findSession(pending.link);
        if (session && acknowledge(*session, pending, now, sender, user))
        {
            links_.touchOutbound(*session, now);
            pending = {};
        }
    }
}
void LinuxReticulumLinks::retryAccepted(uint32_t now, Sender sender, void* user)
{
    for (auto& pending : incoming_)
    {
        if (!pending.used || !pending.durable || uint32_t(now - pending.last_try) < 1000) continue;
        pending.last_try = now;
        auto* session = links_.findSession(pending.link);
        if (!session || session->state == state::LinkState::Closed) pending = {};
        else if (acknowledge(*session, pending, now, sender, user))
        {
            links_.touchOutbound(*session, now);
            pending = {};
        }
    }
}
bool LinuxReticulumLinks::decodePropagatedText(const uint8_t* message, size_t size, const ::chat::RxMeta* meta,
                                               ::chat::MeshIncomingText& incoming, uint8_t message_hash[32], uint8_t* missing_author)
{
    if (missing_author) std::memset(missing_author, 0, 16);
    constexpr size_t prefix = r::kTruncatedHashSize + r::kEncryptionPublicKeySize;
    if (!identity_.ready() || !message || !message_hash || size <= prefix + r::kTokenOverhead ||
        size > state::kMaxAdvertisedResourceBytes || std::memcmp(message, identity_.destinationHash().data(), 16)) return false;
    struct Secret
    {
        uint8_t shared[32]{}, key[r::kDerivedTokenKeySize]{};
        ~Secret()
        {
            volatile uint8_t* bytes = shared;
            for (size_t i = 0; i < sizeof(shared); ++i) bytes[i] = 0;
            bytes = key;
            for (size_t i = 0; i < sizeof(key); ++i) bytes[i] = 0;
        }
    } secret;
    if (!identity_.sharedSecret(message + r::kTruncatedHashSize, secret.shared) ||
        !r::hkdfSha256(secret.shared, sizeof(secret.shared), identity_.identityHash().data(), 16,
                       nullptr, 0, secret.key, sizeof(secret.key))) return false;
    state::ResourcePayloadBuffer envelope(size - prefix + r::kTruncatedHashSize);
    size_t plaintext_size = envelope.size() - r::kTruncatedHashSize;
    if (!r::tokenDecrypt(secret.key, message + prefix, size - prefix, envelope.data() + r::kTruncatedHashSize, &plaintext_size)) return false;
    std::memcpy(envelope.data(), identity_.destinationHash().data(), r::kTruncatedHashSize);
    envelope.resize(r::kTruncatedHashSize + plaintext_size);
    ::chat::MeshIncomingText decoded;
    // No session identity fallback: the relay is never the original author.
    if (!materialiseIncomingText(nullptr, envelope.data(), envelope.size(), meta, decoded, missing_author)) return false;
    std::memcpy(message_hash, decoded.reticulum_lxmf_hash, 32);
    incoming = std::move(decoded);
    return true;
}
bool LinuxReticulumLinks::authenticateDelivery(const Session* session, const uint8_t* payload, size_t size,
                                               AuthenticatedDelivery& authenticated, uint8_t* missing_author)
{
    if (!identity_.ready() || !payload || size > state::kMaxAdvertisedResourceBytes) return false;
    auto& envelope = authenticated.envelope;
    if (!::chat::lxmf::unpackMessageEnvelopeForDestination(identity_.destinationHash().data(), payload, size, &envelope) ||
        std::memcmp(envelope.destination_hash, identity_.destinationHash().data(), 16)) return false;
    uint8_t public_key[64]{}, expected[16], name[10];
    auto* peer_identity = authenticated.peer_identity.data();
    bool ignored = false;
    bool resolved = false;
    try
    {
        resolved = peer_resolver_ && peer_resolver_(incoming_context_, envelope.source_hash, public_key, &ignored);
    }
    catch (...)
    {
        return false;
    }
    if (ignored) return false;
    const uint8_t* signing_key = nullptr;
    r::computeNameHash("lxmf", "delivery", name);
    if (resolved)
    {
        r::computeIdentityHash(public_key, peer_identity);
        r::computeDestinationHash(name, peer_identity, expected);
        if (std::memcmp(expected, envelope.source_hash, 16)) return false;
        signing_key = public_key + 32;
    }
    else if (session && session->remote_identity_known)
    {
        r::computeDestinationHash(name, session->remote_identity_hash, expected);
        if (std::memcmp(expected, envelope.source_hash, 16)) return false;
        std::memcpy(peer_identity, session->remote_identity_hash, 16);
        signing_key = session->peer_identity_sig_pub;
    }
    if (!signing_key)
    {
        if (missing_author) std::memcpy(missing_author, envelope.source_hash, 16);
        return false;
    }
    state::ResourcePayloadBuffer signed_data(envelope.packed_payload.size() + 64);
    size_t signed_size = signed_data.size();
    auto* message_hash = authenticated.message_hash.data();
    if (!::chat::lxmf::buildSignedPart(envelope.destination_hash, envelope.source_hash,
                                       envelope.packed_payload.data(), envelope.packed_payload.size(), signed_data.data(), &signed_size, message_hash) ||
        !platform::linux_runtime::LinuxReticulumIdentity::verify(signing_key, signed_data.data(), signed_size, envelope.signature)) return false;
    auto& decoded = authenticated.payload;
    if (!::chat::lxmf::unpackTextPayload(envelope.packed_payload.data(), envelope.packed_payload.size(), &decoded) ||
        !std::isfinite(decoded.timestamp) || decoded.timestamp < 0 || decoded.timestamp > UINT32_MAX ||
        decoded.content.find('\0') != std::string::npos) return false;
    return true;
}
bool LinuxReticulumLinks::materialiseVerifiedText(const AuthenticatedDelivery& authenticated, const ::chat::RxMeta* meta,
                                                  ::chat::MeshIncomingText& incoming) const
{
    const auto& envelope = authenticated.envelope;
    const auto& decoded = authenticated.payload;
    const auto* peer_identity = authenticated.peer_identity.data();
    const auto* message_hash = authenticated.message_hash.data();
    if (!decoded.fields_empty) return false;
    ::chat::lxmf::DecodedAppData app;
    if (::chat::lxmf::decodeAppDataPayload(envelope.packed_payload.data(), envelope.packed_payload.size(), &app)) return false;
    state::LxmfDeliveryContext context;
    context.peer_node_id = r::nodeIdFromDestinationHash(envelope.source_hash);
    context.local_node_id = identity_.nodeId();
    context.message_id = (uint32_t(message_hash[28]) << 24) | (uint32_t(message_hash[29]) << 16) |
                         (uint32_t(message_hash[30]) << 8) | message_hash[31];
    context.has_message_hash = true;
    std::memcpy(context.message_hash, message_hash, 32);
    context.peer_identity = ::chat::makeReticulumPeerIdentity(envelope.source_hash, peer_identity);
    context.timestamp_s = meta && meta->rx_timestamp_s ? meta->rx_timestamp_s : static_cast<uint32_t>(decoded.timestamp);
    if (meta) context.rx_meta = *meta;
    state::LxmfMaterialisedText delivery;
    if (!state::materialiseLxmfTextDelivery(decoded, context, &delivery)) return false;
    incoming = std::move(delivery.incoming);
    return true;
}
bool LinuxReticulumLinks::materialiseIncomingText(const Session* session, const uint8_t* payload, size_t size,
                                                  const ::chat::RxMeta* meta, ::chat::MeshIncomingText& incoming, uint8_t* missing_author)
{
    AuthenticatedDelivery authenticated;
    return authenticateDelivery(session, payload, size, authenticated, missing_author) && materialiseVerifiedText(authenticated, meta, incoming);
}
bool LinuxReticulumLinks::incomingText(Session& session, const uint8_t* payload, size_t size,
                                       const uint8_t* raw, size_t raw_size, uint32_t now, const ::chat::RxMeta* meta,
                                       const uint8_t* resource_hash, const uint8_t* resource_proof)
{
    if ((!text_receiver_ && !custom_receiver_) || (!resource_hash && size > session.mdu)) return false;
    AuthenticatedDelivery authenticated;
    if (!authenticateDelivery(&session, payload, size, authenticated)) return false;
    ::chat::lxmf::ByteSpan custom_type, custom_data;
    const auto custom_result = ::chat::lxmf::extractCustomData(authenticated.payload, &custom_type, &custom_data);
    const bool custom = custom_result == ::chat::lxmf::CustomDataResult::Valid;
    if (custom_result == ::chat::lxmf::CustomDataResult::Invalid ||
        (custom && (!custom_receiver_ || !custom_type.data || !custom_type.size || custom_type.size > 64 ||
                    !custom_data.data || !custom_data.size || custom_data.size > 8192))) return false;
    ::chat::MeshIncomingText delivery;
    if (!custom && (!text_receiver_ || !materialiseVerifiedText(authenticated, meta, delivery))) return false;
    const auto* message_hash = authenticated.message_hash.data();
    PendingIncoming* slot = nullptr;
    for (auto& pending : incoming_)
    {
        if (pending.used && !std::memcmp(pending.link, session.link_id, 16) && !std::memcmp(pending.message_hash, message_hash, 32))
        {
            pending.resource = resource_hash != nullptr;
            if (resource_hash)
            {
                std::memcpy(pending.resource_hash, resource_hash, 32);
                std::memcpy(pending.packet_hash, resource_proof, 32);
            }
            else r::computePacketHash(raw, raw_size, pending.packet_hash);
            return true;
        }
        if (!pending.used && !slot) slot = &pending;
    }
    if (!slot) return false;
    if (raw && raw_size > 1)
    {
        delivery.rx_meta.hop_count = static_cast<uint8_t>(raw[1] + 1U);
        delivery.rx_meta.wire_flags = raw[0];
    }
    slot->used = true;
    slot->created = now;
    std::memcpy(slot->link, session.link_id, 16);
    std::memcpy(slot->destination, authenticated.envelope.source_hash, 16);
    std::memcpy(slot->identity, authenticated.peer_identity.data(), 16);
    std::memcpy(slot->message_hash, message_hash, 32);
    slot->resource = resource_hash != nullptr;
    if (resource_hash)
    {
        std::memcpy(slot->resource_hash, resource_hash, 32);
        std::memcpy(slot->packet_hash, resource_proof, 32);
    }
    else r::computePacketHash(raw, raw_size, slot->packet_hash);
    bool accepted = false;
    try
    {
        if (custom)
        {
            const ::chat::lxmf::CustomDeliveryView view{{authenticated.envelope.source_hash, 16},
                                                        {authenticated.envelope.destination_hash, 16},
                                                        {message_hash, 32},
                                                        custom_data};
            accepted = custom_receiver_(custom_context_, custom_type, view);
        }
        else accepted = text_receiver_(incoming_context_, delivery);
    }
    catch (...)
    {
        accepted = false;
    }
    if (!accepted)
    {
        *slot = {};
        return false;
    }
    if (custom)
    {
        slot->durable = true;
        slot->last_try = now - 1000; // Eligible for the normal proof/retry pump.
        ++counters_.incoming_custom;
    }
    else ++counters_.incoming_texts;
    return true;
}
bool LinuxReticulumLinks::hasOpenDestination(const uint8_t destination[16])
{
    return destination && links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Delivery);
}
bool LinuxReticulumLinks::hasActiveDelivery(const uint8_t destination[16])
{
    return destination && links_.findActiveSessionByDestination(destination, state::LocalDestinationKind::Delivery);
}
void LinuxReticulumLinks::outcome(::chat::MessageId id, ::chat::MessageStatus status, ::chat::delivery::SendFailureKind failure)
{
    auto sink = outcome_sink_;
    auto* context = outcome_context_;
    const auto custom = std::find(custom_message_ids_.begin(), custom_message_ids_.end(), id);
    if (id && custom != custom_message_ids_.end())
    {
        sink = custom_outcome_sink_;
        context = custom_outcome_context_;
        if (status == ::chat::MessageStatus::Delivered || status == ::chat::MessageStatus::Failed) *custom = 0;
    }
    if (id && sink)
    {
        try
        {
            sink(context, id, status, failure);
        }
        catch (...)
        {
        }
    }
}
std::vector<::chat::MessageId> LinuxReticulumLinks::pendingOutboundIds() const
{
    std::vector<::chat::MessageId> ids;
    attempts_.forEachReceipt([&](const auto& receipt)
                             { if (receipt.message_id) ids.push_back(receipt.message_id); });
    links_.forEachSession([&](const Session& session)
                          { links_.forEachDeferredPayload(session, [&](const auto& item)
                                                          { if (item.message_id) ids.push_back(item.message_id); }); });
    return ids;
}
bool LinuxReticulumLinks::textFitsSinglePacket(const std::string& text)
{
    if (text.empty() || text.size() > r::kReticulumMtu || text.find('\0') != std::string::npos) return false;
    std::array<uint8_t, r::kReticulumMtu> payload{};
    size_t size = payload.size();
    return ::chat::lxmf::encodeTextPayload(0, "", text.c_str(), payload.data(), &size) &&
           size + 2 * r::kTruncatedHashSize + r::kSignatureSize <= r::linkPayloadMdu(r::kReticulumMtu);
}
bool LinuxReticulumLinks::textFitsTransport(const std::string& text)
{
    return !text.empty() && text.size() < state::kMaxAdvertisedResourceBytes - 128 && text.find('\0') == std::string::npos;
}
LinuxReticulumLinks::QueueResult LinuxReticulumLinks::queueText(const uint8_t destination[16], const uint8_t public_key[64],
                                                                const std::string& text, ::chat::MessageId id, double timestamp, uint32_t now, Sender sender, void* user,
                                                                uint8_t expected_hops, const uint8_t* next_hop)
{
    if (!id || !identity_.ready() || !destination || !public_key || !textFitsTransport(text) ||
        !std::isfinite(timestamp) || timestamp < 0 || timestamp > UINT32_MAX) return QueueResult::Invalid;
    const auto pending = pendingOutboundIds();
    if (std::find(pending.begin(), pending.end(), id) != pending.end()) return QueueResult::Pending;
    if (pending.size() >= 32) return QueueResult::Busy;
    state::ResourcePayloadBuffer envelope;
    uint8_t hash[32];
    if (!buildTextEnvelope(destination, text, timestamp, envelope, hash)) return QueueResult::Invalid;
    return queueEnvelope(destination, public_key, envelope, id, now, sender, user, expected_hops, next_hop);
}
LinuxReticulumLinks::QueueResult LinuxReticulumLinks::queueEnvelope(const uint8_t* destination, const uint8_t* public_key,
                                                                    state::ResourcePayloadBuffer& envelope, ::chat::MessageId id, uint32_t now, Sender sender, void* user,
                                                                    uint8_t expected_hops, const uint8_t* next_hop)
{
    if (!id || !identity_.ready() || !destination || !public_key || envelope.empty() || envelope.size() > state::kMaxAdvertisedResourceBytes) return QueueResult::Invalid;
    const auto pending = pendingOutboundIds();
    if (std::find(pending.begin(), pending.end(), id) != pending.end()) return QueueResult::Pending;
    if (pending.size() >= 32) return QueueResult::Busy;
    const auto opened = open(destination, public_key, now, sender, user, expected_hops, next_hop);
    if (opened == OpenResult::TxFailed) return QueueResult::TxFailed;
    if (opened == OpenResult::Invalid) return QueueResult::Invalid;
    if (opened == OpenResult::Full) return QueueResult::Busy;
    auto* session = links_.findOpenSessionByDestination(destination, state::LocalDestinationKind::Delivery);
    if (!session || links_.deferredPayloadCount(*session) >= 8) return QueueResult::Busy;
    state::DeferredLinkPayload payload;
    payload.message_id = id;
    payload.payload = std::move(envelope);
    links_.appendDeferredPayload(*session, std::move(payload));
    return QueueResult::Queued;
}
void LinuxReticulumLinks::processOutbound(uint32_t now, Sender sender, void* user)
{
    if (resource_turn_ && processResources(now, sender, user))
    {
        resource_turn_ = false;
        return;
    }
    bool attempted = false;
    links_.forEachSession([&](Session& session)
                          {
        if (attempted || !session.validated || session.state != state::LinkState::Active) return;
        const auto* pending = links_.firstDeferredPayload(session);
        if (pending && attempts_.size() < 32)
        {
            const auto id = pending->message_id;
            const bool fits = pending->payload.size() <= session.mdu;
            if (!fits)
            {
                if (std::count_if(resource_io_.begin(), resource_io_.end(), [](const auto& io) { return io.used && io.outgoing; }) >= 2)
                    return;
                attempted = true;
                const bool queued = startResource(session, pending->payload.data(), pending->payload.size(), id, now, sender, user);
                links_.popFirstDeferredPayload(session);
                if (!queued) outcome(id, ::chat::MessageStatus::Failed, ::chat::delivery::SendFailureKind::RadioSendFailed);
                return;
            }
            attempted = true;
            const bool sent = fits &&
                send(session, r::PacketContext::None, pending->payload.data(), pending->payload.size(), true, sender, user);
            if (sent)
            {
                uint8_t packet_hash[32];
                r::computePacketHash(packet_.data(), last_packet_size_, packet_hash);
                attempts_.noteLinkPacketReceipt(packet_hash, session.link_id, id, now, 32);
                links_.touchOutbound(session, now);
            }
            links_.popFirstDeferredPayload(session);
            outcome(id, sent ? ::chat::MessageStatus::Sent : ::chat::MessageStatus::Failed,
                sent ? ::chat::delivery::SendFailureKind::None : fits ? ::chat::delivery::SendFailureKind::RadioSendFailed : ::chat::delivery::SendFailureKind::Rejected);
        }
        else if (links_.advanceSessionLifecycle(session, now, state::LinkRuntimeLimits{}).send_keepalive)
        {
            attempted = true;
            const uint8_t probe = 0xff;
            if (send(session, r::PacketContext::Keepalive, &probe, 1, false, sender, user))
            {
                links_.noteKeepaliveSent(session, now);
                links_.touchOutbound(session, now);
            }
        } });
    if (attempted) resource_turn_ = true;
    else
    {
        (void)processResources(now, sender, user);
        resource_turn_ = false;
    }
}
bool LinuxReticulumLinks::receive(const uint8_t* raw, size_t len, uint32_t now, Sender sender, void* user, const ::chat::RxMeta* meta)
{
    r::ParsedPacket packet;
    if (!raw || len > r::kReticulumMtu || !r::parsePacket(raw, len, &packet) || packet.hops >= 128) return false;
    if (packet.packet_type == r::PacketType::LinkRequest)
    {
        const bool ok = request(raw, len, packet, now, sender, user);
        if (!ok) ++counters_.rejected;
        return true;
    }
    if (packet.destination_type != r::DestinationType::Link) return false;
    auto* session = links_.findSession(packet.destination_hash);
    if (session && (session->destination == state::LocalDestinationKind::Delivery || session->destination == state::LocalDestinationKind::Propagation) && session->validated && session->state == state::LinkState::Active && packet.packet_type == r::PacketType::Proof &&
        packet.context == static_cast<uint8_t>(r::PacketContext::ResourcePrf) && packet.hops + 1 == session->expected_hops && len <= session->mtu)
    {
        if (!resourceReceipt(*session, packet.payload, packet.payload_len, now)) ++counters_.rejected;
        return true;
    }
    if (session && session->destination == state::LocalDestinationKind::Delivery && session->validated && session->state == state::LinkState::Active && packet.packet_type == r::PacketType::Proof &&
        packet.context == 0 && packet.payload_len == 96 && packet.hops + 1 == session->expected_hops && len <= session->mtu)
    {
        const auto* receipt = attempts_.findLinkPacketReceipt(session->link_id, packet.payload);
        const auto* key = session->initiator ? session->peer_identity_sig_pub : session->peer_link_sig_pub;
        if (receipt && platform::linux_runtime::LinuxReticulumIdentity::verify(key, packet.payload, 32, packet.payload + 32))
        {
            const auto id = receipt->message_id;
            attempts_.removeLinkPacketReceipt(session->link_id, packet.payload);
            links_.touchInbound(*session, now);
            outcome(id, ::chat::MessageStatus::Delivered);
        }
        else ++counters_.rejected;
        return true;
    }
    if (session && session->destination == state::LocalDestinationKind::Propagation && session->validated && session->state == state::LinkState::Active &&
        packet.packet_type == r::PacketType::Proof && packet.context == 0 && packet.payload_len == 96 &&
        packet.hops + 1 == session->expected_hops && len <= session->mtu)
    {
        if (links_.findPendingRequest(*session, packet.payload, 16) &&
            platform::linux_runtime::LinuxReticulumIdentity::verify(session->peer_identity_sig_pub, packet.payload, 32, packet.payload + 32)) links_.touchInbound(*session, now);
        else ++counters_.rejected;
        return true;
    }
    if (session && packet.packet_type == r::PacketType::Proof && packet.context == static_cast<uint8_t>(r::PacketContext::LrProof) &&
        packet.hops + 1 == session->expected_hops && len <= session->mtu)
    {
        if (!acceptProof(*session, packet, now, sender, user)) ++counters_.rejected;
        return true;
    }
    if (!session || packet.packet_type != r::PacketType::Data || packet.hops + 1 != session->expected_hops ||
        session->state == state::LinkState::Closed || len > session->mtu)
    {
        ++counters_.rejected;
        return true;
    }
    const auto context = static_cast<r::PacketContext>(packet.context);
    if ((session->destination == state::LocalDestinationKind::Delivery || session->destination == state::LocalDestinationKind::Propagation) && context == r::PacketContext::Resource && session->validated && session->state == state::LinkState::Active)
    {
        if (resourcePart(*session, packet, now, sender, user, meta)) links_.touchInbound(*session, now);
        else ++counters_.rejected;
        return true;
    }
    if (context == r::PacketContext::Keepalive && session->validated && packet.payload_len == 1)
    {
        if (packet.payload[0] == 0xff)
        {
            const uint8_t ack = 0xfe;
            if (send(*session, context, &ack, 1, false, sender, user)) links_.touchOutbound(*session, now);
        }
        if (packet.payload[0] == 0xff || packet.payload[0] == 0xfe) links_.touchInbound(*session, now);
        return true;
    }
    size_t size = plain_.size();
    if (!r::tokenDecrypt(session->derived_key, packet.payload, packet.payload_len, plain_.data(), &size))
    {
        ++counters_.rejected;
        return true;
    }
    bool valid = false;
    if ((session->destination == state::LocalDestinationKind::Delivery || session->destination == state::LocalDestinationKind::Propagation) && context >= r::PacketContext::ResourceAdv && context <= r::PacketContext::ResourceRcl && session->validated)
        valid = resourceControl(*session, context, plain_.data(), size, now, sender, user);
    else if (context == r::PacketContext::None && session->validated && session->state == state::LinkState::Active)
    {
        if (session->destination == state::LocalDestinationKind::CallAudio)
        {
            if (callAdmitted(*session) && call_hooks_.receive)
                try
                {
                    valid = call_hooks_.receive(call_hooks_.context, callPeer(*session), plain_.data(), size);
                }
                catch (...)
                {
                }
        }
        else valid = incomingText(*session, plain_.data(), size, raw, len, now, meta);
    }
    else if (context == r::PacketContext::Response && session->destination == state::LocalDestinationKind::Propagation && session->validated && session->state == state::LinkState::Active)
        valid = serviceResponse(*session, plain_.data(), size);
    else if (context == r::PacketContext::LrRtt && size == 9 && plain_[0] == 0xcb && session->state == state::LinkState::Handshake)
    {
        uint64_t bits = 0;
        for (size_t i = 1; i < 9; ++i) bits = (bits << 8) | plain_[i];
        double rtt;
        std::memcpy(&rtt, &bits, sizeof(rtt));
        if (std::isfinite(rtt) && rtt >= 0 && rtt <= 3600)
        {
            links_.markSessionValidatedActive(*session, static_cast<float>(rtt), 15000);
            if (session->destination == state::LocalDestinationKind::CallAudio && call_hooks_.validated)
                try
                {
                    call_hooks_.validated(call_hooks_.context, callPeer(*session));
                }
                catch (...)
                {
                }
            valid = true;
        }
    }
    else if (context == r::PacketContext::LinkIdentify && session->validated && size == 128)
    {
        uint8_t signed_data[80];
        std::memcpy(signed_data, session->link_id, 16);
        std::memcpy(signed_data + 16, plain_.data(), 64);
        valid = platform::linux_runtime::LinuxReticulumIdentity::verify(plain_.data() + 32, signed_data, sizeof(signed_data), plain_.data() + 64);
        if (valid)
        {
            uint8_t identity_hash[16];
            r::computeIdentityHash(plain_.data(), identity_hash);
            if (session->remote_identity_known && std::memcmp(identity_hash, session->remote_identity_hash, 16)) valid = false;
            else
            {
                const bool newly_identified = !session->remote_identity_known;
                std::memcpy(session->remote_identity_hash, identity_hash, 16);
                std::memcpy(session->peer_identity_sig_pub, plain_.data() + 32, 32);
                session->remote_identity_known = true;
                uint8_t name[10];
                r::computeNameHash(session->destination == state::LocalDestinationKind::CallAudio ? "lxst" : "lxmf",
                                   session->destination == state::LocalDestinationKind::CallAudio ? "telephony" : session->destination == state::LocalDestinationKind::Propagation ? "propagation"
                                                                                                                                                                                   : "delivery",
                                   name);
                r::computeDestinationHash(name, identity_hash, session->remote_destination_hash);
                if (session->destination == state::LocalDestinationKind::CallAudio && !admitCall(*session))
                {
                    closeCall(session->link_id, now, sender, user);
                    valid = false;
                }
                if (newly_identified && peer_observer_)
                    try
                    {
                        peer_observer_(incoming_context_, plain_.data(), meta ? meta->rx_timestamp_s : 0);
                    }
                    catch (...)
                    {
                    }
            }
        }
    }
    else if (context == r::PacketContext::LinkClose && size == 16 && !std::memcmp(plain_.data(), session->link_id, 16))
    {
        close(*session, state::LinkCloseReason::RemoteClose, now);
        valid = true;
    }
    // Application acceptance and its later durable commit own text proofs.
    if (valid) links_.touchInbound(*session, now);
    else ++counters_.rejected;
    wipe(plain_.data(), plain_.size());
    return true;
}
void LinuxReticulumLinks::tick(uint32_t now)
{
    attempts_.takeExpiredReceipts(state::DeliveryAttemptKind::LinkResource, now, 120000,
                                  [&](const auto& item)
                                  { outcome(item.message_id, ::chat::MessageStatus::Failed, ::chat::delivery::SendFailureKind::AckTimeout); });
    attempts_.takeExpiredReceipts(state::DeliveryAttemptKind::LinkPacket, now, 60000,
                                  [&](const auto& item)
                                  { outcome(item.message_id, ::chat::MessageStatus::Failed, ::chat::delivery::SendFailureKind::AckTimeout); });
    for (auto& pending : incoming_)
        if (pending.used && uint32_t(now - pending.created) > 120000) pending = {};
    links_.forEachSession([&](Session& session)
                          {
        const uint32_t activity = session.validated ? session.last_inbound_ms : session.created_ms;
        const uint32_t timeout = session.validated ? 45000U : session.state == state::LinkState::Pending ? 30000U : 15000U;
        if (session.state != state::LinkState::Closed && uint32_t(now - activity) > timeout)
            close(session, state::LinkCloseReason::Timeout, now); });
    state::LinkRuntimeLimits limits;
    limits.link_request_ttl_ms = 120000;
    limits.handshake_timeout_ms = UINT32_MAX;
    limits.idle_timeout_ms = UINT32_MAX;
    limits.session_ttl_ms = UINT32_MAX;
    limits.closed_retention_ms = 1;
    links_.removeExpiredSessions(now, limits);
}
} // namespace trailmate::linux_app
