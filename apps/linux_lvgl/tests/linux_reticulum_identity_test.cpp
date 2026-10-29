#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <future>
#include <sqlite3.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

using Identity = platform::linux_runtime::LinuxReticulumIdentity;
template <size_t N>
std::array<uint8_t, N> hex(std::string_view text)
{
    assert(text.size() == N * 2);
    std::array<uint8_t, N> out{};
    const auto digit = [](char c)
    { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < N; ++i) out[i] = static_cast<uint8_t>((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
    return out;
}
struct Database
{
    sqlite3* db = nullptr;
    explicit Database(const std::filesystem::path& path) { assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK); }
    ~Database() { sqlite3_close(db); }
    void exec(const char* sql) { assert(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    void fixture(const std::array<uint8_t, 64>& private_key, const std::array<uint8_t, 64>& public_key)
    {
        sqlite3_stmt* statement = nullptr;
        assert(sqlite3_prepare_v2(db, "UPDATE linux_reticulum_identity SET private_key=?1,public_key=?2", -1, &statement, nullptr) == SQLITE_OK);
        assert(sqlite3_bind_blob(statement, 1, private_key.data(), private_key.size(), SQLITE_STATIC) == SQLITE_OK);
        assert(sqlite3_bind_blob(statement, 2, public_key.data(), public_key.size(), SQLITE_STATIC) == SQLITE_OK);
        assert(sqlite3_step(statement) == SQLITE_DONE);
        sqlite3_finalize(statement);
    }
};
int main()
{
    char root[] = "/tmp/trailmate-reticulum-identity-XXXXXX";
    assert(mkdtemp(root));
    const auto path = std::filesystem::path(root) / "identity.db";
    Identity initial(path);
    uint8_t signature[64], secret[32];
    assert(!initial.sign(nullptr, 0, signature) && std::all_of(std::begin(signature), std::end(signature), [](uint8_t b)
                                                               { return b == 0; }));
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    assert(!initial.begin() && !initial.ready() && !std::filesystem::exists(path));
    assert(!initial.sharedSecret(nullptr, secret));
    assert(std::all_of(std::begin(secret), std::end(secret), [](uint8_t b)
                       { return b == 0; }));
    std::filesystem::remove_all(root);
    return 0;
#else
    assert(initial.begin() && initial.ready() && initial.generation() == 1);
    assert(chmod(path.c_str(), 0666) == 0);
    Identity reopened(path);
    assert(reopened.begin() && reopened.publicKey() == initial.publicKey() && reopened.identityHash() == initial.identityHash() && reopened.destinationHash() == initial.destinationHash());
    struct stat file_status
    {
    };
    assert(stat(path.c_str(), &file_status) == 0 && (file_status.st_mode & 0777) == 0600);
    assert(initial.sign(reinterpret_cast<const uint8_t*>("message"), 7, signature));
    assert(Identity::verify(initial.publicKey().data() + 32, reinterpret_cast<const uint8_t*>("message"), 7, signature));
    assert(!Identity::verify(initial.publicKey().data() + 32, reinterpret_cast<const uint8_t*>("changed"), 7, signature));
    Identity peer(std::filesystem::path(root) / "peer.db");
    assert(peer.begin() && peer.publicKey() != initial.publicKey());
    uint8_t reverse[32];
    assert(initial.sharedSecret(peer.publicKey().data(), secret) && peer.sharedSecret(initial.publicKey().data(), reverse) && !std::memcmp(secret, reverse, sizeof(secret)));
    uint8_t zero[32]{};
    assert(!initial.sharedSecret(zero, secret) && !std::memcmp(secret, zero, sizeof(secret)));
    assert(!initial.sign(nullptr, 1, signature) && std::all_of(std::begin(signature), std::end(signature), [](uint8_t b)
                                                               { return b == 0; }));
    const auto public_before = initial.publicKey();
    assert(!initial.sign(nullptr, 0, const_cast<uint8_t*>(initial.publicKey().data())) && initial.publicKey() == public_before);

    // Independent published vectors: RFC 7748 section 6.1 and RFC 8032 test 1.
    const auto private_key = hex<64>("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"
                                     "9d61b19deffd5a60ba844af492ec2cc4"
                                     "4449c5697b326919703bac031cae7f60");
    const auto public_key = hex<64>("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"
                                    "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    Database db(path);
    db.fixture(private_key, public_key);
    Identity reference(path);
    assert(reference.begin() && reference.publicKey() == public_key);
    assert(reference.identityHash() == hex<16>("48f7e3807dce41a286611331ddfbe99d"));
    assert(reference.destinationHash() == hex<16>("91eb19b62a69a9913d50e49f6376e7a0"));
    assert(reference.sign(nullptr, 0, signature));
    const auto expected_signature = hex<64>("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                                            "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    assert(!std::memcmp(signature, expected_signature.data(), sizeof(signature)));
    const auto bob = hex<32>("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const auto expected_secret = hex<32>("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    assert(reference.sharedSecret(bob.data(), secret) && !std::memcmp(secret, expected_secret.data(), sizeof(secret)));
    db.exec("UPDATE linux_reticulum_identity SET public_key=zeroblob(64)");
    Identity corrupted(path);
    assert(!corrupted.begin() && !corrupted.ready());
    db.fixture(private_key, public_key);
    db.exec("UPDATE linux_reticulum_identity SET version=3");
    Identity future(path);
    assert(!future.begin());
    db.exec("UPDATE linux_reticulum_identity SET version=1; DELETE FROM linux_reticulum_identity");
    Identity missing(path);
    assert(!missing.begin());
    // Two fresh openers converge on the same committed key, without publishing
    // either transaction's provisional key before its COMMIT.
    const auto concurrent_path = std::filesystem::path(root) / "concurrent.db";
    auto other = std::async(std::launch::async, [&]
                            { Identity id(concurrent_path); assert(id.begin()); return id.publicKey(); });
    Identity concurrent(concurrent_path);
    assert(concurrent.begin() && concurrent.publicKey() == other.get());
    const auto locked_path = std::filesystem::path(root) / "locked.db";
    Database locked(locked_path);
    locked.exec("BEGIN IMMEDIATE");
    Identity busy(locked_path);
    assert(!busy.begin() && !busy.ready());
    locked.exec("ROLLBACK");
    assert(busy.begin());
    Identity memory(":memory:");
    assert(!memory.begin());
    std::filesystem::remove_all(root);
#endif
}
