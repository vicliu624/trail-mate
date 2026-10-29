#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/reticulum/link_handshake_wire.h"
#include "chat/linux_reticulum_links.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace r = chat::reticulum;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
struct Wire
{
    std::vector<uint8_t> last;
    std::vector<std::vector<uint8_t>> frames;
    unsigned sends = 0;
    bool fail = false;
    static bool send(void* pointer, const uint8_t* data, size_t size)
    {
        auto& wire = *static_cast<Wire*>(pointer);
        wire.last.assign(data, data + size);
        wire.frames.push_back(wire.last);
        ++wire.sends;
        return !wire.fail;
    }
};
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-links-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    Identity local(root / "local.sqlite"), peer(root / "peer.sqlite");
    assert(local.begin() && peer.begin());
    Links links(local);
    Wire wire;
    uint8_t payload[67];
    std::memcpy(payload, peer.publicKey().data(), 64);
    r::buildLinkSignalling(500, payload + 64);
    std::array<uint8_t, 500> request{};
    size_t request_size = request.size();
    assert(r::buildHeader1Packet(r::PacketType::LinkRequest, r::DestinationType::Single, r::PacketContext::None, false,
                                 local.destinationHash().data(), payload, sizeof(payload), request.data(), &request_size));
    uint8_t id[16], legacy_id[16];
    assert(r::computeLinkRequestId(request.data(), request_size, id));
    assert(r::computeLinkRequestId(request.data(), request_size - 3, legacy_id));
    assert(!std::memcmp(id, legacy_id, 16));
    assert(r::linkPayloadMdu(500) == 431 && r::linkPayloadMdu(67) == 0);
    assert(links.receive(request.data(), request_size, 100, Wire::send, &wire));
    assert(links.statistics().sessions == 1 && links.statistics().proofs == 1 && links.statistics().active == 0);
    r::ParsedPacket proof;
    assert(r::parsePacket(wire.last.data(), wire.last.size(), &proof));
    assert(proof.packet_type == r::PacketType::Proof && proof.destination_type == r::DestinationType::Link &&
           proof.context == 0xff && proof.payload_len == 99 && !std::memcmp(proof.destination_hash, id, 16));
    uint8_t signed_data[83];
    std::memcpy(signed_data, id, 16);
    std::memcpy(signed_data + 16, proof.payload + 64, 32);
    std::memcpy(signed_data + 48, local.publicKey().data() + 32, 32);
    std::memcpy(signed_data + 80, proof.payload + 96, 3);
    assert(Identity::verify(local.publicKey().data() + 32, signed_data, sizeof(signed_data), proof.payload));
    uint8_t secret[32], key[64];
    assert(peer.sharedSecret(proof.payload + 64, secret));
    assert(r::hkdfSha256(secret, sizeof(secret), id, 16, nullptr, 0, key, sizeof(key)));
    const auto proof_bytes = wire.last;
    assert(links.receive(request.data(), request_size, 101, Wire::send, &wire));
    assert(wire.last == proof_bytes && links.statistics().sessions == 1);
    links.tick(200);
    assert(links.statistics().sessions == 1); // Maintenance must retain live handshakes.
    auto encrypted = [&](r::PacketContext context, const uint8_t* plain, size_t size)
    {
        uint8_t iv[16]{}; // Deterministic test peer, never application randomness.
        std::array<uint8_t, 500> token{}, packet{};
        size_t token_size = token.size(), packet_size = packet.size();
        assert(r::tokenEncrypt(key, iv, plain, size, token.data(), &token_size));
        assert(r::buildHeader1Packet(r::PacketType::Data, r::DestinationType::Link, context, false, id,
                                     token.data(), token_size, packet.data(), &packet_size));
        return std::vector<uint8_t>(packet.data(), packet.data() + packet_size);
    };
    const uint8_t rtt[] = {0xcb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0}; // MessagePack 1.0 seconds.
    auto packet = encrypted(r::PacketContext::LrRtt, rtt, sizeof(rtt));
    packet.back() ^= 1;
    assert(links.receive(packet.data(), packet.size(), 201, Wire::send, &wire));
    assert(links.statistics().active == 0 && links.statistics().rejected == 1);
    packet.back() ^= 1;
    assert(links.receive(packet.data(), packet.size(), 202, Wire::send, &wire));
    assert(links.statistics().active == 1);
    uint8_t identify[128], identity_domain[80];
    std::memcpy(identify, peer.publicKey().data(), 64);
    std::memcpy(identity_domain, id, 16);
    std::memcpy(identity_domain + 16, identify, 64);
    assert(peer.sign(identity_domain, sizeof(identity_domain), identify + 64));
    packet = encrypted(r::PacketContext::LinkIdentify, identify, sizeof(identify));
    assert(links.receive(packet.data(), packet.size(), 203, Wire::send, &wire));
    assert(links.statistics().rejected == 1);
    packet = encrypted(r::PacketContext::None, rtt, sizeof(rtt));
    const auto sent = wire.sends;
    assert(links.receive(packet.data(), packet.size(), 204, Wire::send, &wire));
    assert(wire.sends == sent && links.statistics().rejected == 2); // No false application proof.
    struct Received
    {
        chat::MeshIncomingText message{};
        unsigned count = 0;
        std::vector<chat::MeshIncomingText> messages;
        bool reject = false, ignored = false, resolve = false;
        uint8_t public_key[64]{};
    } received;
    links.setIncomingHooks([](void* pointer, const chat::MeshIncomingText& message)
                           {
        auto& received = *static_cast<Received*>(pointer);
        if (received.reject) return false;
        received.message = message;
        received.messages.push_back(message);
        ++received.count;
        return true; },
                           [](void* pointer, const uint8_t*, uint8_t* public_key, bool* ignored)
                           {
                               const auto& received = *static_cast<Received*>(pointer);
                               *ignored = received.ignored;
                               std::memcpy(public_key, received.public_key, 64);
                               return received.resolve;
                           },
                           &received);
    uint8_t body[300], signed_message[500], hash[32], signature[64], envelope[500];
    size_t body_size = sizeof(body), signed_size = sizeof(signed_message), envelope_size = sizeof(envelope);
    assert(chat::lxmf::encodeTextPayload(1700000000, "", "durable receive 中文", body, &body_size));
    assert(chat::lxmf::buildSignedPart(local.destinationHash().data(), peer.destinationHash().data(), body, body_size,
                                       signed_message, &signed_size, hash));
    assert(peer.sign(signed_message, signed_size, signature));
    assert(chat::lxmf::packMessage(local.destinationHash().data(), peer.destinationHash().data(), signature,
                                   body, body_size, envelope, &envelope_size));
    const auto text_packet = encrypted(r::PacketContext::None, envelope, envelope_size);
    auto bad_envelope = std::vector<uint8_t>(envelope, envelope + envelope_size);
    bad_envelope[32] ^= 1;
    packet = encrypted(r::PacketContext::None, bad_envelope.data(), bad_envelope.size());
    const auto before_text = wire.sends;
    assert(links.receive(packet.data(), packet.size(), 205, Wire::send, &wire));
    assert(received.count == 0 && wire.sends == before_text);
    assert(links.receive(text_packet.data(), text_packet.size(), 206, Wire::send, &wire));
    assert(received.count == 1 && received.message.text == "durable receive 中文" && received.message.encrypted &&
           !received.message.source_unverified && received.message.has_reticulum_lxmf_hash &&
           !std::memcmp(received.message.reticulum_lxmf_hash, hash, 32));
    assert(wire.sends == before_text && links.statistics().pending_incoming == 1);
    assert(links.receive(text_packet.data(), text_packet.size(), 207, Wire::send, &wire));
    assert(received.count == 1); // Retransmission cannot duplicate pending application work.
    auto wrong_commit = received.message;
    wrong_commit.reticulum_lxmf_hash[0] ^= 1;
    links.commitIncoming(wrong_commit, true, 208, Wire::send, &wire);
    assert(wire.sends == before_text && links.statistics().pending_incoming == 1);
    links.commitIncoming(received.message, false, 209, Wire::send, &wire);
    assert(wire.sends == before_text && links.statistics().pending_incoming == 0);
    assert(links.receive(text_packet.data(), text_packet.size(), 210, Wire::send, &wire));
    wire.fail = true;
    links.commitIncoming(received.message, true, 211, Wire::send, &wire);
    assert(links.statistics().pending_incoming == 1 && links.statistics().delivery_proofs == 0);
    wire.fail = false;
    links.retryAccepted(1210, Wire::send, &wire);
    assert(links.statistics().pending_incoming == 1);
    links.retryAccepted(1211, Wire::send, &wire);
    assert(links.statistics().pending_incoming == 0 && links.statistics().delivery_proofs == 1);
    r::ParsedPacket accepted_proof;
    assert(r::parsePacket(wire.last.data(), wire.last.size(), &accepted_proof));
    uint8_t accepted_hash[32];
    r::computePacketHash(text_packet.data(), text_packet.size(), accepted_hash);
    assert(accepted_proof.packet_type == r::PacketType::Proof && accepted_proof.context == 0 && accepted_proof.payload_len == 96 &&
           !std::memcmp(accepted_proof.payload, accepted_hash, 32) &&
           Identity::verify(local.publicKey().data() + 32, accepted_hash, 32, accepted_proof.payload + 32));
    received.ignored = true;
    assert(links.receive(text_packet.data(), text_packet.size(), 1212, Wire::send, &wire));
    assert(received.count == 2 && links.statistics().pending_incoming == 0);
    received.ignored = false;
    received.reject = true;
    assert(links.receive(text_packet.data(), text_packet.size(), 1213, Wire::send, &wire));
    assert(links.statistics().pending_incoming == 0);
    received.reject = false;
    const auto tail_packet = encrypted(r::PacketContext::None, envelope + 16, envelope_size - 16);
    assert(links.receive(tail_packet.data(), tail_packet.size(), 1214, Wire::send, &wire));
    assert(received.count == 3 && !std::memcmp(received.message.reticulum_lxmf_hash, hash, 32));
    links.commitIncoming(received.message, true, 1215, Wire::send, &wire);
    assert(links.statistics().pending_incoming == 0 && links.statistics().delivery_proofs == 2);
    received.messages.clear();
    for (unsigned i = 0; i < 33; ++i)
    {
        body_size = sizeof(body);
        signed_size = sizeof(signed_message);
        envelope_size = sizeof(envelope);
        assert(chat::lxmf::encodeTextPayload(1700000100 + i, "", "bounded acceptance", body, &body_size));
        assert(chat::lxmf::buildSignedPart(local.destinationHash().data(), peer.destinationHash().data(), body, body_size,
                                           signed_message, &signed_size, hash));
        assert(peer.sign(signed_message, signed_size, signature));
        assert(chat::lxmf::packMessage(local.destinationHash().data(), peer.destinationHash().data(), signature,
                                       body, body_size, envelope, &envelope_size));
        packet = encrypted(r::PacketContext::None, envelope, envelope_size);
        assert(links.receive(packet.data(), packet.size(), 1220 + i, Wire::send, &wire));
        assert(links.statistics().pending_incoming == std::min<size_t>(i + 1, 32));
        assert(received.messages.size() == std::min<size_t>(i + 1, 32));
    }
    for (const auto& message : received.messages) links.commitIncoming(message, false, 1260, Wire::send, &wire);
    assert(links.statistics().pending_incoming == 0);
    packet = encrypted(r::PacketContext::LinkClose, id, sizeof(id));
    assert(links.receive(packet.data(), packet.size(), 1261, Wire::send, &wire));
    assert(links.statistics().active == 0);
    links.tick(1263);
    assert(links.statistics().sessions == 0);
    wire.fail = true;
    assert(links.receive(request.data(), request_size, 300, Wire::send, &wire));
    assert(links.statistics().sessions == 0 && links.statistics().tx_errors == 2);
    wire.fail = false;
    assert(links.receive(request.data(), request_size, 400, Wire::send, &wire));
    links.tick(15401);
    links.tick(15403);
    assert(links.statistics().sessions == 0);
    links.reset();
    assert(links.statistics().proofs == 0);
    auto invalid = request;
    invalid[r::kPacketHeader1Size + 64] = 0xe0;
    assert(links.receive(invalid.data(), request_size, 20000, Wire::send, &wire));
    assert(links.statistics().sessions == 0 && links.statistics().rejected == 1);
    invalid = request;
    std::memset(invalid.data() + r::kPacketHeader1Size, 0, 32);
    assert(links.receive(invalid.data(), request_size, 20001, Wire::send, &wire));
    assert(links.statistics().sessions == 0); // Reject low-order X25519 peer keys.
    for (unsigned i = 0; i < 9; ++i)
    {
        auto unique = request;
        unique[r::kPacketHeader1Size + 32] ^= static_cast<uint8_t>(i);
        assert(links.receive(unique.data(), request_size, 21000 + i, Wire::send, &wire));
        assert(links.statistics().sessions == std::min<size_t>(i + 1, 8));
    }
    const auto capacity_sends = wire.sends;
    auto extra = request;
    extra[r::kPacketHeader1Size + 32] ^= 9;
    assert(links.receive(extra.data(), request_size, 21010, Wire::send, &wire));
    assert(wire.sends == capacity_sends && links.statistics().sessions == 8);
    links.reset();
    // Drive two independent owners through the full asynchronous handshake.
    std::vector<std::pair<chat::MessageId, chat::MessageStatus>> outcomes;
    Links caller(peer), callee(local);
    caller.setOutcomeSink([](void* pointer, chat::MessageId id, chat::MessageStatus status, chat::delivery::SendFailureKind)
                          { static_cast<decltype(outcomes)*>(pointer)->emplace_back(id, status); },
                          &outcomes);
    Wire outbound, inbound;
    assert(caller.open(local.destinationHash().data(), local.publicKey().data(), 30000, Wire::send, &outbound) == Links::OpenResult::Opening);
    const auto initiating_request = outbound.last;
    assert(caller.open(local.destinationHash().data(), local.publicKey().data(), 30001, Wire::send, &outbound) == Links::OpenResult::Pending);
    assert(outbound.sends == 1);
    assert(callee.receive(initiating_request.data(), initiating_request.size(), 30010, Wire::send, &inbound));
    auto response = inbound.last;
    response.back() ^= 1;
    assert(caller.receive(response.data(), response.size(), 30020, Wire::send, &outbound));
    assert(caller.statistics().active == 0 && outbound.sends == 1);
    response.back() ^= 1;
    assert(caller.receive(response.data(), response.size(), 30021, Wire::send, &outbound));
    assert(caller.statistics().active == 1 && outbound.frames.size() == 3);
    for (size_t i = 1; i < outbound.frames.size(); ++i)
        assert(callee.receive(outbound.frames[i].data(), outbound.frames[i].size(), 30022, Wire::send, &inbound));
    assert(callee.statistics().active == 1 && callee.statistics().rejected == 0);
    assert(caller.open(local.destinationHash().data(), local.publicKey().data(), 30023, Wire::send, &outbound) == Links::OpenResult::Active);
    callee.setIncomingHooks([](void* pointer, const chat::MeshIncomingText& message)
                            {
        *static_cast<chat::MeshIncomingText*>(pointer) = message;
        return true; },
                            nullptr, &received.message);
    assert(caller.queueText(local.destinationHash().data(), local.publicKey().data(), "outbound packet", 91, 1700000000.25,
                            30024, Wire::send, &outbound) == Links::QueueResult::Queued);
    caller.processOutbound(30025, Wire::send, &outbound);
    assert(outcomes.size() == 1 && outcomes.back().second == chat::MessageStatus::Sent);
    assert(callee.receive(outbound.last.data(), outbound.last.size(), 30026, Wire::send, &inbound));
    assert(received.message.text == "outbound packet" && callee.statistics().pending_incoming == 1);
    callee.commitIncoming(received.message, true, 30027, Wire::send, &inbound);
    auto ack = inbound.last;
    ack.back() ^= 1;
    assert(caller.receive(ack.data(), ack.size(), 30028, Wire::send, &outbound));
    assert(outcomes.size() == 1);
    ack.back() ^= 1;
    assert(caller.receive(ack.data(), ack.size(), 30029, Wire::send, &outbound));
    assert(outcomes.size() == 2 && outcomes.back().first == 91 && outcomes.back().second == chat::MessageStatus::Delivered);
    assert(caller.receive(ack.data(), ack.size(), 30030, Wire::send, &outbound));
    assert(outcomes.size() == 2);
    assert(caller.queueText(local.destinationHash().data(), local.publicKey().data(), "failed packet", 92, 1700000000.5,
                            30031, Wire::send, &outbound) == Links::QueueResult::Queued);
    outbound.fail = true;
    caller.processOutbound(30032, Wire::send, &outbound);
    assert(outcomes.size() == 3 && outcomes.back().second == chat::MessageStatus::Failed);
    outbound.fail = false;
    assert(caller.queueText(local.destinationHash().data(), local.publicKey().data(), "timeout packet", 93, 1700000000.75,
                            30033, Wire::send, &outbound) == Links::QueueResult::Queued);
    caller.processOutbound(30034, Wire::send, &outbound);
    caller.tick(90035);
    assert(outcomes.back().first == 93 && outcomes.back().second == chat::MessageStatus::Failed);
    caller.reset();
    callee.reset();
    outbound = {};
    inbound = {};
    outbound.fail = true;
    assert(caller.open(local.destinationHash().data(), local.publicKey().data(), 31000, Wire::send, &outbound) == Links::OpenResult::TxFailed);
    assert(caller.statistics().sessions == 0);
    outbound.fail = false;
    auto invalid_destination = local.destinationHash();
    invalid_destination[0] ^= 1;
    assert(caller.open(invalid_destination.data(), local.publicKey().data(), 31001, Wire::send, &outbound) == Links::OpenResult::Invalid);
    assert(caller.open(local.destinationHash().data(), local.publicKey().data(), 31002, Wire::send, &outbound, 2) == Links::OpenResult::Invalid);
    links.reset();
    received.resolve = true;
    received.reject = received.ignored = false;
    std::memcpy(received.public_key, peer.publicKey().data(), 64);
    assert(links.receive(request.data(), request_size, 50000, Wire::send, &wire));
    r::ParsedPacket fresh_proof;
    assert(r::parsePacket(wire.last.data(), wire.last.size(), &fresh_proof));
    assert(peer.sharedSecret(fresh_proof.payload + 64, secret));
    assert(r::hkdfSha256(secret, sizeof(secret), id, 16, nullptr, 0, key, sizeof(key)));
    packet = encrypted(r::PacketContext::LrRtt, rtt, sizeof(rtt));
    assert(links.receive(packet.data(), packet.size(), 50001, Wire::send, &wire));
    const auto before_resolved = received.count;
    packet = encrypted(r::PacketContext::None, envelope, envelope_size);
    assert(links.receive(packet.data(), packet.size(), 50002, Wire::send, &wire));
    assert(received.count == before_resolved + 1); // Verified directory key, without LinkIdentify.
    received.public_key[0] ^= 1;
    assert(links.receive(packet.data(), packet.size(), 50003, Wire::send, &wire));
    assert(received.count == before_resolved + 1); // Reject a mismatched directory key.
    received.resolve = false;
    assert(links.receive(packet.data(), packet.size(), 50004, Wire::send, &wire));
    assert(received.count == before_resolved + 1); // No unauthenticated fallback.
    links.reset();
    std::filesystem::remove_all(root);
}
