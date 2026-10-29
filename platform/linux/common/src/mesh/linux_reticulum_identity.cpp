#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/linux/identity_owner_lease.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <sqlite3.h>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
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
    bool execute(const char* sql) { return sqlite3_exec(value, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
};
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement() { sqlite3_finalize(value); }
    bool prepare(sqlite3* db, const char* sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
};
struct Secret
{
    std::array<uint8_t, 96> bytes{};
    ~Secret()
    {
#if defined(TRAIL_MATE_HAS_OPENSSL)
        OPENSSL_cleanse(bytes.data(), bytes.size());
#endif
    }
};
#if defined(__linux__)
bool ownerLock(int fd, short type)
{
    return set_identity_owner_lock(fd, type == F_WRLCK);
}
#endif
#if defined(TRAIL_MATE_HAS_OPENSSL)
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
bool derivePublic(const Secret& secret, uint8_t format, std::array<uint8_t, 64>& out)
{
    Key encryption(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, secret.bytes.data(), 32), EVP_PKEY_free);
    size_t enc_size = 32;
    if (!encryption || EVP_PKEY_get_raw_public_key(encryption.get(), out.data(), &enc_size) != 1 || enc_size != 32) return false;
    if (format == 0)
    {
        uint8_t enc_nonzero = 0, signing_nonzero = 0;
        for (size_t i = 0; i < 32; ++i)
        {
            enc_nonzero |= secret.bytes[i];
            signing_nonzero |= secret.bytes[32 + i];
        }
        if (!enc_nonzero || !signing_nonzero || (secret.bytes[0] & 7) != 0 || (secret.bytes[31] & 0xc0) != 0x40) return false;
        ed25519_derive_pub(out.data() + 32, secret.bytes.data() + 32);
        return true;
    }
    if (format != 1) return false;
    Key signing(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, secret.bytes.data() + 32, 32), EVP_PKEY_free);
    size_t sig_size = 32;
    return signing && EVP_PKEY_get_raw_public_key(signing.get(), out.data() + 32, &sig_size) == 1 && sig_size == 32;
}
#endif
} // namespace
LinuxReticulumIdentity::LinuxReticulumIdentity(std::filesystem::path path) : database_(std::move(path)) {}
LinuxReticulumIdentity::~LinuxReticulumIdentity()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    OPENSSL_cleanse(private_.data(), private_.size());
#endif
#if defined(__linux__)
    if (owner_fd_ >= 0) ::close(owner_fd_);
