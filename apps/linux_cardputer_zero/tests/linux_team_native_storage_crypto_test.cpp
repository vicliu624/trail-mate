#include "platform/linux/team_protocol_crypto.h"
#include "platform/linux/team_snapshot_store.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

std::vector<uint8_t> hex(const char* text)
{
    std::vector<uint8_t> result;
    while (*text)
    {
        unsigned value = 0;
        assert(std::sscanf(text, "%2x", &value) == 1);
        result.push_back(static_cast<uint8_t>(value));
        text += 2;
    }
    return result;
}

int main()
{
    using namespace platform::linux_runtime;
    // RFC 8439 section 2.8.2: independent published AEAD interoperability vector.
    TeamProtocolCrypto crypto;
    const auto key = hex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    const auto nonce = hex("070000004041424344454647");
    const auto aad = hex("50515253c0c1c2c3c4c5c6c7");
    const char* text = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    const auto expected = hex("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691");
    std::vector<uint8_t> cipher, plain;
    assert(crypto.aeadEncrypt(key.data(), key.size(), nonce.data(), nonce.size(), aad.data(), aad.size(),
                              reinterpret_cast<const uint8_t*>(text), std::strlen(text), cipher));
    assert(cipher == expected);
    assert(crypto.aeadDecrypt(key.data(), key.size(), nonce.data(), nonce.size(), aad.data(), aad.size(), cipher.data(), cipher.size(), plain));
    assert(std::string(plain.begin(), plain.end()) == text);
    cipher.back() ^= 1;
    assert(!crypto.aeadDecrypt(key.data(), key.size(), nonce.data(), nonce.size(), aad.data(), aad.size(), cipher.data(), cipher.size(), plain));
    assert(plain.empty());

    const auto root = std::filesystem::temp_directory_path() / ("trailmate-team-store-" + std::to_string(::getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "team.snapshot";
    team::ui::TeamUiSnapshot value;
    value.in_team = value.has_team_id = value.has_team_psk = value.self_is_leader = true;
    value.team_id = {1, 2, 3, 4, 5, 6, 7, 8};
    value.team_name = "Native Team";
    value.security_round = 17;
    value.pending_join = value.kicked_out = true;
    value.pending_join_started_s = 12345;
    for (unsigned i = 0; i < 16; ++i) value.team_psk[i] = static_cast<uint8_t>(i + 11);
    {
        TeamSnapshotStore store(path);
        store.save(value);
    }
    team::ui::TeamUiSnapshot loaded;
    TeamSnapshotStore restarted(path);
    assert(restarted.load(loaded));
    assert(loaded.in_team && loaded.self_is_leader && loaded.has_team_psk && loaded.has_team_id);
    assert(loaded.team_id == value.team_id && loaded.team_psk == value.team_psk);
    assert(loaded.team_name == value.team_name && loaded.security_round == 17);
    assert(loaded.pending_join && loaded.kicked_out && loaded.pending_join_started_s == 12345);
    struct stat status
    {
    };
    assert(::stat(path.c_str(), &status) == 0 && (status.st_mode & 0777) == 0600);
    {
        std::ofstream broken(path, std::ios::binary | std::ios::trunc);
        broken << "LTS1";
    }
    assert(!restarted.load(loaded));
    restarted.clear();
    assert(!restarted.load(loaded));
    std::filesystem::remove(root);
}
