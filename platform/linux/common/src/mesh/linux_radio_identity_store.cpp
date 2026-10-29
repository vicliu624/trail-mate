#include "platform/linux/mesh/linux_radio_identity_store.h"
#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "platform/linux/identity_owner_lease.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <algorithm>
#include <cstring>
#include <sqlite3.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
namespace
{
using Failure = ::mesh::StoreFailure;
struct Database
{
    sqlite3* value = nullptr;
    ~Database()
    {
        if (value) sqlite3_close(value);
    }
    bool execute(const char* sql) { return sqlite3_exec(value, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
};
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement() { sqlite3_finalize(value); }
    bool prepare(sqlite3* db, const char* sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
};
Failure table(sqlite3* db, const char* name, bool& present)
{
    present = false;
    Statement query;
    if (!query.prepare(db, "SELECT type FROM sqlite_master WHERE name=?1") || sqlite3_bind_text(query.value, 1, name, -1, SQLITE_STATIC) != SQLITE_OK) return Failure::IoError;
    const auto result = sqlite3_step(query.value);
    if (result == SQLITE_DONE) return Failure::None;
    if (result != SQLITE_ROW) return Failure::IoError;
    const auto* type = sqlite3_column_text(query.value, 0);
    if (sqlite3_column_type(query.value, 0) != SQLITE_TEXT || !type || std::strcmp(reinterpret_cast<const char*>(type), "table")) return Failure::Corrupt;
    present = true;
    return sqlite3_step(query.value) == SQLITE_DONE ? Failure::None : Failure::Corrupt;
}
Failure generation(sqlite3* db, uint8_t kind, uint32_t& value)
{
    bool present = false;
    const auto state = table(db, "linux_radio_identity_generation", present);
    if (state != Failure::None || !present) return state;
    Statement query;
    if (!query.prepare(db, "SELECT kind,generation FROM linux_radio_identity_generation WHERE kind=?1") || sqlite3_bind_int(query.value, 1, kind) != SQLITE_OK) return Failure::IoError;
    const auto result = sqlite3_step(query.value);
    if (result == SQLITE_DONE) return Failure::None;
    if (result != SQLITE_ROW) return Failure::IoError;
    if (sqlite3_column_type(query.value, 0) != SQLITE_INTEGER || sqlite3_column_int64(query.value, 0) != kind ||
        sqlite3_column_type(query.value, 1) != SQLITE_INTEGER || sqlite3_column_int64(query.value, 1) < 0 || sqlite3_column_int64(query.value, 1) > UINT32_MAX) return Failure::Corrupt;
    value = uint32_t(sqlite3_column_int64(query.value, 1));
    return sqlite3_step(query.value) == SQLITE_DONE ? Failure::None : Failure::Corrupt;
}
Failure meshcore(sqlite3* db, RadioIdentitySnapshot& out)
{
    bool present = false;
    const auto state = table(db, "linux_protocol_identity", present);
    if (state != Failure::None || !present) return state;
    Statement query;
    if (!query.prepare(db, "SELECT seed,public_key FROM linux_protocol_identity WHERE protocol='meshcore'")) return Failure::IoError;
    const auto result = sqlite3_step(query.value);
    if (result == SQLITE_DONE) return Failure::None;
    if (result != SQLITE_ROW) return Failure::IoError;
    const auto size = sqlite3_column_bytes(query.value, 0);
    if (sqlite3_column_type(query.value, 0) != SQLITE_BLOB || (size != 32 && size != 64) ||
        sqlite3_column_type(query.value, 1) != SQLITE_BLOB || sqlite3_column_bytes(query.value, 1) != 32) return Failure::Corrupt;
    std::copy_n(static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 1)), 32, out.public_key.data());
    if (size == 32)
    {
        std::array<uint8_t, 32> derived{};
        if (!chat::meshcore::meshcoreCreateKeypair(static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 0)), derived.data(), out.private_key.data()) || derived != out.public_key) return Failure::Corrupt;
    }
    else std::copy_n(static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 0)), 64, out.private_key.data());
    out.private_size = 64;
    if (!LinuxRadioIdentityStore::validatePair(1, out.public_key.data(), out.private_key.data(), out.private_size) || sqlite3_step(query.value) != SQLITE_DONE) return Failure::Corrupt;
    out.present = true;
    return Failure::None;
}
Failure blob(sqlite3* db, const char* ns, const char* key, uint8_t* output, bool& present)
{
    present = false;
    Statement query;
    if (!query.prepare(db, "SELECT kind,payload FROM settings WHERE namespace=?1 AND key=?2") ||
        sqlite3_bind_text(query.value, 1, ns, -1, SQLITE_STATIC) != SQLITE_OK || sqlite3_bind_text(query.value, 2, key, -1, SQLITE_STATIC) != SQLITE_OK) return Failure::IoError;
    const auto result = sqlite3_step(query.value);
    if (result == SQLITE_DONE) return Failure::None;
    if (result != SQLITE_ROW) return Failure::IoError;
    const auto* type = sqlite3_column_text(query.value, 0);
    if (sqlite3_column_type(query.value, 0) != SQLITE_TEXT || !type || sqlite3_column_bytes(query.value, 0) != 1 || *type != 'x' ||
        sqlite3_column_type(query.value, 1) != SQLITE_BLOB || sqlite3_column_bytes(query.value, 1) != 32) return Failure::Corrupt;
    std::copy_n(static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 1)), 32, output);
    present = true;
    return sqlite3_step(query.value) == SQLITE_DONE ? Failure::None : Failure::Corrupt;
}
Failure meshtastic(sqlite3* db, const mesh::LinuxSqliteLocalIdentityStore::Options& options, RadioIdentitySnapshot& out)
{
    bool present = false;
    const auto state = table(db, "settings", present);
    if (state != Failure::None || !present) return state;
    bool public_present = false, private_present = false;
    auto result = blob(db, options.ns, options.public_key, out.public_key.data(), public_present);
    if (result != Failure::None) return result;
    result = blob(db, options.ns, options.private_key, out.private_key.data(), private_present);
    if (result != Failure::None) return result;
    if (!public_present && !private_present) return Failure::None;
    if (!public_present || !private_present || !LinuxRadioIdentityStore::validatePair(2, out.public_key.data(), out.private_key.data(), 32)) return Failure::Corrupt;
    // Wire backups use the canonical scalar; the legacy seed stays unchanged.
    out.private_key[0] &= 0xf8;
    out.private_key[31] = (out.private_key[31] & 0x7f) | 0x40;
    out.private_size = 32;
    out.present = true;
    return Failure::None;
}
} // namespace
void RadioIdentitySnapshot::clear()
{
    volatile uint8_t* bytes = private_key.data();
    for (size_t i = 0; i < private_key.size(); ++i) bytes[i] = 0;
    public_key.fill(0);
    present = false;
    generation = 0;
    private_size = 0;
}
bool LinuxRadioIdentityStore::validatePair(uint8_t kind, const uint8_t* pub, const uint8_t* priv, size_t size, bool canonical)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!pub || !priv || (kind != 1 && kind != 2) || size != (kind == 1 ? 64U : 32U) ||
        chat::meshcore::meshcoreIsZeroBytes(pub, 32) || chat::meshcore::meshcoreIsZeroBytes(priv, size)) return false;
    std::array<uint8_t, 32> derived{};
    if (kind == 1)
        return size == 64 && !chat::meshcore::meshcoreIsZeroBytes(priv, 32) && !chat::meshcore::meshcoreIsZeroBytes(priv + 32, 32) &&
               chat::meshcore::meshcoreDerivePublicKey(priv, derived.data()) && CRYPTO_memcmp(pub, derived.data(), 32) == 0;
    if (kind != 2 || size != 32 || (canonical && ((priv[0] & 7) != 0 || (priv[31] & 0xc0) != 0x40))) return false;
    auto* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv, 32);
    size_t length = derived.size();
    const bool valid = key && EVP_PKEY_get_raw_public_key(key, derived.data(), &length) == 1 && length == derived.size() && CRYPTO_memcmp(pub, derived.data(), 32) == 0;
    EVP_PKEY_free(key);
    return valid;
