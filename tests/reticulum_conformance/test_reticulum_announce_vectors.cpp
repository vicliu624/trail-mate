#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/infra/reticulum/announce_verifier.h"

#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{

struct AnnounceVector
{
    const char* name;
    const char* raw_hex;
    bool rebroadcast;
    bool ratchet;
    uint8_t header_type;
    uint8_t hops;
    std::size_t payload_len;
    std::size_t app_data_len;
    bool validate_announce;
};

constexpr std::array<uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};

uint32_t rotateRight(uint32_t value, uint8_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

uint32_t readBe32(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           static_cast<uint32_t>(data[3]);
}

void writeBe32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value >> 24U);
    out[1] = static_cast<uint8_t>(value >> 16U);
    out[2] = static_cast<uint8_t>(value >> 8U);
    out[3] = static_cast<uint8_t>(value);
}

void sha256(const uint8_t* data,
            std::size_t len,
            uint8_t out_hash[chat::reticulum::kFullHashSize])
{
    std::vector<uint8_t> padded;
    if (data && len != 0U)
    {
        padded.assign(data, data + len);
    }
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U)
    {
        padded.push_back(0x00U);
    }

    const uint64_t bit_len = static_cast<uint64_t>(len) * 8ULL;
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        padded.push_back(static_cast<uint8_t>(bit_len >> shift));
    }

    std::array<uint32_t, 8> state = {0x6a09e667UL,
                                     0xbb67ae85UL,
                                     0x3c6ef372UL,
                                     0xa54ff53aUL,
                                     0x510e527fUL,
                                     0x9b05688cUL,
                                     0x1f83d9abUL,
                                     0x5be0cd19UL};

    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64U)
    {
        std::array<uint32_t, 64> schedule = {};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            schedule[index] = readBe32(padded.data() + chunk + (index * 4U));
        }
        for (std::size_t index = 16U; index < 64U; ++index)
        {
            const uint32_t s0 = rotateRight(schedule[index - 15U], 7U) ^
                                rotateRight(schedule[index - 15U], 18U) ^
                                (schedule[index - 15U] >> 3U);
            const uint32_t s1 = rotateRight(schedule[index - 2U], 17U) ^
                                rotateRight(schedule[index - 2U], 19U) ^
                                (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 +
                              schedule[index - 7U] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];

        for (std::size_t index = 0; index < 64U; ++index)
        {
            const uint32_t s1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^
                                rotateRight(e, 25U);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 =
                h + s1 + ch + kSha256RoundConstants[index] + schedule[index];
            const uint32_t s0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^
                                rotateRight(a, 22U);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    for (std::size_t index = 0; index < state.size(); ++index)
    {
        writeBe32(out_hash + (index * 4U), state[index]);
    }
}

uint8_t hexNibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<uint8_t>(10 + value - 'a');
    }
    if (value >= 'A' && value <= 'F')
    {
        return static_cast<uint8_t>(10 + value - 'A');
    }
    throw std::runtime_error("invalid hex digit");
}

std::vector<uint8_t> fromHex(std::string_view hex)
{
    if ((hex.size() % 2U) != 0U)
    {
        throw std::runtime_error("odd hex string");
    }

    std::vector<uint8_t> bytes(hex.size() / 2U, 0);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<uint8_t>((hexNibble(hex[index * 2U]) << 4U) |
                                            hexNibble(hex[(index * 2U) + 1U]));
    }
    return bytes;
}

bool destinationHashValid(const chat::reticulum::ParsedPacket& packet,
                          const chat::reticulum::ParsedAnnounce& announce)
{
    uint8_t identity_full[chat::reticulum::kFullHashSize] = {};
    sha256(announce.public_key, chat::reticulum::kCombinedPublicKeySize, identity_full);

    std::array<uint8_t, chat::reticulum::kNameHashSize +
                            chat::reticulum::kTruncatedHashSize>
        material = {};
    std::memcpy(material.data(), announce.name_hash, chat::reticulum::kNameHashSize);
    std::memcpy(material.data() + chat::reticulum::kNameHashSize,
                identity_full,
                chat::reticulum::kTruncatedHashSize);

    uint8_t destination_full[chat::reticulum::kFullHashSize] = {};
    sha256(material.data(), material.size(), destination_full);
    return std::memcmp(destination_full,
                       packet.destination_hash,
                       chat::reticulum::kTruncatedHashSize) == 0;
}

