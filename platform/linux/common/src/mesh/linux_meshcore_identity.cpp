#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "platform/linux/runtime_paths.h"
#include <cstring>
#include <sqlite3.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/rand.h>
#endif

namespace platform::linux_runtime
{
namespace
{
struct Database
{
    sqlite3* value = nullptr;
    ~Database()
    {
        if (value) sqlite3_close(value);
    }
};
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement()
    {
        if (value) sqlite3_finalize(value);
    }
};
struct Seed
{
    uint8_t bytes[32]{};
    ~Seed()
    {
#if defined(TRAIL_MATE_HAS_OPENSSL)
        OPENSSL_cleanse(bytes, sizeof(bytes));
#endif
    }
};
} // namespace
LinuxMeshCoreIdentity::~LinuxMeshCoreIdentity()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    OPENSSL_cleanse(private_.data(), private_.size());
#endif
}
bool LinuxMeshCoreIdentity::begin()
{
    if (ready_) return true;
    error_.clear();
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    error_ = "MeshCore identity requires secure random support";
    return false;
#else
    const auto path = sqlite_database_path();
    if (!ensure_directory(path.parent_path()))
    {
        error_ = "Cannot create MeshCore identity directory";
        return false;
    }
#if defined(__linux__)
    if (!owner_.acquireShared(radio_identity_owner_path(path)))
    {
        error_ = "MeshCore identity is exclusively owned";
        return false;
    }
#endif
    Database database;
    if (sqlite3_open_v2(path.string().c_str(), &database.value,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        error_ = "Cannot open MeshCore identity storage";
        return false;
    }
    sqlite3_busy_timeout(database.value, 5000);
    if (sqlite3_exec(database.value,
                     "BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS linux_protocol_identity "
                     "(protocol TEXT PRIMARY KEY, seed BLOB NOT NULL, public_key BLOB NOT NULL);",
                     nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        error_ = "Cannot begin MeshCore identity transaction";
        return false;
    }
    Seed seed;
    bool found = false;
    {
        Statement query;
        if (sqlite3_prepare_v2(database.value,
                               "SELECT seed,public_key FROM linux_protocol_identity WHERE protocol='meshcore'",
                               -1, &query.value, nullptr) != SQLITE_OK)
            error_ = "Cannot query MeshCore identity";
        else
        {
            const auto result = sqlite3_step(query.value);
            if (result == SQLITE_ROW)
            {
                found = true;
                const auto private_size = sqlite3_column_bytes(query.value, 0);
                if (sqlite3_column_type(query.value, 0) != SQLITE_BLOB || (private_size != 32 && private_size != 64) ||
                    sqlite3_column_type(query.value, 1) != SQLITE_BLOB || sqlite3_column_bytes(query.value, 1) != 32)
                    error_ = "Stored MeshCore identity is corrupt";
                else
                {
                    std::memcpy(public_.data(), sqlite3_column_blob(query.value, 1), 32);
                    std::array<uint8_t, 32> derived{};
                    bool valid = false;
                    if (private_size == 32)
                    {
                        std::memcpy(seed.bytes, sqlite3_column_blob(query.value, 0), 32);
                        valid = chat::meshcore::meshcoreCreateKeypair(seed.bytes, derived.data(), private_.data());
                    }
                    else
                    {
                        std::memcpy(private_.data(), sqlite3_column_blob(query.value, 0), 64);
                        valid = !chat::meshcore::meshcoreIsZeroBytes(private_.data(), 32) &&
                                !chat::meshcore::meshcoreIsZeroBytes(private_.data() + 32, 32) &&
                                chat::meshcore::meshcoreDerivePublicKey(private_.data(), derived.data());
                    }
                    if (!valid || derived != public_)
                        error_ = "Stored MeshCore identity does not match its private key";
                }
            }
            else if (result != SQLITE_DONE) error_ = "Cannot read MeshCore identity";
        }
    }
    if (!found && error_.empty())
    {
        bool generated = false;
        for (unsigned attempt = 0; attempt < 64 && !generated; ++attempt)
        {
            if (RAND_bytes(seed.bytes, sizeof(seed.bytes)) != 1) break;
            generated = chat::meshcore::meshcoreCreateKeypair(seed.bytes, public_.data(), private_.data());
        }
        Statement insert;
        if (!generated || sqlite3_prepare_v2(database.value,
                                             "INSERT INTO linux_protocol_identity VALUES('meshcore',?,?)",
                                             -1, &insert.value, nullptr) != SQLITE_OK)
            error_ = "Cannot create MeshCore identity";
        else
        {
            sqlite3_bind_blob(insert.value, 1, seed.bytes, sizeof(seed.bytes), SQLITE_TRANSIENT);
            sqlite3_bind_blob(insert.value, 2, public_.data(), public_.size(), SQLITE_TRANSIENT);
            if (sqlite3_step(insert.value) != SQLITE_DONE) error_ = "Cannot persist MeshCore identity";
        }
    }
    if (error_.empty() && sqlite3_exec(database.value, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK)
    {
        ready_ = true;
        return true;
    }
    sqlite3_exec(database.value, "ROLLBACK", nullptr, nullptr, nullptr);
    OPENSSL_cleanse(private_.data(), private_.size());
    public_.fill(0);
    if (error_.empty()) error_ = "Cannot commit MeshCore identity";
    return false;
#endif
}
uint32_t LinuxMeshCoreIdentity::nodeId() const
{
    return ready_ ? chat::meshcore::deriveNodeIdFromPubkey(public_.data(), public_.size()) : 0;
}
bool LinuxMeshCoreIdentity::sign(const uint8_t* message, std::size_t size, uint8_t signature[64]) const
{
    return ready_ && chat::meshcore::meshcoreSign(private_.data(), public_.data(), message, size, signature);
}
bool LinuxMeshCoreIdentity::sharedSecret(const uint8_t peer[32], uint8_t secret[32]) const
{
    return ready_ && chat::meshcore::meshcoreDeriveSharedSecret(private_.data(), peer, secret);
}
} // namespace platform::linux_runtime