#else
    (void)kind;
    (void)pub;
    (void)priv;
    (void)size;
    (void)canonical;
    return false;
#endif
}
::mesh::StoreResult LinuxRadioIdentityStore::read(uint8_t kind, RadioIdentitySnapshot& out) const
{
    out.clear();
    const auto fail = [&](Failure reason)
    { out.clear(); return ::mesh::StoreResult::fail(reason); };
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)kind;
    return fail(Failure::IoError);
#else
    if ((kind != 1 && kind != 2) || database_.empty() || database_ == ":memory:" || database_.string().rfind("file:", 0) == 0) return fail(Failure::InvalidArgument);
    if (kind == 2 && (!primary_.ns || !*primary_.ns || !primary_.public_key || !*primary_.public_key || !primary_.private_key || !*primary_.private_key || !std::strcmp(primary_.public_key, primary_.private_key))) return fail(Failure::InvalidArgument);
    IdentityOwnerLease owner;
    if (!owner.acquireShared(radio_identity_owner_path(database_))) return fail(Failure::IoError);
    Database db;
    int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    if (sqlite3_open_v2(database_.string().c_str(), &db.value, flags, nullptr) != SQLITE_OK)
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(database_, error);
        return !exists && !error ? ::mesh::StoreResult::success() : fail(Failure::IoError);
    }
    if (sqlite3_busy_timeout(db.value, 250) != SQLITE_OK || sqlite3_exec(db.value, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return fail(Failure::IoError);
    const auto epoch = generation(db.value, kind, out.generation);
    if (epoch != Failure::None) return fail(epoch);
    const auto loaded = kind == 1 ? meshcore(db.value, out) : meshtastic(db.value, primary_, out);
    if (loaded != Failure::None) return fail(loaded);
    if (sqlite3_exec(db.value, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return fail(Failure::IoError);
    return ::mesh::StoreResult::success();
#endif
}
chat::DeviceIdentityRestoreResult LinuxRadioIdentityStore::replace(uint8_t kind, uint32_t author_generation, const uint8_t* author_public,
                                                                   uint32_t radio_generation, const uint8_t* expected_radio_public, const uint8_t* public_key, const uint8_t* private_key, size_t private_size) const
{
    using Result = chat::DeviceIdentityRestoreResult;
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)kind;
    (void)author_generation;
    (void)author_public;
    (void)radio_generation;
    (void)expected_radio_public;
    (void)public_key;
    (void)private_key;
    (void)private_size;
    return Result::Unsupported;
#else
    if (!author_generation || !author_public || radio_generation == UINT32_MAX || !validatePair(kind, public_key, private_key, private_size, true)) return Result::InvalidInput;
    if (kind == 2 && (!primary_.ns || !*primary_.ns || !primary_.public_key || !*primary_.public_key || !primary_.private_key || !*primary_.private_key || !std::strcmp(primary_.public_key, primary_.private_key))) return Result::InvalidInput;
    LinuxReticulumIdentity author(database_);
    if (!author.begin()) return Result::StorageError;
    if (author.generation() != author_generation || CRYPTO_memcmp(author.publicKey().data(), author_public, 64)) return Result::StaleIdentity;
    IdentityOwnerLease owner;
    if (!owner.acquireShared(radio_identity_owner_path(database_)) || !owner.acquireExclusive()) return Result::Unavailable;
    Database db;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    if (sqlite3_open_v2(database_.string().c_str(), &db.value, flags, nullptr) != SQLITE_OK || sqlite3_busy_timeout(db.value, 250) != SQLITE_OK ||
        !db.execute("PRAGMA synchronous=FULL; BEGIN IMMEDIATE")) return Result::StorageError;
    {
        Statement query;
        if (!query.prepare(db.value, "SELECT generation,public_key FROM linux_reticulum_identity WHERE id=1") || sqlite3_step(query.value) != SQLITE_ROW) return Result::StorageError;
        if (sqlite3_column_type(query.value, 0) != SQLITE_INTEGER || sqlite3_column_int64(query.value, 0) != author_generation ||
            sqlite3_column_type(query.value, 1) != SQLITE_BLOB || sqlite3_column_bytes(query.value, 1) != 64 ||
            CRYPTO_memcmp(sqlite3_column_blob(query.value, 1), author_public, 64)) return Result::StaleIdentity;
        if (sqlite3_step(query.value) != SQLITE_DONE) return Result::StorageError;
    }
    RadioIdentitySnapshot current;
    if (generation(db.value, kind, current.generation) != Failure::None ||
        (kind == 1 ? meshcore(db.value, current) : meshtastic(db.value, primary_, current)) != Failure::None) return Result::StorageError;
    if (current.generation != radio_generation || current.present != bool(expected_radio_public) ||
        (current.present && CRYPTO_memcmp(current.public_key.data(), expected_radio_public, 32))) return Result::StaleRadioIdentity;
    const bool same = current.present && current.private_size == private_size &&
                      !CRYPTO_memcmp(current.public_key.data(), public_key, 32) && !CRYPTO_memcmp(current.private_key.data(), private_key, private_size);
    if (!same && kind == 1)
    {
        if (!db.execute("CREATE TABLE IF NOT EXISTS linux_protocol_identity(protocol TEXT PRIMARY KEY,seed BLOB NOT NULL,public_key BLOB NOT NULL)")) return Result::StorageError;
        Statement update;
        if (!update.prepare(db.value, "INSERT INTO linux_protocol_identity(protocol,seed,public_key) VALUES('meshcore',?1,?2) ON CONFLICT(protocol) DO UPDATE SET seed=excluded.seed,public_key=excluded.public_key") ||
            sqlite3_bind_blob(update.value, 1, private_key, 64, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_blob(update.value, 2, public_key, 32, SQLITE_TRANSIENT) != SQLITE_OK || sqlite3_step(update.value) != SQLITE_DONE) return Result::StorageError;
    }
    if (!same && kind == 2)
    {
        if (!db.execute("CREATE TABLE IF NOT EXISTS settings(namespace TEXT NOT NULL,key TEXT NOT NULL,kind TEXT NOT NULL,payload BLOB NOT NULL,updated_at INTEGER NOT NULL DEFAULT(CAST(strftime('%s','now') AS INTEGER)),PRIMARY KEY(namespace,key))")) return Result::StorageError;
        for (unsigned field = 0; field < 2; ++field)
        {
            Statement update;
            if (!update.prepare(db.value, "INSERT INTO settings(namespace,key,kind,payload) VALUES(?1,?2,'x',?3) ON CONFLICT(namespace,key) DO UPDATE SET kind='x',payload=excluded.payload,updated_at=CAST(strftime('%s','now') AS INTEGER)") ||
                sqlite3_bind_text(update.value, 1, primary_.ns, -1, SQLITE_STATIC) != SQLITE_OK ||
                sqlite3_bind_text(update.value, 2, field ? primary_.private_key : primary_.public_key, -1, SQLITE_STATIC) != SQLITE_OK ||
                sqlite3_bind_blob(update.value, 3, field ? private_key : public_key, 32, SQLITE_TRANSIENT) != SQLITE_OK || sqlite3_step(update.value) != SQLITE_DONE) return Result::StorageError;
        }
    }
    if (!db.execute("CREATE TABLE IF NOT EXISTS linux_radio_identity_generation(kind INTEGER PRIMARY KEY CHECK(kind IN(1,2)),generation INTEGER NOT NULL CHECK(generation>=0 AND generation<=4294967295))")) return Result::StorageError;
    Statement epoch;
    if (!epoch.prepare(db.value, "INSERT INTO linux_radio_identity_generation(kind,generation) VALUES(?1,?2) ON CONFLICT(kind) DO UPDATE SET generation=excluded.generation") ||
        sqlite3_bind_int(epoch.value, 1, kind) != SQLITE_OK || sqlite3_bind_int64(epoch.value, 2, int64_t(radio_generation) + 1) != SQLITE_OK || sqlite3_step(epoch.value) != SQLITE_DONE) return Result::StorageError;
    if (db.execute("COMMIT")) return Result::CommittedRebootRequired;
    if (!sqlite3_get_autocommit(db.value) && db.execute("ROLLBACK")) return Result::StorageError;
    return Result::IndeterminateRebootRequired;
#endif
}
} // namespace platform::linux_runtime