#endif
}
bool LinuxReticulumIdentity::begin()
{
    if (retired_) return false;
    if (ready_) return true;
    error_.clear();
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    error_ = "Reticulum identity requires OpenSSL";
    return false;
#else
    const auto fail = [&](const char* error)
    { error_ = error; return false; };
    if (database_.empty() || database_ == ":memory:" || database_.string().rfind("file:", 0) == 0) return fail("Reticulum identity requires durable storage");
    std::error_code error;
    if (!database_.parent_path().empty()) std::filesystem::create_directories(database_.parent_path(), error);
    if (error) return fail("Cannot create Reticulum identity directory");
#if defined(__linux__)
    if (owner_fd_ < 0)
    {
        const auto owner_path = database_.string() + ".identity-owner";
        if (!open_shared_identity_owner(owner_path, owner_fd_)) return fail("Reticulum identity is retired or exclusively owned");
    }
    const int file = ::open(database_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat status
    {
    };
    if (file < 0) return fail("Cannot open Reticulum identity file");
    const bool regular = ::fstat(file, &status) == 0 && S_ISREG(status.st_mode) && status.st_uid == ::geteuid();
    const bool protected_file = regular && ::fchmod(file, 0600) == 0;
    ::close(file);
    if (!protected_file) return fail("Reticulum identity requires a private, user-owned regular file");
#endif
    Database db;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    if (sqlite3_open_v2(database_.string().c_str(), &db.value, flags, nullptr) != SQLITE_OK || sqlite3_busy_timeout(db.value, 250) != SQLITE_OK ||
        !db.execute("PRAGMA synchronous=FULL; BEGIN IMMEDIATE")) return fail("Cannot open Reticulum identity transaction");
    // Closing an uncommitted connection rolls back. No replacement identity is
    // generated when the existing row, schema or key relationship is damaged.
    bool fresh = false;
    {
        Statement table;
        if (!table.prepare(db.value, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='linux_reticulum_identity'") ||
            sqlite3_step(table.value) != SQLITE_ROW) return fail("Cannot inspect Reticulum identity storage");
        fresh = sqlite3_column_int(table.value, 0) == 0;
    }
    Secret secret;
    std::array<uint8_t, 64> public_key{};
    uint32_t generation = 0;
    uint8_t format = 1;
    if (fresh)
    {
        if (RAND_bytes(secret.bytes.data(), 64) != 1 || !derivePublic(secret, format, public_key)) return fail("Cannot generate Reticulum identity");
        if (!db.execute("CREATE TABLE linux_reticulum_identity(id INTEGER PRIMARY KEY CHECK(id=1),version INTEGER NOT NULL,generation INTEGER NOT NULL,private_key BLOB NOT NULL,public_key BLOB NOT NULL)")) return fail("Cannot initialize Reticulum identity storage");
        Statement insert;
        if (!insert.prepare(db.value, "INSERT INTO linux_reticulum_identity VALUES(1,1,1,?1,?2)") ||
            sqlite3_bind_blob(insert.value, 1, secret.bytes.data(), 64, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_blob(insert.value, 2, public_key.data(), public_key.size(), SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_step(insert.value) != SQLITE_DONE) return fail("Cannot persist Reticulum identity");
        generation = 1;
    }
    else
    {
        Statement row;
        if (!row.prepare(db.value, "SELECT id,version,generation,private_key,public_key FROM linux_reticulum_identity") ||
            sqlite3_step(row.value) != SQLITE_ROW) return fail("Stored Reticulum identity is missing or corrupt");
        for (int column = 0; column < 3; ++column)
            if (sqlite3_column_type(row.value, column) != SQLITE_INTEGER) return fail("Stored Reticulum identity metadata is corrupt");
        const auto epoch = sqlite3_column_int64(row.value, 2);
        const auto version = sqlite3_column_int64(row.value, 1);
        if (sqlite3_column_int64(row.value, 0) != 1 || (version != 1 && version != 2) || epoch <= 0 || epoch > UINT32_MAX)
            return fail("Stored Reticulum identity version or generation is unsupported");
        format = version == 1 ? 1 : 0;
        const int private_size = format == 1 ? 64 : 96;
        for (int column = 3; column < 5; ++column)
            if (sqlite3_column_type(row.value, column) != SQLITE_BLOB || sqlite3_column_bytes(row.value, column) != (column == 3 ? private_size : 64))
                return fail("Stored Reticulum identity key shape is corrupt");
        std::memcpy(secret.bytes.data(), sqlite3_column_blob(row.value, 3), private_size);
        if (!derivePublic(secret, format, public_key) || CRYPTO_memcmp(public_key.data(), sqlite3_column_blob(row.value, 4), public_key.size()) != 0)
            return fail("Stored Reticulum identity public/private keys do not match");
        generation = static_cast<uint32_t>(epoch);
        if (sqlite3_step(row.value) != SQLITE_DONE) return fail("Stored Reticulum identity has extra rows");
    }
    if (!db.execute("COMMIT")) return fail("Cannot commit Reticulum identity");
    private_ = secret.bytes;
    public_ = public_key;
    generation_ = generation;
    format_ = format;
    chat::reticulum::computeIdentityHash(public_.data(), identity_.data());
    uint8_t name[chat::reticulum::kNameHashSize];
    chat::reticulum::computeNameHash("lxmf", "delivery", name);
    chat::reticulum::computeDestinationHash(name, identity_.data(), destination_.data());
    ready_ = true;
    return true;
#endif
}
bool LinuxReticulumIdentity::overlaps(const void* output, size_t size) const
{
    const auto out = reinterpret_cast<uintptr_t>(output), self = reinterpret_cast<uintptr_t>(this);
    return out <= self ? self - out < size : out - self < sizeof(*this);
}
uint32_t LinuxReticulumIdentity::nodeId() const { return ready_ ? chat::reticulum::nodeIdFromDestinationHash(destination_.data()) : 0; }
bool LinuxReticulumIdentity::sign(const uint8_t* data, size_t size, uint8_t signature[64]) const
{
    if (!signature || overlaps(signature, 64)) return false;
    std::array<uint8_t, 64> result{};
    bool ok = false;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (ready_ && (data || !size))
    {
        if (format_ == 0)
        {
            ed25519_sign(result.data(), data, size, public_.data() + 32, private_.data() + 32);
            ok = true;
        }
        else
        {
            Key key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, private_.data() + 32, 32), EVP_PKEY_free);
            Context ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
            size_t written = result.size();
            ok = key && ctx && EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
                 EVP_DigestSign(ctx.get(), result.data(), &written, data, size) == 1 && written == result.size();
        }
    }
#else
    (void)data;
    (void)size;
#endif
    if (!ok) result.fill(0);
    std::memcpy(signature, result.data(), result.size());
    return ok;
}
bool LinuxReticulumIdentity::verify(const uint8_t public_key[32], const uint8_t* data, size_t size, const uint8_t signature[64])
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!public_key || !signature || (!data && size)) return false;
    Key key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, public_key, 32), EVP_PKEY_free);
    Context ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    return key && ctx && EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
           EVP_DigestVerify(ctx.get(), signature, 64, data, size) == 1;
