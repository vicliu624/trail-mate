#include "chat/linux_reticulum_links.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unistd.h>

using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
namespace r = chat::reticulum;
void printHex(const uint8_t* bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) std::printf("%02x", bytes[i]);
}
int main(int argc, char** argv)
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-outgoing-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        Identity author(root / "author.db"), recipient(root / "recipient.db");
        assert(author.begin() && recipient.begin());
        auto key = recipient.publicKey();
        if (argc == 2)
        {
            assert(std::strlen(argv[1]) == 128);
            for (size_t i = 0; i < key.size(); ++i)
            {
                unsigned value;
                assert(std::sscanf(argv[1] + 2 * i, "%2x", &value) == 1);
                key[i] = uint8_t(value);
            }
        }
        uint8_t recipient_identity[16], name[10], destination[16];
        r::computeIdentityHash(key.data(), recipient_identity);
        r::computeNameHash("lxmf", "delivery", name);
        r::computeDestinationHash(name, recipient_identity, destination);
        Links sender(author), receiver(recipient);
        std::string text = "Native encrypted mail";
        text.resize(40000, 'x');
        chat::lxmf::runtime::ResourcePayloadBuffer encrypted, second;
        uint8_t message_hash[32], transient[32], second_hash[32], second_transient[32];
        assert(sender.encodePropagatedText(destination, key.data(), text, 123, encrypted, message_hash, transient));
        assert(sender.encodePropagatedText(destination, key.data(), text, 123, second, second_hash, second_transient));
        assert(encrypted != second && std::memcmp(transient, second_transient, 32) && !std::memcmp(message_hash, second_hash, 32));
        uint8_t calculated[32];
        r::fullHash(encrypted.data(), encrypted.size(), calculated);
        assert(!std::memcmp(calculated, transient, 32));
        if (argc == 2)
        {
            std::printf("{\"author\":\"");
            printHex(author.publicKey().data(), 64);
            std::printf("\",\"message\":\"");
            printHex(encrypted.data(), encrypted.size());
            std::printf("\",\"message_hash\":\"");
            printHex(message_hash, 32);
            std::printf("\",\"transient\":\"");
            printHex(transient, 32);
            std::printf("\"}\n");
        }
        else
        {
            receiver.setIncomingHooks(
                nullptr, [](void* context, const uint8_t* source, uint8_t* key, bool* ignored)
                {
                                          const auto& identity = *static_cast<Identity*>(context);
                                          *ignored = false;
                                          if (std::memcmp(source, identity.destinationHash().data(), 16)) return false;
                                          std::memcpy(key, identity.publicKey().data(), 64);
                                          return true; },
                &author);
            chat::MeshIncomingText incoming;
            assert(receiver.decodePropagatedText(encrypted.data(), encrypted.size(), nullptr, incoming, calculated));
            assert(incoming.text == text && !std::memcmp(calculated, message_hash, 32));
            const auto preserved = encrypted;
            auto wrong = key;
            wrong[0] ^= 1;
            assert(!sender.encodePropagatedText(destination, wrong.data(), text, 123, encrypted, second_hash, second_transient));
            assert(!sender.encodePropagatedText(destination, key.data(), "", 123, encrypted, second_hash, second_transient));
            assert(!sender.encodePropagatedText(destination, key.data(), text, NAN, encrypted, second_hash, second_transient));
            assert(encrypted == preserved && !std::memcmp(second_hash, message_hash, 32));
        }
    }
    std::filesystem::remove_all(root);
}
