#include "chat/infra/reticulum/announce_verifier.h"
#include <cstring>

namespace chat::reticulum
{
const char* AnnounceVerifier::verify(const ParsedPacket& packet, ParsedAnnounce& announce, uint8_t identity[16], uint8_t destination[16], AnnounceSignatureVerifier verifier)
{
    announce = {};
    if (!identity || !destination) return "invalid_output";
    std::memset(identity, 0, 16);
    std::memset(destination, 0, 16);
    if (!packet.valid || !packet.destination_hash || packet.packet_type != PacketType::Announce || packet.destination_type != DestinationType::Single ||
        (packet.context != static_cast<uint8_t>(PacketContext::None) && packet.context != static_cast<uint8_t>(PacketContext::PathResponse))) return "invalid_packet";
    if (!parseAnnounce(packet, &announce) || !announce.valid) return "parse_failed";
    computeIdentityHash(announce.public_key, identity);
    computeDestinationHash(announce.name_hash, identity, destination);
    if (std::memcmp(destination, packet.destination_hash, 16)) return "destination_mismatch";
    size_t used = 0;
    const auto append = [&](const uint8_t* data, size_t size)
    {
        if ((!data && size) || size > sizeof(signed_scratch_) - used) return false;
        if (size) std::memcpy(signed_scratch_ + used, data, size);
        used += size;
        return true;
    };
    if (!append(packet.destination_hash, kTruncatedHashSize) || !append(announce.public_key, kCombinedPublicKeySize) ||
        !append(announce.name_hash, kNameHashSize) || !append(announce.random_hash, 10) ||
        (announce.has_ratchet && !append(announce.ratchet, announce.ratchet_len)) || !append(announce.app_data, announce.app_data_len)) return "signed_data_overflow";
    if (!verifier) return "crypto_unavailable";
    return verifier(announce.public_key + kEncryptionPublicKeySize, announce.signature, signed_scratch_, used) ? nullptr : "signature_failed";
}
} // namespace chat::reticulum