#else
    (void)public_key;
    (void)data;
    (void)size;
    (void)signature;
    return false;
#endif
}
bool LinuxReticulumIdentity::sharedSecret(const uint8_t peer[32], uint8_t secret[32]) const
{
    if (!secret || overlaps(secret, 32)) return false;
    std::array<uint8_t, 32> result{};
    bool ok = false;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (ready_ && peer)
    {
        Key local(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, private_.data(), 32), EVP_PKEY_free);
        Key remote(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer, 32), EVP_PKEY_free);
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(local ? EVP_PKEY_CTX_new(local.get(), nullptr) : nullptr, EVP_PKEY_CTX_free);
        size_t written = result.size();
        const std::array<uint8_t, 32> zero{};
        ok = local && remote && ctx && EVP_PKEY_derive_init(ctx.get()) == 1 && EVP_PKEY_derive_set_peer(ctx.get(), remote.get()) == 1 &&
             EVP_PKEY_derive(ctx.get(), result.data(), &written) == 1 && written == result.size() &&
             CRYPTO_memcmp(result.data(), zero.data(), result.size()) != 0;
    }
#else
    (void)peer;
#endif
    if (!ok) result.fill(0);
    std::memcpy(secret, result.data(), result.size());
#if defined(TRAIL_MATE_HAS_OPENSSL)
    OPENSSL_cleanse(result.data(), result.size());
#endif
    return ok;
}
bool LinuxReticulumIdentity::copyPrivateKey(uint32_t expected, uint8_t* output, size_t capacity, size_t& written) const
{
    written = 0;
    const size_t size = format_ == 1 ? 64 : 96;
    if (!output || overlaps(output, std::min(capacity, size))) return false;
    std::fill_n(output, std::min(capacity, size), 0);
    if (!ready_ || !expected || generation_ != expected || capacity < size) return false;
    std::copy_n(private_.data(), size, output);
    written = size;
    return true;
}
bool LinuxReticulumIdentity::validateKeyPair(uint8_t format, const uint8_t* bytes, size_t size, const uint8_t* public_key)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!bytes || !public_key || format > 1 || size != (format == 1 ? 64U : 96U)) return false;
    Secret secret;
    std::copy_n(bytes, size, secret.bytes.data());
    std::array<uint8_t, 64> derived{};
    return derivePublic(secret, format, derived) && CRYPTO_memcmp(derived.data(), public_key, derived.size()) == 0;
#else
    (void)format;
    (void)bytes;
    (void)size;
    (void)public_key;
    return false;
#endif
}
chat::DeviceIdentityRestoreResult LinuxReticulumIdentity::replaceKeyPair(uint32_t expected, const uint8_t* expected_public,
                                                                         uint8_t format, const uint8_t* bytes, size_t size, const uint8_t* public_key)
{
    using Result = chat::DeviceIdentityRestoreResult;
    if (!ready_) return Result::Unavailable;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!expected_public || !expected || expected == UINT32_MAX || !validateKeyPair(format, bytes, size, public_key)) return Result::InvalidInput;
    if (generation_ != expected || CRYPTO_memcmp(expected_public, public_.data(), public_.size())) return Result::StaleIdentity;
