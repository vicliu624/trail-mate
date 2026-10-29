#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/linux_reticulum_links.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <unistd.h>

using Identity = platform::linux_runtime::LinuxReticulumIdentity;
namespace r = chat::reticulum;
std::vector<uint8_t> encrypted(Identity& recipient, Identity& author, Identity& ephemeral, bool valid_signature)
{
    uint8_t body[256], domain[384], envelope[512], signature[64], hash[32], shared[32], key[r::kDerivedTokenKeySize], iv[16]{};
    size_t body_size = sizeof(body), domain_size = sizeof(domain), envelope_size = sizeof(envelope);
    assert(chat::lxmf::encodeTextPayload(123, "", "original author", body, &body_size));
    assert(chat::lxmf::buildSignedPart(recipient.destinationHash().data(), author.destinationHash().data(), body, body_size, domain, &domain_size, hash));
    assert(author.sign(domain, domain_size, signature));
    if (!valid_signature) signature[0] ^= 1;
    assert(chat::lxmf::packMessage(recipient.destinationHash().data(), author.destinationHash().data(), signature, body, body_size, envelope, &envelope_size));
    assert(ephemeral.sharedSecret(recipient.publicKey().data(), shared));
    assert(r::hkdfSha256(shared, sizeof(shared), recipient.identityHash().data(), 16, nullptr, 0, key, sizeof(key)));
    std::vector<uint8_t> message(1024);
    std::memcpy(message.data(), recipient.destinationHash().data(), 16);
    std::memcpy(message.data() + 16, ephemeral.publicKey().data(), 32);
    size_t token_size = message.size() - 48;
    assert(r::tokenEncrypt(key, iv, envelope + 16, envelope_size - 16, message.data() + 48, &token_size));
    message.resize(48 + token_size);
    return message;
}
int main()
{
    char root[] = "/tmp/trailmate-propagated-text-XXXXXX";
    assert(mkdtemp(root));
    {
        Identity recipient(std::filesystem::path(root) / "recipient.db"), author(std::filesystem::path(root) / "author.db"), ephemeral(std::filesystem::path(root) / "ephemeral.db");
        assert(recipient.begin() && author.begin() && ephemeral.begin());
        trailmate::linux_app::LinuxReticulumLinks links(recipient);
        struct Resolver
        {
            Identity& author;
            bool known = true, ignored = false;
            unsigned deliveries = 0;
        } resolver{author};
        links.setIncomingHooks([](void* context, const chat::MeshIncomingText&)
                               { ++static_cast<Resolver*>(context)->deliveries; return true; },
                               [](void* context, const uint8_t* destination, uint8_t* key, bool* ignored)
                               {
                                   auto& state = *static_cast<Resolver*>(context);
                                   *ignored = state.ignored;
                                   if (!state.known || std::memcmp(destination, state.author.destinationHash().data(), 16)) return false;
                                   std::memcpy(key, state.author.publicKey().data(), 64);
                                   return true;
                               },
                               &resolver);
        auto message = encrypted(recipient, author, ephemeral, true);
        chat::MeshIncomingText incoming;
        uint8_t hash[32]{};
        assert(links.decodePropagatedText(message.data(), message.size(), nullptr, incoming, hash));
        assert(incoming.text == "original author" && incoming.encrypted && !incoming.source_unverified);
        assert(!std::memcmp(incoming.reticulum_identity.destination_hash, author.destinationHash().data(), 16));
        assert(!std::memcmp(hash, incoming.reticulum_lxmf_hash, 32));
        assert(resolver.deliveries == 0 && links.statistics().pending_incoming == 0);
        // Failed validation must not mutate caller-owned acceptance state.
        const auto original_hash = std::vector<uint8_t>(hash, hash + 32);
        incoming.text = "unchanged";
        resolver.known = false;
        assert(!links.decodePropagatedText(message.data(), message.size(), nullptr, incoming, hash));
        resolver.known = true;
        resolver.ignored = true;
        assert(!links.decodePropagatedText(message.data(), message.size(), nullptr, incoming, hash));
        resolver.ignored = false;
        auto invalid = encrypted(recipient, author, ephemeral, false);
        assert(!links.decodePropagatedText(invalid.data(), invalid.size(), nullptr, incoming, hash));
        message.back() ^= 1;
        assert(!links.decodePropagatedText(message.data(), message.size(), nullptr, incoming, hash));
        message[0] ^= 1;
        assert(!links.decodePropagatedText(message.data(), message.size(), nullptr, incoming, hash));
        assert(incoming.text == "unchanged" && !std::memcmp(hash, original_hash.data(), 32) && resolver.deliveries == 0);
    }
    std::filesystem::remove_all(root);
}
