#include "chat/linux_reticulum_links.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace r = chat::reticulum;
namespace state = chat::lxmf::runtime;
LinuxReticulumLinks::QueueResult LinuxReticulumLinks::queueCustom(const uint8_t destination[16], const uint8_t public_key[64], const char* type,
                                                                  ::chat::lxmf::ByteSpan data, ::chat::MessageId id, double timestamp, uint32_t now, Sender sender, void* context,
                                                                  uint8_t expected_hops, const uint8_t* next_hop, uint8_t accepted_hash[32])
{
    if (!id || !identity_.ready() || !destination || !public_key || !type || !*type || strnlen(type, 65) > 64 ||
        !data.data || !data.size || data.size > 8192 || !std::isfinite(timestamp) || timestamp < 0 || timestamp > UINT32_MAX) return QueueResult::Invalid;
    const auto pending = pendingOutboundIds();
    if (std::find(pending.begin(), pending.end(), id) != pending.end()) return QueueResult::Pending;
    if (pending.size() >= 32) return QueueResult::Busy;
    const auto custom_slot = std::find(custom_message_ids_.begin(), custom_message_ids_.end(), 0);
    if (custom_slot == custom_message_ids_.end()) return QueueResult::Busy;
    state::ResourcePayloadBuffer body(data.size + 256), domain(data.size + 320), envelope(data.size + 384);
    size_t body_size = body.size(), domain_size = domain.size(), envelope_size = envelope.size();
    std::array<uint8_t, 32> hash{};
    std::array<uint8_t, 64> signature{};
    if (!::chat::lxmf::encodeCustomDataPayload(timestamp, "", "", type, data, body.data(), &body_size) ||
        !::chat::lxmf::buildSignedPart(destination, identity_.destinationHash().data(), body.data(), body_size,
                                       domain.data(), &domain_size, hash.data()) ||
        !identity_.sign(domain.data(), domain_size, signature.data()) ||
        !::chat::lxmf::packMessage(destination, identity_.destinationHash().data(), signature.data(), body.data(), body_size,
                                   envelope.data(), &envelope_size)) return QueueResult::Invalid;
    envelope.resize(envelope_size);
    const auto result = queueEnvelope(destination, public_key, envelope, id, now, sender, context, expected_hops, next_hop);
    if (result == QueueResult::Queued)
    {
        *custom_slot = id;
        if (accepted_hash) std::copy_n(hash.data(), hash.size(), accepted_hash);
    }
    return result;
}
bool LinuxReticulumLinks::buildTextEnvelope(const uint8_t destination[16], const std::string& text, double timestamp,
                                            state::ResourcePayloadBuffer& output, uint8_t message_hash[32]) const
{
    if (!identity_.ready() || !destination || !message_hash || !textFitsTransport(text) ||
        !std::isfinite(timestamp) || timestamp < 0 || timestamp > UINT32_MAX) return false;
    state::ResourcePayloadBuffer body(text.size() + 128), domain(text.size() + 128), envelope(text.size() + 128);
    uint8_t hash[32], signature[64];
    size_t body_size = body.size(), domain_size = domain.size(), envelope_size = envelope.size();
    if (!chat::lxmf::encodeTextPayload(timestamp, "", text.c_str(), body.data(), &body_size) ||
        !chat::lxmf::buildSignedPart(destination, identity_.destinationHash().data(), body.data(), body_size, domain.data(), &domain_size, hash) ||
        !identity_.sign(domain.data(), domain_size, signature) ||
        !chat::lxmf::packMessage(destination, identity_.destinationHash().data(), signature, body.data(), body_size, envelope.data(), &envelope_size)) return false;
    envelope.resize(envelope_size);
    output = std::move(envelope);
    std::memcpy(message_hash, hash, sizeof(hash));
    return true;
}
bool LinuxReticulumLinks::encodePropagatedText(const uint8_t destination[16], const uint8_t public_key[64], const std::string& text,
                                               double timestamp, state::ResourcePayloadBuffer& output, uint8_t message_hash[32], uint8_t transient_id[32]) const
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!destination || !public_key || !message_hash || !transient_id) return false;
    uint8_t recipient[16], name[10], expected[16];
    r::computeIdentityHash(public_key, recipient);
    r::computeNameHash("lxmf", "delivery", name);
    r::computeDestinationHash(name, recipient, expected);
    if (std::memcmp(destination, expected, 16)) return false;
    state::ResourcePayloadBuffer envelope;
    uint8_t original_hash[32];
    if (!buildTextEnvelope(destination, text, timestamp, envelope, original_hash)) return false;
    struct Secret
    {
        uint8_t shared[32]{}, key[r::kDerivedTokenKeySize]{};
        ~Secret()
        {
            OPENSSL_cleanse(shared, sizeof(shared));
            OPENSSL_cleanse(key, sizeof(key));
        }
    } secret;
    EVP_PKEY_CTX* generator = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    EVP_PKEY* ephemeral = nullptr;
    bool ok = generator && EVP_PKEY_keygen_init(generator) == 1 && EVP_PKEY_keygen(generator, &ephemeral) == 1;
    EVP_PKEY_CTX_free(generator);
    uint8_t ephemeral_public[32], iv[r::kTokenIvSize];
    size_t public_size = sizeof(ephemeral_public), shared_size = sizeof(secret.shared);
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, public_key, 32);
    EVP_PKEY_CTX* exchange = ephemeral ? EVP_PKEY_CTX_new(ephemeral, nullptr) : nullptr;
    ok = ok && peer && exchange && EVP_PKEY_get_raw_public_key(ephemeral, ephemeral_public, &public_size) == 1 && public_size == 32 &&
         EVP_PKEY_derive_init(exchange) == 1 && EVP_PKEY_derive_set_peer(exchange, peer) == 1 &&
         EVP_PKEY_derive(exchange, secret.shared, &shared_size) == 1 && shared_size == 32;
    EVP_PKEY_CTX_free(exchange);
    EVP_PKEY_free(peer);
    EVP_PKEY_free(ephemeral);
    if (!ok || !r::hkdfSha256(secret.shared, sizeof(secret.shared), recipient, sizeof(recipient), nullptr, 0, secret.key, sizeof(secret.key)) ||
        RAND_bytes(iv, sizeof(iv)) != 1) return false;
    state::ResourcePayloadBuffer encrypted(envelope.size() + 32 + r::kTokenOverhead + 16);
    std::memcpy(encrypted.data(), destination, 16);
    std::memcpy(encrypted.data() + 16, ephemeral_public, sizeof(ephemeral_public));
    size_t token_size = encrypted.size() - 48;
    if (!r::tokenEncrypt(secret.key, iv, envelope.data() + 16, envelope.size() - 16, encrypted.data() + 48, &token_size)) return false;
    encrypted.resize(48 + token_size);
    if (encrypted.size() > state::kMaxAdvertisedResourceBytes - 128) return false;
    uint8_t transient[32];
    r::fullHash(encrypted.data(), encrypted.size(), transient);
    output = std::move(encrypted);
    std::memcpy(message_hash, original_hash, 32);
    std::memcpy(transient_id, transient, 32);
    return true;
#else
    (void)destination;
    (void)public_key;
    (void)text;
    (void)timestamp;
    (void)output;
    (void)message_hash;
    (void)transient_id;
    return false;
#endif
}
} // namespace trailmate::linux_app