bool signatureValid(const chat::reticulum::ParsedPacket& packet,
                    const chat::reticulum::ParsedAnnounce& announce)
{
    std::vector<uint8_t> signed_data;
    signed_data.reserve(chat::reticulum::kTruncatedHashSize +
                        chat::reticulum::kCombinedPublicKeySize +
                        chat::reticulum::kNameHashSize + 10U +
                        chat::reticulum::kRatchetSize + announce.app_data_len);
    signed_data.insert(signed_data.end(),
                       packet.destination_hash,
                       packet.destination_hash + chat::reticulum::kTruncatedHashSize);
    signed_data.insert(signed_data.end(),
                       announce.public_key,
                       announce.public_key + chat::reticulum::kCombinedPublicKeySize);
    signed_data.insert(signed_data.end(),
                       announce.name_hash,
                       announce.name_hash + chat::reticulum::kNameHashSize);
    signed_data.insert(signed_data.end(), announce.random_hash, announce.random_hash + 10U);
    if (announce.has_ratchet)
    {
        assert(announce.ratchet != nullptr);
        assert(announce.ratchet_len == chat::reticulum::kRatchetSize);
        signed_data.insert(signed_data.end(),
                           announce.ratchet,
                           announce.ratchet + announce.ratchet_len);
    }
    if (announce.app_data_len != 0U)
    {
        signed_data.insert(signed_data.end(),
                           announce.app_data,
                           announce.app_data + announce.app_data_len);
    }

    const uint8_t* sig_pub =
        announce.public_key + chat::reticulum::kEncryptionPublicKeySize;
    return ed25519_verify(announce.signature,
                          signed_data.data(),
                          signed_data.size(),
                          sig_pub) != 0;
}

bool validateAnnounceLikeReference(const chat::reticulum::ParsedPacket& packet,
                                   const chat::reticulum::ParsedAnnounce& announce)
{
    return destinationHashValid(packet, announce) && signatureValid(packet, announce);
}

void expectVector(const AnnounceVector& vector)
{
    assert(vector.name != nullptr);
    const std::vector<uint8_t> raw = fromHex(vector.raw_hex);

    chat::reticulum::ParsedPacket packet{};
    assert(chat::reticulum::parsePacket(raw.data(), raw.size(), &packet));
    assert(packet.valid);
    assert(packet.packet_type == chat::reticulum::PacketType::Announce);
    assert(packet.destination_type == chat::reticulum::DestinationType::Single);
    assert(packet.context == static_cast<uint8_t>(chat::reticulum::PacketContext::None));
    assert(packet.header_type == vector.header_type);
    assert(packet.hops == vector.hops);
    assert(packet.context_flag == static_cast<uint8_t>(vector.ratchet ? 1U : 0U));
    assert(packet.payload_len == vector.payload_len);
    assert(packet.payload != nullptr);
    assert(packet.destination_hash != nullptr);

    if (vector.rebroadcast)
    {
        assert(packet.header_len == chat::reticulum::kPacketHeader2Size);
        assert(packet.transport_id != nullptr);
        assert(packet.transport_type == chat::reticulum::TransportType::Transport);
    }
    else
    {
        assert(packet.header_len == chat::reticulum::kPacketHeader1Size);
        assert(packet.transport_id == nullptr);
        assert(packet.transport_type == chat::reticulum::TransportType::Broadcast);
    }

    chat::reticulum::ParsedAnnounce announce{};
    assert(chat::reticulum::parseAnnounce(packet, &announce));
    assert(announce.valid);
    assert(announce.has_ratchet == vector.ratchet);
    assert(announce.public_key == packet.payload);
    assert(announce.name_hash == announce.public_key + chat::reticulum::kCombinedPublicKeySize);
    assert(announce.random_hash == announce.name_hash + chat::reticulum::kNameHashSize);
    assert(announce.signature != nullptr);
    assert(announce.app_data != nullptr);
    assert(announce.app_data_len == vector.app_data_len);

    if (vector.ratchet)
    {
        assert(announce.ratchet == announce.random_hash + 10);
        assert(announce.ratchet_len == chat::reticulum::kRatchetSize);
        assert(announce.signature == announce.ratchet + chat::reticulum::kRatchetSize);
    }
    else
    {
        assert(announce.ratchet == nullptr);
        assert(announce.ratchet_len == 0);
        assert(announce.signature == announce.random_hash + 10);
    }

    assert(validateAnnounceLikeReference(packet, announce) == vector.validate_announce);
    chat::reticulum::AnnounceVerifier verifier;
    uint8_t identity[16], destination[16];
    const auto verify = [](const uint8_t* public_key, const uint8_t* signature, const uint8_t* data, size_t size)
    { return ed25519_verify(signature, data, size, public_key) != 0; };
    assert((verifier.verify(packet, announce, identity, destination, verify) == nullptr) == vector.validate_announce);
    assert(verifier.verify(packet, announce, identity, destination, nullptr));
    for (size_t index : {size_t(packet.destination_hash - raw.data()), packet.header_len,
                         packet.header_len + 64, packet.header_len + 74, raw.size() - 1})
    {
        auto changed = raw;
        changed[index] ^= 1;
        chat::reticulum::ParsedPacket tampered;
        assert(chat::reticulum::parsePacket(changed.data(), changed.size(), &tampered));
        assert(verifier.verify(tampered, announce, identity, destination, verify));
    }
}

} // namespace

