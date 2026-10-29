#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/linux_meshcore_advert_wire.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/runtime_paths.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <sqlite3.h>
#include <unistd.h>
#include <vector>

using Identity = platform::linux_runtime::LinuxMeshCoreIdentity;
namespace wire = trailmate::linux_app::meshcore_wire;
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-mc-identity-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    Identity a;
    assert(!a.ready() && a.nodeId() == 0);
    assert(a.begin() && a.ready() && a.nodeId());
    const auto pub = a.publicKey();
    Identity reloaded;
    assert(reloaded.begin() && reloaded.publicKey() == pub && reloaded.nodeId() == a.nodeId());
    // Race creation in an empty state root, not only reads of an existing key.
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "race").c_str(), 1);
    std::vector<std::future<std::array<uint8_t, 32>>> concurrent;
    for (unsigned index = 0; index < 6; ++index)
        concurrent.push_back(std::async(std::launch::async, []
                                        {
            Identity value;
            assert(value.begin());
            return value.publicKey(); }));
    const auto created = concurrent.front().get();
    for (unsigned index = 1; index < concurrent.size(); ++index) assert(concurrent[index].get() == created);
    assert(created != pub);
    uint8_t signature[64]{};
    const uint8_t message[] = {1, 2, 3};
    assert(a.sign(message, sizeof(message), signature));
    assert(chat::meshcore::meshcoreVerify(pub.data(), signature, message, sizeof(message)));
    signature[0] ^= 1;
    assert(!chat::meshcore::meshcoreVerify(pub.data(), signature, message, sizeof(message)));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "other").c_str(), 1);
    Identity b;
    assert(b.begin() && b.publicKey() != pub);
    uint8_t ab[32]{}, ba[32]{};
    assert(a.sharedSecret(b.publicKey().data(), ab));
    assert(b.sharedSecret(a.publicKey().data(), ba));
    assert(!std::memcmp(ab, ba, sizeof(ab)));
    chat::MeshConfig config;
    for (auto profile : {chat::MeshCorePayloadSendProfile::V1Only, chat::MeshCorePayloadSendProfile::V2Only})
        for (bool broadcast : {false, true})
        {
            config.meshcore_send_profile = profile;
            wire::Frame frame;
            assert(wire::encodeAdvert(a, config, "Signed peer", 1700000000U, broadcast, frame));
            mesh::MeshProtocolEvent received;
            assert(wire::decodeAdvert(frame.bytes.data(), frame.size, received));
            assert(received.peer.value == a.nodeId());
            assert(received.advert.timestamp == 1700000000U);
            assert(received.advert.has_name && std::string(received.advert.name) == "Signed peer");
            assert(!std::memcmp(received.peer_key.public_key, pub.data(), pub.size()));
            auto corrupt = frame;
            corrupt.bytes[corrupt.size - 1] ^= 1;
            assert(!wire::decodeAdvert(corrupt.bytes.data(), corrupt.size, received));
            // A bare public key is not a signed identity announcement.
            assert(!wire::decodeAdvert(frame.bytes.data(), 34, received));
        }
    // A corrupt persisted record must fail closed without replacing the key.
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    sqlite3* database = nullptr;
    assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().c_str(), &database) == SQLITE_OK);
    assert(sqlite3_exec(database, "UPDATE linux_protocol_identity SET public_key=zeroblob(32) WHERE protocol='meshcore'", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(database);
    Identity corrupt;
    assert(!corrupt.begin() && !corrupt.ready() && corrupt.nodeId() == 0);
    assert(std::string(corrupt.lastError()).find("match") != std::string::npos);
    assert(a.publicKey() == pub); // Initialized owners retain their established key.
    // Independent Ed25519 fixture generated with Python cryptography.
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "fixture").c_str(), 1);
    std::filesystem::create_directories(root / "fixture");
    assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().c_str(), &database) == SQLITE_OK);
    assert(sqlite3_exec(database,
                        "CREATE TABLE linux_protocol_identity(protocol TEXT PRIMARY KEY,seed BLOB NOT NULL,public_key BLOB NOT NULL);"
                        "INSERT INTO linux_protocol_identity VALUES('meshcore',"
                        "X'000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F',"
                        "X'03A107BFF3CE10BE1D70DD18E74BC09967E4D6309BA50D5F1DDC8664125531B8');",
                        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(database);
    Identity fixture;
    assert(fixture.begin() && fixture.sign(message, sizeof(message), signature));
    assert(chat::meshcore::toHex(signature, sizeof(signature)) ==
           "3A36B413B5AB3AB4B61D4AE0C1186A557AB72F04B057EAE3CB7A518520747240"
           "80E2BBFE1654FC8885A854B9E8DF1A886B6A19A20DB70D362ECDA1891B50AC0A");
    std::filesystem::remove_all(root);
}
