#include "platform/linux/hostlink_geocaching_drafts.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/geocaching_draft_store.h"
#include "platform/linux/hostlink_track_signer.h"
#include "platform/linux/route_image_source_io.h"
#include <algorithm>
#include <sqlite3.h>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
namespace
{
namespace mgmt = ::hostlink::management;
using Status = mgmt::Status;
using Operation = mgmt::Operation;
using Store = LinuxGeocachingDraftStore;
bool nonzero(const uint8_t* bytes, size_t size)
{
    uint8_t any = 0;
    if (bytes)
        for (size_t i = 0; i < size; ++i) any |= bytes[i];
    return any != 0;
}
struct Statement
{
    sqlite3_stmt* value = nullptr;
    ~Statement()
    {
        if (value) sqlite3_finalize(value);
    }
    bool prepare(sqlite3* db, const char* sql) { return sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK; }
};
struct Upload
{
    std::array<uint8_t, 16> id{}, owner{}, draft{};
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 32> hash{};
    uint32_t generation = 0, total = 0;
    uint64_t expected = 0;
    uint8_t phase = 0;
    std::vector<uint8_t> bytes;
};
Status storage_status(Store::Result result)
{
    switch (result)
    {
    case Store::Result::Ready:
        return Status::Ok;
    case Store::Result::Missing:
    case Store::Result::Conflict:
    case Store::Result::PublicationRequired:
        return Status::Conflict;
    case Store::Result::Forbidden:
        return Status::Unauthorized;
    case Store::Result::Invalid:
        return Status::InvalidField;
    default:
        return Status::StorageUnavailable;
    }
}
Status transfer_reply(const Upload& upload, uint8_t* output, size_t capacity, size_t& written)
{
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !writer.binary({upload.id.data(), 16}) || !writer.unsignedInteger(320) ||
        !writer.unsignedInteger(upload.bytes.size()) || !writer.unsignedInteger(upload.phase ? 1 : 0)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace
struct HostlinkGeocachingDrafts::Impl
{
    explicit Impl(std::filesystem::path database) : database(std::move(database)), drafts(this->database) {}
    ~Impl()
    {
        if (db) sqlite3_close(db);
    }
    bool open()
    {
        if (db) return true;
        if (drafts.open() != Store::Result::Ready) return false;
        lease = std::make_unique<RouteImageLease>((database.string() + ".geocaching-upload.lock").c_str());
        if (!*lease) return false;
        sqlite3* opened = nullptr;
        if (sqlite3_open_v2(database.c_str(), &opened, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        {
            if (opened) sqlite3_close(opened);
            return false;
        }
        sqlite3_busy_timeout(opened, 2000);
        const auto schema = "PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS linux_geocaching_draft_upload("
                            "singleton INTEGER PRIMARY KEY CHECK(singleton=1),id BLOB NOT NULL CHECK(length(id)=16),owner BLOB NOT NULL CHECK(length(owner)=16),"
                            "author BLOB NOT NULL CHECK(length(author)=64),generation INTEGER NOT NULL CHECK(generation BETWEEN 1 AND 4294967295),"
                            "total INTEGER NOT NULL CHECK(total BETWEEN 1 AND 32768),hash BLOB NOT NULL CHECK(length(hash)=32),candidate BLOB NOT NULL CHECK(length(candidate)<=32768),"
                            "phase INTEGER NOT NULL CHECK(phase BETWEEN 0 AND 2),draft BLOB NOT NULL CHECK(length(draft)=16),expected BLOB NOT NULL CHECK(length(expected)=8));"
                            "UPDATE linux_geocaching_draft_upload SET owner=zeroblob(16) WHERE singleton=1";
        if (sqlite3_exec(opened, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            sqlite3_close(opened);
            return false;
        }
        db = opened;
        return true;
    }
    bool load(Upload& upload, bool& exists)
    {
        exists = false;
        Statement query;
        if (!open() || !query.prepare(db, "SELECT id,owner,author,generation,total,hash,candidate,phase,draft,expected FROM linux_geocaching_draft_upload WHERE singleton=1")) return false;
        const auto step = sqlite3_step(query.value);
        if (step == SQLITE_DONE) return true;
        if (step != SQLITE_ROW) return false;
        exists = true;
        const auto copy = [&](int column, uint8_t* target, size_t bytes)
        {
            const auto* source = sqlite3_column_blob(query.value, column);
            if (!source || sqlite3_column_bytes(query.value, column) != int(bytes)) return false;
            std::memcpy(target, source, bytes);
            return true;
        };
        std::array<uint8_t, 8> expected{};
        if (!copy(0, upload.id.data(), 16) || !nonzero(upload.id.data(), 16) || !copy(1, upload.owner.data(), 16) || !copy(2, upload.author.data(), 64) ||
            !copy(5, upload.hash.data(), 32) || !copy(8, upload.draft.data(), 16) || !copy(9, expected.data(), 8)) return false;
        const auto generation = sqlite3_column_int64(query.value, 3), total = sqlite3_column_int64(query.value, 4), phase = sqlite3_column_int64(query.value, 7);
        const int bytes = sqlite3_column_bytes(query.value, 6);
        if (generation <= 0 || generation > UINT32_MAX || total <= 0 || total > 32768 || phase < 0 || phase > 2 || bytes < 0 || bytes > total || (phase && bytes != total)) return false;
        upload.generation = uint32_t(generation);
        upload.total = uint32_t(total);
        upload.phase = uint8_t(phase);
        const auto* source = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 6));
        if (bytes && !source) return false;
        if (bytes) upload.bytes.assign(source, source + bytes);
        for (unsigned i = 0; i < 8; ++i) upload.expected |= uint64_t(expected[i]) << (8 * i);
        if (phase)
        {
            LinuxGeocachingCrypto crypto;
            std::array<uint8_t, 32> hash{};
            if (!crypto.sha256({upload.bytes.data(), upload.bytes.size()}, hash.data()) || hash != upload.hash) return false;
        }
        return phase != 2 || (nonzero(upload.draft.data(), 16) && upload.expected != UINT64_MAX);
    }
    bool save(const Upload& upload)
    {
        Statement write;
        if (!write.prepare(db, "INSERT OR REPLACE INTO linux_geocaching_draft_upload(singleton,id,owner,author,generation,total,hash,candidate,phase,draft,expected) VALUES(1,?,?,?,?,?,?,?,?,?,?)")) return false;
        sqlite3_bind_blob(write.value, 1, upload.id.data(), 16, SQLITE_TRANSIENT);
        sqlite3_bind_blob(write.value, 2, upload.owner.data(), 16, SQLITE_TRANSIENT);
        sqlite3_bind_blob(write.value, 3, upload.author.data(), 64, SQLITE_TRANSIENT);
        sqlite3_bind_int64(write.value, 4, upload.generation);
        sqlite3_bind_int64(write.value, 5, upload.total);
        sqlite3_bind_blob(write.value, 6, upload.hash.data(), 32, SQLITE_TRANSIENT);
        if (upload.bytes.empty()) sqlite3_bind_zeroblob(write.value, 7, 0);
        else sqlite3_bind_blob(write.value, 7, upload.bytes.data(), upload.bytes.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(write.value, 8, upload.phase);
        sqlite3_bind_blob(write.value, 9, upload.draft.data(), 16, SQLITE_TRANSIENT);
        std::array<uint8_t, 8> expected{};
        for (unsigned i = 0; i < 8; ++i) expected[i] = uint8_t(upload.expected >> (8 * i));
        sqlite3_bind_blob(write.value, 10, expected.data(), 8, SQLITE_TRANSIENT);
        return sqlite3_step(write.value) == SQLITE_DONE;
    }
    std::filesystem::path database;
    Store drafts;
    std::unique_ptr<RouteImageLease> lease;
    sqlite3* db = nullptr;
    uint64_t touched = 0;
};
HostlinkGeocachingDrafts::HostlinkGeocachingDrafts(std::filesystem::path database) : impl_(std::make_unique<Impl>(std::move(database))) {}
HostlinkGeocachingDrafts::~HostlinkGeocachingDrafts() = default;
bool HostlinkGeocachingDrafts::supported() { return true; }
bool HostlinkGeocachingDrafts::routes(const mgmt::EnvelopeView& request)
{
    if (request.code >= 64 && request.code <= 66) return true;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    geocaching::ByteView id;
    size_t fields = 0;
    if (!reader.array(fields, 4) || !reader.binary(id, 16) || id.size != 16) return false;
    if (request.code == uint16_t(Operation::TransferBegin))
    {
        uint64_t kind = 0;
        return reader.unsignedInteger(kind) && kind == 2;
    }
    if (request.code < 33 || request.code > 36) return false;
    Upload upload;
    bool exists = false;
    return impl_->load(upload, exists) && exists && !std::memcmp(upload.id.data(), id.data, 16);
}
bool HostlinkGeocachingDrafts::identityBusy()
{
    Upload upload;
    bool exists = false;
    return !impl_->load(upload, exists) || (exists && upload.phase < 2 && nonzero(upload.owner.data(), 16));
}
void HostlinkGeocachingDrafts::close(const uint8_t* session)
{
    Upload upload;
    bool exists = false;
    if (!impl_->load(upload, exists) || !exists || (session && std::memcmp(session, upload.owner.data(), 16))) return;
    upload.owner.fill(0);
    impl_->save(upload);
    impl_->touched = 0;
}
void HostlinkGeocachingDrafts::advance(uint64_t now, bool connected)
{
    if (impl_->touched && (!connected || now < impl_->touched || now - impl_->touched >= 30000)) close();
}
Status HostlinkGeocachingDrafts::process(const mgmt::EnvelopeView& request, uint64_t now, uint8_t* output, size_t capacity, size_t& written)
try
{
    written = 0;
    if (!request.session || !nonzero(request.session, 16) || !output || capacity < 384) return Status::InvalidField;
    advance(now, true);
    LinuxReticulumIdentity identity(impl_->database);
    if (!identity.begin()) return Status::StorageUnavailable;
    if (!impl_->open()) return Status::Busy;
    const auto code = static_cast<Operation>(request.code);
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t offset = 0, count = 0, expected = 0, generation = 0;
    geocaching::ByteView id;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (code == Operation::CacheListOwned || code == Operation::CacheDraftGet)
    {
        const bool catalog = code == Operation::CacheListOwned;
        bool bound = false;
        if (!reader.array(fields, 5) || fields != (catalog ? 3u : 5u) ||
            (!catalog && (!reader.binary(id, 16) || id.size != 16 || !nonzero(id.data, 16))) || !reader.unsignedInteger(offset) || offset > UINT32_MAX ||
            (!catalog && (!reader.unsignedInteger(count) || !count || count > 320))) return Status::InvalidField;
        if (catalog)
        {
            if (reader.position() >= request.body_size) return Status::InvalidField;
            bound = request.body[reader.position()] != 0xc0;
            if (!(bound ? reader.unsignedInteger(expected) : reader.nil()) || (offset && !bound)) return Status::InvalidField;
        }
        else if (!reader.unsignedInteger(expected) || (offset && !expected)) return Status::InvalidField;
        if (!reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX || !reader.finished()) return Status::InvalidField;
        if (generation != identity.generation()) return Status::StaleIdentity;
        if (catalog)
        {
            Store::Catalog page;
            const auto status = storage_status(impl_->drafts.catalog(uint32_t(offset), bound, expected, page));
            if (status != Status::Ok) return status;
            if (page.total > UINT32_MAX) return Status::LimitExceeded;
            if (!writer.array(4) || !writer.unsignedInteger(page.generation) || !writer.unsignedInteger(offset) || !writer.unsignedInteger(page.total)) return Status::Internal;
            if (!page.present)
            {
                if (!writer.nil()) return Status::Internal;
            }
            else
            {
                geocaching::storage::DraftView row;
                if (!page.row.view(row)) return Status::StorageUnavailable;
                // Native publication retention is a separate port. Do not
                // fabricate publication flags for a retained published base.
                if (row.base_hash.size) return Status::Unsupported;
                if (!writer.array(9) || !writer.binary({page.row.id.data(), 16}) || !writer.unsignedInteger(row.generation) ||
                    !(row.author.size ? writer.binary(row.author) : writer.nil()) || !writer.text(row.name) || !writer.unsignedInteger(row.state) ||
                    !(row.has_coordinates ? writer.signedInteger(row.latitude_e7) : writer.nil()) || !(row.has_coordinates ? writer.signedInteger(row.longitude_e7) : writer.nil()) ||
                    !writer.nil() || !writer.array(3) || !writer.unsignedInteger(0) || !writer.unsignedInteger(0) || !writer.unsignedInteger(0)) return Status::Internal;
            }
        }
        else
        {
            Store::Id key{};
            std::copy_n(id.data, 16, key.begin());
            Store::Row row;
            const auto status = storage_status(impl_->drafts.read(key, row));
            if (status != Status::Ok) return status;
            geocaching::storage::DraftView view;
            if (!row.view(view)) return Status::StorageUnavailable;
            if ((expected && expected != view.generation) || offset > row.bytes.size()) return Status::Conflict;
            const auto length = std::min<size_t>(count, row.bytes.size() - offset);
            if (!writer.array(5) || !writer.binary({key.data(), 16}) || !writer.unsignedInteger(view.generation) || !writer.unsignedInteger(row.bytes.size()) ||
                !writer.unsignedInteger(offset) || !writer.binary({row.bytes.data() + offset, length})) return Status::Internal;
        }
        written = writer.size();
        return Status::Ok;
    }
    Upload upload;
    bool exists = false;
    if (!impl_->load(upload, exists)) return Status::StorageUnavailable;
    if (code == Operation::CacheDraftPut)
    {
        geocaching::ByteView draft;
        if (!reader.array(fields, 4) || fields != 4 || !reader.binary(id, 16) || id.size != 16 || !reader.binary(draft, 16) || draft.size != 16 ||
            !reader.unsignedInteger(expected) || expected == UINT64_MAX || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX || !reader.finished()) return Status::InvalidField;
        if (generation != identity.generation()) return Status::StaleIdentity;
        if (!exists) return Status::Expired;
        if (std::memcmp(upload.owner.data(), request.session, 16)) return Status::Unauthorized;
        if (!upload.phase || std::memcmp(upload.id.data(), id.data, 16)) return Status::Conflict;
        if (upload.generation != generation || std::memcmp(upload.author.data(), identity.publicKey().data(), 64)) return Status::StaleIdentity;
        Store::Id key{};
        std::copy_n(draft.data, 16, key.begin());
        if (upload.phase == 2 && (upload.draft != key || upload.expected != expected)) return Status::Conflict;
        const auto saved = storage_status(impl_->drafts.saveWeb(key, expected, {upload.bytes.data(), upload.bytes.size()}, identity.publicKey().data(), uint32_t(generation), upload.id, upload.hash.data()));
        if (saved != Status::Ok) return saved;
        upload.phase = 2;
        upload.draft = key;
        upload.expected = expected;
        if (!impl_->save(upload)) return Status::StorageUnavailable;
        impl_->touched = now;
        if (!writer.array(3) || !writer.binary({upload.id.data(), 16}) || !writer.binary({key.data(), 16}) || !writer.unsignedInteger(expected + 1)) return Status::Internal;
        written = writer.size();
        return Status::Ok;
    }
    const bool begin = code == Operation::TransferBegin, chunk = code == Operation::TransferChunk;
    if (!begin && !chunk && code != Operation::TransferStatus && code != Operation::TransferFinish && code != Operation::TransferAbort) return Status::Unsupported;
    geocaching::ByteView bytes, hash;
    uint64_t total = 0, kind = 0;
    if (!reader.array(fields, 4) || fields != (begin ? 4u : chunk ? 3u
                                                                  : 1u) ||
        !reader.binary(id, 16) || id.size != 16 || !nonzero(id.data, 16) ||
        (begin && (!reader.unsignedInteger(kind) || kind != 2 || !reader.unsignedInteger(total) || !total || total > 32768 || !reader.binary(hash, 32) || hash.size != 32)) ||
        (chunk && (!reader.unsignedInteger(offset) || offset > UINT32_MAX || !reader.binary(bytes, 320) || !bytes.size)) || !reader.finished()) return Status::InvalidField;
    if (begin)
    {
        if (exists && !std::memcmp(upload.id.data(), id.data, 16))
        {
            if (upload.generation != identity.generation() || std::memcmp(upload.author.data(), identity.publicKey().data(), 64)) return Status::StaleIdentity;
            if (upload.total != total || std::memcmp(upload.hash.data(), hash.data, 32)) return Status::Conflict;
            if (nonzero(upload.owner.data(), 16) && std::memcmp(upload.owner.data(), request.session, 16)) return Status::Unauthorized;
        }
        else
        {
            if (exists && upload.phase < 2) return Status::Conflict;
            upload = {};
            std::copy_n(id.data, 16, upload.id.begin());
            std::copy_n(hash.data, 32, upload.hash.begin());
            upload.generation = identity.generation();
            upload.author = identity.publicKey();
            upload.total = uint32_t(total);
        }
        std::copy_n(request.session, 16, upload.owner.begin());
        if (!impl_->save(upload)) return Status::StorageUnavailable;
    }
    else
    {
        if (!exists) return Status::Expired;
        if (std::memcmp(upload.id.data(), id.data, 16)) return Status::Conflict;
        if (code == Operation::TransferAbort && (!nonzero(upload.owner.data(), 16) || !std::memcmp(upload.owner.data(), request.session, 16)))
        {
            if (sqlite3_exec(impl_->db, "DELETE FROM linux_geocaching_draft_upload WHERE singleton=1", nullptr, nullptr, nullptr) != SQLITE_OK) return Status::StorageUnavailable;
            output[0] = 0x90;
            written = 1;
            return Status::Ok;
        }
        if (std::memcmp(upload.owner.data(), request.session, 16)) return Status::Unauthorized;
        if (upload.generation != identity.generation() || upload.author != identity.publicKey()) return Status::StaleIdentity;
        if (chunk)
        {
            if (offset > upload.bytes.size() || bytes.size > upload.total - offset) return Status::Conflict;
            if (offset < upload.bytes.size())
            {
                if (bytes.size > upload.bytes.size() - offset || std::memcmp(upload.bytes.data() + offset, bytes.data, bytes.size)) return Status::Conflict;
            }
            else
            {
                upload.bytes.insert(upload.bytes.end(), bytes.data, bytes.data + bytes.size);
                if (!impl_->save(upload)) return Status::StorageUnavailable;
            }
        }
        if (code == Operation::TransferFinish)
        {
            if (upload.bytes.size() != upload.total) return Status::Conflict;
            LinuxGeocachingCrypto crypto;
            std::array<uint8_t, 32> digest{};
            if (!crypto.sha256({upload.bytes.data(), upload.bytes.size()}, digest.data())) return Status::StorageUnavailable;
            if (digest != upload.hash) return Status::HashMismatch;
            if (!upload.phase)
            {
                upload.phase = 1;
                if (!impl_->save(upload)) return Status::StorageUnavailable;
            }
        }
    }
    impl_->touched = now;
    return transfer_reply(upload, output, capacity, written);
}
catch (...)
{
    written = 0;
    return Status::StorageUnavailable;
}
#else
struct HostlinkGeocachingDrafts::Impl
{
};
HostlinkGeocachingDrafts::HostlinkGeocachingDrafts(std::filesystem::path) : impl_(std::make_unique<Impl>()) {}
HostlinkGeocachingDrafts::~HostlinkGeocachingDrafts() = default;
bool HostlinkGeocachingDrafts::supported() { return false; }
bool HostlinkGeocachingDrafts::routes(const ::hostlink::management::EnvelopeView&) { return false; }
bool HostlinkGeocachingDrafts::identityBusy() { return false; }
void HostlinkGeocachingDrafts::close(const uint8_t*) {}
void HostlinkGeocachingDrafts::advance(uint64_t, bool) {}
::hostlink::management::Status HostlinkGeocachingDrafts::process(const ::hostlink::management::EnvelopeView&, uint64_t, uint8_t*, size_t, size_t& written)
{
    written = 0;
    return ::hostlink::management::Status::Unsupported;
}
#endif
} // namespace platform::linux_runtime
