#include "chat/linux_meshcore_direct_wire.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include <cstring>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/crypto.h>
#endif

namespace trailmate::linux_app::meshcore_wire
{
namespace
{
namespace mc = chat::meshcore;
struct Secret
{
    uint8_t bytes[32]{};
    ~Secret()
    {
#if defined(TRAIL_MATE_HAS_OPENSSL)
        OPENSSL_cleanse(bytes, sizeof(bytes));
#endif
    }
};
void put32(uint8_t* out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(value >> (8 * i));
}
uint32_t get32(const uint8_t* in)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
    return value;
}
uint32_t textAck(const uint8_t* plain, std::size_t size, const std::array<uint8_t, 32>& sender)
{
    std::array<uint8_t, 216> input{};
    if (size > 184) return 0;
    std::memcpy(input.data(), plain, size);
    std::memcpy(input.data() + size, sender.data(), sender.size());
    uint8_t hash[4]{};
    mc::sha256Trunc(hash, sizeof(hash), input.data(), size + sender.size());
    return get32(hash);
}
bool encode(const Identity& self, const std::array<uint8_t, 32>& peer, mc::PayloadProfile profile,
            uint8_t type, const uint8_t* plain, std::size_t size, Frame& frame)
{
    frame = {};
    const auto hashes = mc::payloadHashBytes(profile);
    const auto budget = ((184 - 2 * hashes - mc::payloadMacBytes(profile)) / 16) * 16;
    if (!self.ready() || !plain || !size || size > budget || peer == self.publicKey()) return false;
    Secret secret;
    if (!self.sharedSecret(peer.data(), secret.bytes)) return false;
    std::array<uint8_t, 184> payload{};
    std::size_t payload_size = 0;
    if (!mc::buildPeerDatagramPayload(profile, peer.data(), self.publicKey().data(),
                                      secret.bytes, secret.bytes, plain, size, payload.data(), payload.size(), &payload_size)) return false;
    return mc::buildFrameNoTransport(profile, 1, type, nullptr, 0, payload.data(), payload_size,
                                     frame.bytes.data(), frame.bytes.size(), &frame.size);
}
} // namespace
bool encodeDirectText(const Identity& self, const std::array<uint8_t, 32>& peer,
                      mc::PayloadProfile profile, const std::string& text,
                      uint32_t timestamp, Frame& frame, uint32_t& ack)
{
    ack = 0;
    frame = {};
    if (text.empty() || text.size() > 179 || text.find('\0') != std::string::npos) return false;
    std::array<uint8_t, 184> plain{};
    put32(plain.data(), timestamp);
    std::memcpy(plain.data() + 5, text.data(), text.size());
    if (!encode(self, peer, profile, 2, plain.data(), 5 + text.size(), frame)) return false;
    ack = textAck(plain.data(), 5 + text.size(), self.publicKey());
    return ack != 0;
}
bool encodeDirectData(const Identity& self, const std::array<uint8_t, 32>& peer,
                      mc::PayloadProfile profile, uint32_t port, const uint8_t* bytes,
                      std::size_t size, bool want_ack, Frame& frame, uint32_t& ack)
{
    ack = 0;
    frame = {};
    if (!port || !bytes || !size || size > 177) return false;
    const bool verification = port == 12 && size == 12 && bytes[0] == 'T' && bytes[1] == 'M' &&
                              bytes[2] == 2 && bytes[3] >= 1 && bytes[3] <= 3;
    if (bytes[size - 1] == 0 && !verification && !mc::isFixedNodeInfoPayload(port, bytes, size)) return false;
    std::array<uint8_t, 184> plain{};
    plain[0] = 0xDA;
    plain[1] = 0x7A;
    plain[2] = want_ack ? 1 : 0;
    put32(plain.data() + 3, port);
    std::memcpy(plain.data() + 7, bytes, size);
    if (!encode(self, peer, profile, 7, plain.data(), 7 + size, frame)) return false;
    mc::ParsedPacket parsed;
    if (!mc::parsePacket(frame.bytes.data(), frame.size, &parsed)) return false;
    ack = want_ack ? mc::packetSignature(7, 0, parsed.payload, parsed.payload_len) : 0;
    return !want_ack || ack != 0;
}
bool decodeDirect(const Identity& self, const std::array<uint8_t, 32>& peer,
                  const uint8_t* bytes, std::size_t size, DirectIncoming& message)
{
    message = {};
    mc::ParsedPacket parsed;
    if (!self.ready() || !bytes || size > 255 || !mc::parsePacket(bytes, size, &parsed) ||
        parsed.payload_len > 184 || (parsed.payload_type != 2 && parsed.payload_type != 7 && parsed.payload_type != 8)) return false;
    const auto profile = mc::payloadProfileFromVersion(parsed.payload_ver);
    const auto hashes = mc::payloadHashBytes(profile);
    if (parsed.payload_len <= 2 * hashes + mc::payloadMacBytes(profile) ||
        std::memcmp(parsed.payload, self.publicKey().data(), hashes) ||
        std::memcmp(parsed.payload + hashes, peer.data(), hashes)) return false;
    Secret secret;
    std::array<uint8_t, 184> plain{};
    std::size_t plain_size = 0;
    if (!self.sharedSecret(peer.data(), secret.bytes) ||
        !mc::macThenDecrypt(secret.bytes, secret.bytes, parsed.payload + 2 * hashes,
                            parsed.payload_len - 2 * hashes, plain.data(), &plain_size, mc::payloadMacBytes(profile))) return false;
    plain_size = mc::trimPeerPlaintextPadding(parsed.payload_type, plain.data(), plain_size);
    message.profile = profile;
    message.signature = mc::packetSignature(parsed.payload_type, 0, parsed.payload, parsed.payload_len);
    message.return_path_size = parsed.path_len;
    if (parsed.path_len > message.return_path.size()) return false;
    if (parsed.path_len) std::memcpy(message.return_path.data(), parsed.path, parsed.path_len);
    if (parsed.payload_type == 8)
    {
        // PATH has explicit lengths. Do not trim an ACK whose high bytes are zero.
        const std::size_t path_size = plain[0];
        if (plain_size < 6 || path_size > 64 || path_size + 6 > plain_size ||
            !mc::pathIsWellFormed(profile, path_size) || plain[1 + path_size] != 3) return false;
        for (std::size_t i = path_size + 6; i < plain_size; ++i)
            if (plain[i]) return false;
        message.kind = DirectKind::Ack;
        message.ack = get32(plain.data() + path_size + 2);
        if (!message.ack) return false;
        message.advertised_path_size = path_size;
        if (path_size) std::memcpy(message.advertised_path.data(), plain.data() + 1, path_size);
        message.has_advertised_path = true;
        return true;
    }
    if (parsed.payload_type == 2)
    {
        if (plain_size <= 5) return false;
        const auto text_type = plain[4] >> 2;
        const std::size_t prefix = text_type == 2 ? 9 : 5;
        if ((text_type != 0 && text_type != 2) || plain_size <= prefix) return false;
        message.kind = DirectKind::Text;
        message.timestamp = get32(plain.data());
        message.size = plain_size - prefix;
        if (std::memchr(plain.data() + prefix, 0, message.size)) return false;
        std::memcpy(message.payload.data(), plain.data() + prefix, message.size);
        message.ack = textAck(plain.data(), plain_size, text_type == 0 ? peer : self.publicKey());
        message.want_ack = message.ack != 0;
    }
    else
    {
        mc::DecodedDirectAppPayload data;
        if (!mc::decodeDirectAppPayload(plain.data(), plain_size, &data) || !data.portnum || !data.payload_len) return false;
        message.kind = DirectKind::Data;
        message.port = data.portnum;
        message.size = data.payload_len;
        std::memcpy(message.payload.data(), data.payload, data.payload_len);
        message.want_ack = data.want_ack;
        message.ack = data.want_ack ? message.signature : 0;
    }
    return true;
}
bool encodeDirectAck(const Identity& self, const std::array<uint8_t, 32>& peer,
                     const DirectIncoming& message, Frame& frame)
{
    frame = {};
    if (!message.want_ack || !message.ack || message.return_path_size > 64 ||
        !mc::pathIsWellFormed(message.profile, message.return_path_size)) return false;
    std::array<uint8_t, 70> plain{};
    plain[0] = uint8_t(message.return_path_size);
    if (message.return_path_size) std::memcpy(plain.data() + 1, message.return_path.data(), message.return_path_size);
    plain[message.return_path_size + 1] = 3;
    put32(plain.data() + message.return_path_size + 2, message.ack);
    return encode(self, peer, message.profile, 8, plain.data(), message.return_path_size + 6, frame);
}
bool applyDirectPath(Frame& frame, mc::PayloadProfile profile, const uint8_t* path, std::size_t size)
{
    mc::ParsedPacket parsed;
    if (size > 64 || (size && !path) || !mc::pathIsWellFormed(profile, size) ||
        frame.size > frame.bytes.size() || !mc::parsePacket(frame.bytes.data(), frame.size, &parsed) ||
        mc::payloadProfileFromVersion(parsed.payload_ver) != profile ||
        (parsed.payload_type != 2 && parsed.payload_type != 7 && parsed.payload_type != 8)) return false;
    Frame routed;
    if (!mc::buildFrameNoTransport(profile, 2, parsed.payload_type, path, size,
                                   parsed.payload, parsed.payload_len, routed.bytes.data(),
                                   routed.bytes.size(), &routed.size)) return false;
    frame = routed;
    return true;
}
} // namespace trailmate::linux_app::meshcore_wire