int main()
{
    constexpr AnnounceVector vectors[] = {
        {"microreticulum_direct_announce",
         "0100f083a7f4b00d799808c44a4634bba7d7006afd960bf3b01801a2e88b2ce1f7040817dc1b6bffa366b103468f3988e0db7f00dc1fdc15fa7fd31a34a02207cfb4d26e11e57504e43686ec7fad84774bec88fd68805f2ea383c8d6f6c39824652e00698fd5fd1088b38832f247a9daebf017d8bfe641882d9fe9b37cf49a97402b7e3d8bec61b4950d39c0996588dd0288bf6a7a0a4390bb331bd82704b618f107cf8bf2230f",
         false,
         false,
         0,
         0,
         148,
         0,
         true},
        {"microreticulum_rebroadcast_announce",
         "510139745d39d5108615635d433d6cb14803f083a7f4b00d799808c44a4634bba7d7006afd960bf3b01801a2e88b2ce1f7040817dc1b6bffa366b103468f3988e0db7f00dc1fdc15fa7fd31a34a02207cfb4d26e11e57504e43686ec7fad84774bec88fd68805f2ea383c8d6f6c39824652e00698fd5fd1088b38832f247a9daebf017d8bfe641882d9fe9b37cf49a97402b7e3d8bec61b4950d39c0996588dd0288bf6a7a0a4390bb331bd82704b618f107cf8bf2230f",
         true,
         false,
         1,
         1,
         148,
         0,
         true},
        {"microreticulum_direct_ratchet_announce",
         "21003dc438c85235151be9a59020807930d200a3dc290f674385c482eeac8da9108c20d5d30b9d20680d2a39bf3d95d6fcb04f1e2bd080869ca0b7f3ca0271205899635b94e19f2463b77e5c4f60e82287ab5e6ec60bc318e2c0f0d9087a321643c100698fd5f11045f113f98185b9f5b01de11f59f21848f7244bcbc54bfe5bad99f3a6e0d2264b78687aa12e62b9ad581161acc9202bcce4978bdc68a73595e908b2396c8152c689d6c3abf99081dd00727f4744caaf76aaf7978c3866e31577c6e6c066830092c40e416e6f6e796d6f75732050656572c0",
         false,
         true,
         0,
         0,
         198,
         18,
         true},
        {"microreticulum_rebroadcast_ratchet_announce",
         "710139745d39d5108615635d433d6cb148033dc438c85235151be9a59020807930d200a3dc290f674385c482eeac8da9108c20d5d30b9d20680d2a39bf3d95d6fcb04f1e2bd080869ca0b7f3ca0271205899635b94e19f2463b77e5c4f60e82287ab5e6ec60bc318e2c0f0d9087a321643c100698fd5f11045f113f98185b9f5b01de11f59f21848f7244bcbc54bfe5bad99f3a6e0d2264b78687aa12e62b9ad581161acc9202bcce4978bdc68a73595e908b2396c8152c689d6c3abf99081dd00727f4744caaf76aaf7978c3866e31577c6e6c066830092c40e416e6f6e796d6f75732050656572c0",
         true,
         true,
         1,
         1,
         198,
         18,
         true},
    };

    for (const auto& vector : vectors)
    {
        expectVector(vector);
    }

    return 0;
}