#if defined(__linux__)
    // OFD upgrade fails atomically, retaining the read lease. Other processes
    // or cached identity objects must be quiesced before any storage mutation.
    if (owner_fd_ < 0 || !ownerLock(owner_fd_, F_WRLCK)) return Result::Unavailable;
    struct ReadLease
    {
        int fd;
        bool retired = false;
        ~ReadLease()
        {
            if (!retired) (void)ownerLock(fd, F_RDLCK);
        }
    } read_lease{owner_fd_};
#endif
    Secret candidate;
    std::copy_n(bytes, size, candidate.bytes.data());
    Database db;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    if (sqlite3_open_v2(database_.string().c_str(), &db.value, flags, nullptr) != SQLITE_OK || sqlite3_busy_timeout(db.value, 250) != SQLITE_OK ||
        !db.execute("PRAGMA synchronous=FULL; BEGIN IMMEDIATE")) return Result::StorageError;
    {
        Statement row;
        if (!row.prepare(db.value, "SELECT id,version,generation,private_key,public_key FROM linux_reticulum_identity") || sqlite3_step(row.value) != SQLITE_ROW) return Result::StorageError;
        for (int column = 0; column < 3; ++column)
            if (sqlite3_column_type(row.value, column) != SQLITE_INTEGER) return Result::StorageError;
        const auto version = sqlite3_column_int64(row.value, 1);
        const auto epoch = sqlite3_column_int64(row.value, 2);
        if (sqlite3_column_int64(row.value, 0) != 1 || (version != 1 && version != 2) || epoch <= 0 || epoch > UINT32_MAX ||
            sqlite3_column_type(row.value, 3) != SQLITE_BLOB || sqlite3_column_bytes(row.value, 3) != (version == 1 ? 64 : 96) ||
            sqlite3_column_type(row.value, 4) != SQLITE_BLOB || sqlite3_column_bytes(row.value, 4) != 64 ||
            !validateKeyPair(version == 1 ? 1 : 0, static_cast<const uint8_t*>(sqlite3_column_blob(row.value, 3)), sqlite3_column_bytes(row.value, 3),
                             static_cast<const uint8_t*>(sqlite3_column_blob(row.value, 4)))) return Result::StorageError;
        if (epoch != expected || CRYPTO_memcmp(sqlite3_column_blob(row.value, 4), expected_public, 64)) return Result::StaleIdentity;
        if (sqlite3_step(row.value) != SQLITE_DONE) return Result::StorageError;
    }
    Statement update;
    if (!update.prepare(db.value, "UPDATE linux_reticulum_identity SET version=?1,generation=?2,private_key=?3,public_key=?4 WHERE id=1 AND generation=?5 AND public_key=?6") ||
        sqlite3_bind_int(update.value, 1, format == 1 ? 1 : 2) != SQLITE_OK || sqlite3_bind_int64(update.value, 2, int64_t(expected) + 1) != SQLITE_OK ||
        sqlite3_bind_blob(update.value, 3, candidate.bytes.data(), static_cast<int>(size), SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_blob(update.value, 4, public_key, 64, SQLITE_STATIC) != SQLITE_OK || sqlite3_bind_int64(update.value, 5, expected) != SQLITE_OK ||
        sqlite3_bind_blob(update.value, 6, expected_public, 64, SQLITE_STATIC) != SQLITE_OK || sqlite3_step(update.value) != SQLITE_DONE) return Result::StorageError;
    if (sqlite3_changes(db.value) != 1) return Result::StaleIdentity;
    const bool committed = db.execute("COMMIT");
    if (!committed && !sqlite3_get_autocommit(db.value) && db.execute("ROLLBACK")) return Result::StorageError;
    ready_ = false;
    retired_ = true;
#if defined(__linux__)
    read_lease.retired = true;
#endif
    OPENSSL_cleanse(private_.data(), private_.size());
    error_ = "Identity replacement requires process restart";
    return committed ? Result::CommittedRebootRequired : Result::IndeterminateRebootRequired;
#else
    (void)expected;
    (void)expected_public;
    (void)format;
    (void)bytes;
    (void)size;
    (void)public_key;
    return Result::Unsupported;
#endif
}
} // namespace platform::linux_runtime
