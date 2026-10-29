#include "chat/infra/meshtastic/mt_pki_crypto.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
std::string hex(const uint8_t* bytes, size_t size)
{
    std::string output;
    for (size_t index = 0; index < size; ++index)
    {
        output += "0123456789abcdef"[bytes[index] >> 4];
        output += "0123456789abcdef"[bytes[index] & 15];
    }
    return output;
}
} // namespace
int main()
{
    namespace mt = chat::meshtastic;
    std::array<uint8_t, 32> key{};
    std::array<uint8_t, mt::kPkiNonceSize> nonce{};
    for (size_t index = 0; index < key.size(); ++index) key[index] = static_cast<uint8_t>(index);
    for (size_t index = 0; index < nonce.size(); ++index) nonce[index] = static_cast<uint8_t>(index);
    const uint8_t plain[]{'n', 'a', 't', 'i', 'v', 'e', ' ', 'p', 'k', 'i'}, aad[]{'c', 't', 'x'};
    // Independent Python cryptography AESCCM vectors; the protocol uses nonce[:13].
    const char* vectors[]{"7855c0e1256cdaf231fe9f20fcf81884146e", "7855c0e1256cdaf231fe7ba9a7ac134801024cbe6ff0f0cbf57e",
                          "7266df0f7acd5af8aab9869e3264644d2cec", "7266df0f7acd5af8aab9927ded129ae8ddd068f2ec8d81dc2ee1"};
    unsigned vector = 0;
    for (const size_t key_size : {size_t(16), size_t(32)})
        for (const size_t tag_size : {size_t(8), size_t(16)})
        {
            std::array<uint8_t, 32> cipher{}, tag{}, recovered{};
            require(mt::encryptPkiAesCcm(key.data(), key_size, nonce.data(), tag_size, aad, sizeof(aad), plain, sizeof(plain), cipher.data(), tag.data()), "OpenSSL CCM encryption failed");
            require(hex(cipher.data(), sizeof(plain)) + hex(tag.data(), tag_size) == vectors[vector++], "CCM differs from independent vector");
            require(mt::decryptPkiAesCcm(key.data(), key_size, nonce.data(), tag_size, cipher.data(), sizeof(plain), aad, sizeof(aad), tag.data(), recovered.data()) &&
                        !std::memcmp(recovered.data(), plain, sizeof(plain)),
                    "CCM decrypt failed");
            tag[0] ^= 1;
            require(!mt::decryptPkiAesCcm(key.data(), key_size, nonce.data(), tag_size, cipher.data(), sizeof(plain), aad, sizeof(aad), tag.data(), recovered.data()), "CCM accepted altered tag");
            tag[0] ^= 1;
            auto changed_nonce = nonce;
            changed_nonce[0] ^= 1;
            require(!mt::decryptPkiAesCcm(key.data(), key_size, changed_nonce.data(), tag_size, cipher.data(), sizeof(plain), aad, sizeof(aad), tag.data(), recovered.data()), "CCM accepted altered nonce");
            require(mt::encryptPkiAesCcm(key.data(), key_size, nonce.data(), tag_size, nullptr, 0, plain, sizeof(plain), cipher.data(), tag.data()) &&
                        mt::decryptPkiAesCcm(key.data(), key_size, nonce.data(), tag_size, cipher.data(), sizeof(plain), nullptr, 0, tag.data(), recovered.data()),
                    "CCM empty AAD failed");
        }
}
