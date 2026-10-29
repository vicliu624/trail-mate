#include "platform/linux/identity_owner_lease.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/mesh/linux_radio_identity_store.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/runtime_paths.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <openssl/evp.h>
#include <sqlite3.h>
#include <string>
#include <unistd.h>

namespace native = platform::linux_runtime;
using Result = chat::DeviceIdentityRestoreResult;
void sql(const std::filesystem::path& path, const char* text)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
mesh::LocalIdentity mtKey(uint8_t value)
{
    mesh::LocalIdentity identity;
    std::fill_n(identity.private_key, 32, value);
    identity.private_key[0] &= 0xf8;
    identity.private_key[31] = (identity.private_key[31] & 0x7f) | 0x40;
    auto* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, identity.private_key, 32);
    size_t size = 32;
    assert(key && EVP_PKEY_get_raw_public_key(key, identity.public_key, &size) == 1 && size == 32);
    EVP_PKEY_free(key);
    identity.valid = true;
    return identity;
}
void verify(const std::filesystem::path& path, uint8_t kind, const native::RadioIdentitySnapshot& expected, uint32_t generation)
{
    native::LinuxRadioIdentityStore store(path);
    native::RadioIdentitySnapshot actual;
    assert(store.read(kind, actual).ok && actual.present && actual.generation == generation);
    assert(actual.public_key == expected.public_key && actual.private_size == expected.private_size && actual.private_key == expected.private_key);
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string mode(argv[1]);
    char directory[] = "/tmp/trailmate-radio-replace-XXXXXX";
    assert(mkdtemp(directory));
    for (uint8_t kind : {1, 2})
    {
        const auto root = std::filesystem::path(directory) / std::to_string(kind);
        setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
        const auto path = native::sqlite_database_path();
        native::LinuxReticulumIdentity author(path);
        assert(author.begin());
        const auto author_public = author.publicKey();
        native::RadioIdentitySnapshot original, candidate;
        if (kind == 1)
        {
            {
                native::LinuxMeshCoreIdentity identity;
                assert(identity.begin());
            }
            const auto source_root = root / "source";
            setenv("TRAIL_MATE_SETTINGS_ROOT", source_root.c_str(), 1);
            {
                native::LinuxMeshCoreIdentity identity;
                assert(identity.begin());
            }
            assert(native::LinuxRadioIdentityStore(native::sqlite_database_path()).read(1, candidate).ok);
            setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
        }
        else
        {
            auto first = mtKey(0x31), second = mtKey(0x57);
            first.private_key[0] |= 1; // Preserve a legacy, semantically equivalent seed.
            native::mesh::LinuxSqliteLocalIdentityStore local;
            assert(local.save(first).ok);
            candidate.present = true;
            candidate.private_size = 32;
            std::copy_n(second.public_key, 32, candidate.public_key.data());
            std::copy_n(second.private_key, 32, candidate.private_key.data());
        }
        native::LinuxRadioIdentityStore store(path);
        assert(store.read(kind, original).ok && original.present && original.generation == 0);
        const auto apply = [&](uint32_t epoch = 0, const uint8_t* expected = nullptr)
        { return store.replace(kind, 1, author_public.data(), epoch, expected ? expected : original.public_key.data(), candidate.public_key.data(), candidate.private_key.data(), candidate.private_size); };
        if (mode == "stale")
        {
            assert(store.replace(kind, 2, author_public.data(), 0, original.public_key.data(), candidate.public_key.data(), candidate.private_key.data(), candidate.private_size) == Result::StaleIdentity);
            auto wrong = author_public;
            wrong[0] ^= 1;
            assert(store.replace(kind, 1, wrong.data(), 0, original.public_key.data(), candidate.public_key.data(), candidate.private_key.data(), candidate.private_size) == Result::StaleIdentity);
            assert(apply(1) == Result::StaleRadioIdentity);
            assert(store.replace(kind, 1, author_public.data(), 0, nullptr, candidate.public_key.data(), candidate.private_key.data(), candidate.private_size) == Result::StaleRadioIdentity);
            assert(store.replace(kind, 1, author_public.data(), UINT32_MAX, original.public_key.data(), candidate.public_key.data(), candidate.private_key.data(), candidate.private_size) == Result::InvalidInput);
            verify(path, kind, original, 0);
        }
        else if (mode == "busy")
        {
            native::IdentityOwnerLease reader;
            assert(reader.acquireShared(native::radio_identity_owner_path(path)));
            assert(apply() == Result::Unavailable);
            verify(path, kind, original, 0);
        }
        else if (mode == "same")
        {
            assert(store.replace(kind, 1, author_public.data(), 0, original.public_key.data(), original.public_key.data(), original.private_key.data(), original.private_size) == Result::CommittedRebootRequired);
            verify(path, kind, original, 1);
            if (kind == 2)
            {
                mesh::LocalIdentity unchanged;
                assert(native::mesh::LinuxSqliteLocalIdentityStore().load(unchanged).ok && (unchanged.private_key[0] & 1));
            }
        }
        else
        {
            if (mode == "rollback")
                sql(path, kind == 1 ? "CREATE TRIGGER fail_key BEFORE UPDATE ON linux_protocol_identity BEGIN SELECT RAISE(ABORT,'injected'); END;" : "CREATE TRIGGER fail_key BEFORE INSERT ON settings WHEN NEW.namespace='chat' AND NEW.key='pki_priv' BEGIN SELECT RAISE(ABORT,'injected'); END;");
            if (mode == "epoch")
            {
                sql(path, "CREATE TABLE linux_radio_identity_generation(kind INTEGER PRIMARY KEY,generation INTEGER NOT NULL);");
                sql(path, "CREATE TRIGGER fail_epoch BEFORE INSERT ON linux_radio_identity_generation BEGIN SELECT RAISE(ABORT,'injected'); END;");
            }
            if (mode == "rollback" || mode == "epoch")
            {
                assert(apply() == Result::StorageError);
                verify(path, kind, original, 0);
                sql(path, mode == "epoch" ? "DROP TRIGGER fail_epoch;" : "DROP TRIGGER fail_key;");
            }
            else assert(mode == "commit");
            assert(apply() == Result::CommittedRebootRequired);
            verify(path, kind, candidate, 1);
            assert(apply() == Result::StaleRadioIdentity);
            if (kind == 1)
            {
                native::LinuxMeshCoreIdentity loaded;
                const uint8_t message[]{4, 5, 6};
                uint8_t signature[64];
                assert(loaded.begin() && loaded.publicKey() == candidate.public_key && loaded.sign(message, sizeof(message), signature));
                assert(native::LinuxReticulumIdentity::verify(candidate.public_key.data(), message, sizeof(message), signature));
            }
            else
            {
                mesh::LocalIdentity loaded;
                assert(native::mesh::LinuxSqliteLocalIdentityStore().load(loaded).ok && !std::memcmp(loaded.public_key, candidate.public_key.data(), 32) && !std::memcmp(loaded.private_key, candidate.private_key.data(), 32));
            }
        }
        native::LinuxReticulumIdentity unchanged_author(path);
        assert(unchanged_author.begin() && unchanged_author.publicKey() == author_public && unchanged_author.generation() == 1);
    }
    std::filesystem::remove_all(directory);
}
