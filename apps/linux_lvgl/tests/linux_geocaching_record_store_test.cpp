#include "geocaching/protocol/record_encoder.h"
#include "geocaching/protocol/sign_record.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/geocaching_record_store.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <sqlite3.h>
#include <unistd.h>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

using Store = platform::linux_runtime::LinuxGeocachingRecordStore;
using Result = Store::Result;
namespace gc = geocaching;
#if defined(TRAIL_MATE_HAS_OPENSSL)
struct Signed
{
    gc::GeocacheId id;
    gc::RevisionHash hash;
    std::vector<uint8_t> bytes;
};
struct Fixture
{
    std::vector<uint8_t> original;
    gc::RecordView base;
    std::array<uint8_t, 64> public_key{};
    EVP_PKEY* private_key = nullptr;
    platform::linux_runtime::LinuxGeocachingCrypto crypto;
    explicit Fixture(const char* filename)
    {
        std::ifstream file(filename, std::ios::binary);
        assert(file.good());
        original.assign(std::istreambuf_iterator<char>(file), {});
        std::array<uint8_t, 4200> scratch;
        gc::protocol::VerifiedRecordView verified;
        assert(gc::protocol::verifyGeocache({original.data(), original.size()}, crypto, scratch.data(), scratch.size(), verified) == gc::protocol::VerificationResult::Valid);
        base = verified.record;
        std::copy_n(base.author_public_key.data, 64, public_key.data());
        // Public interoperability fixture seed, never a device identity.
        std::array<uint8_t, 32> seed{};
        for (size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<uint8_t>(i);
        private_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        assert(private_key);
        std::array<uint8_t, 32> derived;
        size_t size = derived.size();
        assert(EVP_PKEY_get_raw_public_key(private_key, derived.data(), &size) == 1 && size == 32);
        assert(std::equal(derived.begin(), derived.end(), public_key.begin() + 32));
    }
    ~Fixture() { EVP_PKEY_free(private_key); }
    bool isReady() const { return private_key != nullptr; }
    void combinedPublicKey(uint8_t* output) const { std::copy(public_key.begin(), public_key.end(), output); }
    bool sign(const uint8_t* message, size_t size, uint8_t* signature)
    {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (!context) return false;
        size_t written = 64;
        const bool ok = EVP_DigestSignInit(context, nullptr, nullptr, nullptr, private_key) == 1 &&
                        EVP_DigestSign(context, signature, &written, message, size) == 1 && written == 64;
        EVP_MD_CTX_free(context);
        return ok;
    }
    Signed make(uint32_t revision, const gc::RevisionHash* previous = nullptr,
                gc::CacheState state = gc::CacheState::Active, const char* name = nullptr, bool changed_creation = false)
    {
        auto record = base;
        record.revision = revision;
        record.previous_hash = previous ? gc::ByteView{previous->bytes.data(), 32} : gc::ByteView{};
        record.state = state;
        if (name) record.name = name;
        if (revision > 1) record.updated_at = base.updated_at + revision;
        if (changed_creation) ++record.created_at;
        std::vector<uint8_t> encoded(4096), scratch(4200);
        size_t size = 0;
        assert(gc::protocol::encodeGeocacheRecord(record, encoded.data(), encoded.size(), size));
        Signed result;
        result.bytes.resize(4166);
        size_t written = 0;
        assert(gc::protocol::signGeocacheRecord({encoded.data(), size}, *this, scratch.data(), scratch.size(), result.bytes.data(), result.bytes.size(), written));
        result.bytes.resize(written);
        gc::protocol::VerifiedRecordView verified;
        assert(gc::protocol::verifyGeocache({result.bytes.data(), result.bytes.size()}, crypto, scratch.data(), scratch.size(), verified) == gc::protocol::VerificationResult::Valid);
        result.id = verified.id;
        result.hash = verified.hash;
        return result;
    }
};
Result accept(Store& store, const Signed& record) { return store.accept(record.id, record.hash, {record.bytes.data(), record.bytes.size()}); }
void sql(const std::filesystem::path& path, const char* command)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, command, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
#endif
int main(int argc, char** argv)
{
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    (void)argc;
    (void)argv;
    return 77;
#else
    assert(argc == 2);
    Fixture fixture(argv[1]);
    const auto one = fixture.make(1);
    assert(one.bytes == fixture.original); // Independent production sample agrees byte for byte.
    const auto two = fixture.make(2, &one.hash);
    const auto three = fixture.make(3, &two.hash);
    const auto fork = fixture.make(2, &one.hash, gc::CacheState::Active, "Forked author revision");
    const auto archive = fixture.make(2, &one.hash, gc::CacheState::Archived);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-record-history-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto path = root / "history.sqlite3";
    Store::Record out;
    {
        Store store(path, fixture.crypto);
        assert(store.open() == Result::Ready);
        assert(accept(store, one) == Result::Ready);
        assert(store.head(one.id, out) == Result::Ready && out.summary.revision == 1 && !out.history_incomplete);
        assert(accept(store, one) == Result::Identical);
        assert(accept(store, three) == Result::Ready);
        assert(store.head(one.id, out) == Result::Ready && out.summary.revision == 3 && out.history_incomplete);
        assert(accept(store, one) == Result::Older);
        assert(accept(store, two) == Result::Older);
        assert(store.head(one.id, out) == Result::Ready && out.summary.revision == 3 && !out.history_incomplete);
        auto invalid = three;
        invalid.bytes.back() ^= 1;
        assert(accept(store, invalid) == Result::Invalid);
        invalid = three;
        invalid.hash.bytes[0] ^= 1;
        assert(accept(store, invalid) == Result::Invalid);
    }
    {
        Store store(path, fixture.crypto);
        assert(store.open() == Result::Ready);
        assert(store.head(one.id, out) == Result::Ready && out.bytes == three.bytes);
        assert(accept(store, fork) == Result::Conflict);
        assert(store.head(one.id, out) == Result::Conflict && out.bytes.empty());
        assert(store.read(two.id, two.hash, out) == Result::Ready && out.bytes == two.bytes);
        assert(store.read(fork.id, fork.hash, out) == Result::Ready && out.bytes == fork.bytes);
    }
    {
        Store store(path, fixture.crypto);
        assert(store.open() == Result::Ready && store.head(one.id, out) == Result::Conflict);
        const auto later = fixture.make(5, &three.hash);
        assert(accept(store, later) == Result::Conflict);
        assert(store.head(one.id, out) == Result::Conflict); // A gap/newer head cannot erase fork evidence.
    }
    {
        Store store(root / "archive.sqlite3", fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready && accept(store, three) == Result::Ready);
        assert(accept(store, archive) == Result::Conflict); // Late archive conflicts with an already seen later revision.
        assert(store.head(one.id, out) == Result::Conflict);
    }
    {
        Store store(root / "creation.sqlite3", fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready);
        assert(accept(store, fixture.make(5, &three.hash, gc::CacheState::Active, nullptr, true)) == Result::Conflict);
    }
    {
        Store store(root / "link.sqlite3", fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready);
        assert(accept(store, fixture.make(2, &three.hash)) == Result::Conflict);
        assert(store.head(one.id, out) == Result::Conflict);
    }
    {
        const auto missing_history = root / "missing-history.sqlite3";
        Store store(missing_history, fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready && accept(store, two) == Result::Ready);
        sql(missing_history, "DELETE FROM linux_geocaching_records WHERE revision=1");
        assert(store.head(one.id, out) == Result::Corrupt);
        assert(accept(store, three) == Result::Corrupt); // Do not silently downgrade a complete chain into a gap.
    }
    {
        const auto rollback = root / "rollback.sqlite3";
        Store store(rollback, fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready);
        sql(rollback, "CREATE TRIGGER fail_head BEFORE UPDATE ON linux_geocaching_heads BEGIN SELECT RAISE(ABORT,'injected failure'); END");
        assert(accept(store, two) == Result::Error);
        assert(store.read(two.id, two.hash, out) == Result::Missing);
        assert(store.head(one.id, out) == Result::Ready && out.summary.revision == 1);
        sql(rollback, "DROP TRIGGER fail_head");
        assert(accept(store, two) == Result::Ready);
        sql(rollback, "UPDATE linux_geocaching_records SET signed_record=x'00' WHERE revision=1");
        assert(store.head(one.id, out) == Result::Corrupt && out.bytes.empty());
        assert(accept(store, three) == Result::Corrupt);
    }
    {
        const auto gap = root / "missing-gap-history.sqlite3";
        Store store(gap, fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready && accept(store, three) == Result::Ready);
        sql(gap, "DELETE FROM linux_geocaching_records WHERE revision=1");
        assert(store.head(one.id, out) == Result::Corrupt);
        assert(accept(store, two) == Result::Corrupt); // A pre-existing gap cannot hide lost retained evidence.
    }
    {
        const auto orphan = root / "orphan.sqlite3";
        Store store(orphan, fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready);
        sql(orphan, "DELETE FROM linux_geocaching_heads");
        assert(store.head(one.id, out) == Result::Corrupt);
        assert(accept(store, two) == Result::Corrupt);
    }
    {
        const auto concurrent = root / "concurrent.sqlite3";
        Store store(concurrent, fixture.crypto);
        assert(store.open() == Result::Ready && accept(store, one) == Result::Ready);
        const auto contender = [&](const Signed& record)
        {
            platform::linux_runtime::LinuxGeocachingCrypto crypto;
            Store connection(concurrent, crypto);
            assert(connection.open() == Result::Ready);
            return accept(connection, record);
        };
        auto first = std::async(std::launch::async, [&]
                                { return contender(two); });
        auto second = std::async(std::launch::async, [&]
                                 { return contender(fork); });
        const auto a = first.get(), b = second.get();
        assert((a == Result::Ready && b == Result::Conflict) || (a == Result::Conflict && b == Result::Ready));
        assert(store.head(one.id, out) == Result::Conflict);
    }
    struct Unavailable final : gc::protocol::RecordCrypto
    {
        bool sha256(gc::ByteView, uint8_t[32]) override { return false; }
        gc::protocol::VerificationResult verifyEd25519(gc::ByteView, gc::ByteView, gc::ByteView) override { return gc::protocol::VerificationResult::CryptoUnavailable; }
    } unavailable;
    Store unavailable_store(path, unavailable);
    assert(unavailable_store.open() == Result::Ready);
    assert(accept(unavailable_store, one) == Result::CryptoUnavailable);
    assert(unavailable_store.read(one.id, one.hash, out) == Result::CryptoUnavailable && out.bytes.empty());
    std::filesystem::remove_all(root);
#endif
}
