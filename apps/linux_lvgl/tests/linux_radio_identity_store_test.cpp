#include "chat/linux_raw_lora_mesh_adapter.h"
#include "platform/linux/identity_owner_lease.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/mesh/linux_radio_identity_store.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/settings_store.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <openssl/evp.h>
#include <sqlite3.h>
#include <unistd.h>

namespace native = platform::linux_runtime;
using Failure = mesh::StoreFailure;
void sql(const char* text, const uint8_t* bytes = nullptr, size_t size = 0)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(native::sqlite_database_path().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(db, text, -1, &statement, nullptr) == SQLITE_OK);
    if (bytes) assert(sqlite3_bind_blob(statement, 1, bytes, int(size), SQLITE_STATIC) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
    sqlite3_close(db);
}
struct Radio final : native::IPacketRadio
{
    bool online = false;
    native::RadioSettings settings;
    const char* name() const override { return "injected PKI fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override { return online = true; }
    void release() override { online = false; }
    bool configureLoRa(const native::RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override { return true; }
    bool pollReceive(native::RadioPacket*) override { return false; }
    float readRssi() override { return -100; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "fixture"; }
    native::RadioSettings appliedLoRaConfig() const override { return settings; }
    native::RadioStatistics stats() const override { return {}; }
};
int main()
{
    char directory[] = "/tmp/trailmate-radio-identity-XXXXXX";
    assert(mkdtemp(directory));
    setenv("TRAIL_MATE_SETTINGS_ROOT", directory, 1);
    const auto path = native::sqlite_database_path();
    native::LinuxRadioIdentityStore store(path);
    native::RadioIdentitySnapshot out, saved;
    assert(store.read(1, out).ok && !out.present && out.generation == 0 && !std::filesystem::exists(path));
    assert(store.read(2, out).ok && !out.present);
    assert(store.read(3, out).failure == Failure::InvalidArgument);
    native::IdentityOwnerLease writer;
    {
        native::LinuxMeshCoreIdentity seed;
        assert(seed.begin());
        assert(store.read(1, out).ok && out.present && out.private_size == 64 && out.generation == 0 && out.public_key == seed.publicKey());
        saved = out;
        assert(writer.acquireShared(native::radio_identity_owner_path(path)) && !writer.acquireExclusive());
    }
    assert(writer.acquireExclusive());
    assert(store.read(1, out).failure == Failure::IoError && !out.present && out.private_size == 0);
    {
        native::LinuxMeshCoreIdentity blocked;
        assert(!blocked.begin());
    }
    sql("UPDATE linux_protocol_identity SET seed=?1 WHERE protocol='meshcore'", saved.private_key.data(), 64);
    writer.reset();
    {
        native::LinuxMeshCoreIdentity expanded;
        assert(expanded.begin() && expanded.publicKey() == saved.public_key);
        const uint8_t message[]{1, 2, 3};
        uint8_t signature[64];
        assert(expanded.sign(message, sizeof(message), signature));
        assert(native::LinuxReticulumIdentity::verify(saved.public_key.data(), message, sizeof(message), signature));
        assert(store.read(1, out).ok && out.private_key == saved.private_key);
    }
    mesh::LocalIdentity mt;
    mt.valid = true;
    std::fill_n(mt.private_key, 32, 0x37); // Legacy raw seed, intentionally not clamped.
    auto* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, mt.private_key, 32);
    size_t size = 32;
    assert(key && EVP_PKEY_get_raw_public_key(key, mt.public_key, &size) == 1 && size == 32);
    EVP_PKEY_free(key);
    native::mesh::LinuxSqliteLocalIdentityStore local;
    assert(local.save(mt).ok);
    assert(store.read(2, out).ok && out.present && out.private_size == 32 && out.generation == 0);
    assert(!std::memcmp(mt.public_key, out.public_key.data(), 32));
    assert(native::LinuxRadioIdentityStore::validatePair(2, out.public_key.data(), out.private_key.data(), 32, true));
    mesh::LocalIdentity unchanged;
    assert(local.load(unchanged).ok && !std::memcmp(unchanged.private_key, mt.private_key, 32));
    sql("CREATE TABLE linux_radio_identity_generation(kind INTEGER PRIMARY KEY,generation INTEGER NOT NULL)");
    sql("INSERT INTO linux_radio_identity_generation VALUES(2,9)");
    assert(store.read(2, out).ok && out.generation == 9);
    sql("UPDATE linux_radio_identity_generation SET generation='invalid' WHERE kind=2");
    assert(store.read(2, out).failure == Failure::Corrupt && !out.present && out.generation == 0);
    sql("UPDATE linux_radio_identity_generation SET generation=9 WHERE kind=2");
    {
        Radio radio;
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(radio, 123);
        chat::MeshConfig config;
        config.tx_enabled = true;
        sql("UPDATE settings SET payload=zeroblob(31) WHERE namespace='chat' AND key='pki_pub'");
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
        assert(!adapter.isPkiReady());
        uint8_t original[64];
        size_t length = 0;
        assert(platform::ui::settings_store::read_blob_into("chat", "pki_pub", original, sizeof(original), &length) == platform::ui::settings_store::BlobReadResult::Ok && length == 31);
        assert(writer.acquireShared(native::radio_identity_owner_path(path)) && !writer.acquireExclusive());
    }
    assert(writer.acquireExclusive());
    writer.reset();
    {
        auto mismatched = mt;
        mismatched.public_key[0] ^= 0x10;
        assert(local.save(mismatched).ok);
        Radio radio;
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(radio, 123);
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, chat::MeshConfig{});
        assert(!adapter.isPkiReady());
        assert(local.load(unchanged).ok && !std::memcmp(unchanged.public_key, mismatched.public_key, 32));
    }
    std::filesystem::remove_all(directory);
}
