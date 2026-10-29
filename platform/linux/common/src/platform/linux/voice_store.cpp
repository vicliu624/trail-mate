#include "platform/linux/voice_store.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <sqlite3.h>
#include <vector>

namespace platform::linux_runtime
{
namespace
{
namespace vmp = ::chat::voice::vmp;
using Inbox = vmp::VoiceMessageInbox;
using Metadata = vmp::VoiceMessageMetadata;
using Result = LinuxVoiceStore::Result;
constexpr size_t max_payload = 16 + vmp::kVoiceInboxCapacity * (40 + VoiceClip::bytes_per_frame * VoiceClip::max_frames);
using Bytes = std::vector<uint8_t>;
struct InboxDeleter
{
    void operator()(Inbox* inbox) const
    {
        if (inbox)
        {
            inbox->clear();
            delete inbox;
        }
    }
};
using InboxPtr = std::unique_ptr<Inbox, InboxDeleter>;
InboxPtr makeInbox(const Inbox* source = nullptr) { return InboxPtr(source ? new Inbox(*source) : new Inbox); }
void integer(Bytes& bytes, uint64_t value, size_t width)
{
    for (size_t i = width; i; --i) bytes.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));
}
struct Reader
{
    const uint8_t* bytes;
    size_t size, offset = 0;
    bool good = true;
    uint64_t integer(size_t width)
    {
        if (!good || width > size - offset)
        {
            good = false;
            return 0;
        }
        uint64_t value = 0;
        for (size_t i = 0; i < width; ++i) value = (value << 8) | bytes[offset++];
        return value;
    }
};
Bytes encode(const Inbox& inbox)
{
    std::array<Metadata, vmp::kVoiceInboxCapacity> rows;
    const auto count = inbox.listMetadata(rows.data(), rows.size());
    Bytes out;
    out.reserve(max_payload);
    integer(out, 0x564d5053, 4); // VMPS, canonical byte order (not a wire frame).
    integer(out, 2, 1);
    integer(out, count, 1);
    integer(out, 0, 2);
    integer(out, inbox.nextLocalId(), 8);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& row = rows[i];
        vmp::VoiceMessageView view;
        if (!inbox.get(row.local_id, &view)) return {};
        integer(out, row.local_id, 8);
        integer(out, row.sender_id, 4);
        integer(out, row.target_id, 4);
        integer(out, row.session_id, 8);
        integer(out, row.object_fingerprint, 4);
        integer(out, row.received_at_seconds, 4);
        integer(out, row.encoded_media_len, 2);
        integer(out, static_cast<uint8_t>(row.codec), 1);
        integer(out, static_cast<uint8_t>(row.mode), 1);
        integer(out, row.flags, 1);
        integer(out, static_cast<uint8_t>(row.delivery), 1);
        integer(out, static_cast<uint8_t>(row.presentation_protocol), 1);
        integer(out, row.presentation_channel, 1);
        out.insert(out.end(), view.encoded_media, view.encoded_media + row.encoded_media_len);
    }
    return out;
}
Result decode(const Bytes& payload, Inbox& inbox)
{
    if (payload.size() < 16 || payload.size() > max_payload) return Result::Corrupt;
    Reader input{payload.data(), payload.size()};
    if (input.integer(4) != 0x564d5053) return Result::Corrupt;
    const auto schema = input.integer(1);
    if (schema != 1 && schema != 2) return Result::Unsupported;
    const auto count = input.integer(1);
    if (count > vmp::kVoiceInboxCapacity || input.integer(2) != 0) return Result::Corrupt;
    const auto next_id = input.integer(8);
    for (size_t i = 0; i < count; ++i)
    {
        Metadata row;
        row.local_id = input.integer(8);
        row.sender_id = static_cast<uint32_t>(input.integer(4));
        row.target_id = static_cast<uint32_t>(input.integer(4));
        row.session_id = input.integer(8);
        row.object_fingerprint = static_cast<uint32_t>(input.integer(4));
        row.received_at_seconds = static_cast<uint32_t>(input.integer(4));
        row.encoded_media_len = static_cast<uint16_t>(input.integer(2));
        row.codec = static_cast<vmp::Codec>(input.integer(1));
        row.mode = static_cast<vmp::DeliveryMode>(input.integer(1));
        row.flags = static_cast<uint8_t>(input.integer(1));
        row.delivery = static_cast<vmp::VoiceDeliveryState>(input.integer(1));
        row.presentation_protocol = static_cast<vmp::VoicePresentationProtocol>(input.integer(1));
        row.presentation_channel = static_cast<uint8_t>(input.integer(1));
        if (!input.good || !row.sender_id || !row.session_id || row.local_id == UINT64_MAX ||
            row.codec != vmp::Codec::Codec2_1300 || !row.encoded_media_len || row.encoded_media_len > VoiceClip::bytes_per_frame * VoiceClip::max_frames || row.encoded_media_len % VoiceClip::bytes_per_frame ||
            (row.flags & ~uint8_t(15)) || !vmp::isValidVoicePresentationBinding(row.presentation_protocol, row.presentation_channel) ||
            row.encoded_media_len > input.size - input.offset) return Result::Corrupt;
        if (!inbox.restore(row, input.bytes + input.offset, row.encoded_media_len, count - i) || inbox.size() != i + 1) return Result::Corrupt;
        input.offset += row.encoded_media_len;
    }
    return input.good && input.offset == input.size && inbox.restoreNextLocalId(next_id) ? Result::Ready : Result::Corrupt;
}
Bytes generationBytes(uint64_t generation)
{
    Bytes out;
    integer(out, generation, 8);
    return out;
}
uint32_t checksum(uint64_t generation, const Bytes& payload)
{
    uint32_t crc = 0xffffffff;
    const auto update = [&](const Bytes& bytes)
    {
        for (auto byte : bytes)
        {
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    };
    update(generationBytes(generation));
    update(payload);
    return crc ^ 0xffffffff;
}
uint32_t fenceChecksum(uint32_t previous, uint64_t sequence, const Bytes& key)
{
    auto bytes = generationBytes(sequence);
    bytes.insert(bytes.end(), key.begin(), key.end());
    return checksum(previous, bytes);
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
struct Transaction
{
    sqlite3* db;
    bool active;
    explicit Transaction(sqlite3* value) : db(value), active(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK) {}
    ~Transaction()
    {
        if (active) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    bool commit()
    {
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        active = false;
        return true;
    }
};
} // namespace
struct LinuxVoiceStore::Impl
{
    explicit Impl(std::filesystem::path value) : path(std::move(value)) {}
    ~Impl()
    {
        inbox->clear();
        if (db) sqlite3_close(db);
    }
    Result read(Bytes& payload, uint64_t& generation, Inbox& restored, int* schema_out = nullptr)
    {
        Statement query;
        if (!query.prepare(db, "SELECT schema,generation,payload,checksum,(SELECT COUNT(*) FROM linux_vmp_voice_snapshot) FROM linux_vmp_voice_snapshot WHERE singleton=1")) return Result::Error;
        const int step = sqlite3_step(query.value);
        if (step == SQLITE_DONE) return Result::Corrupt;
        if (step != SQLITE_ROW) return Result::Error;
        if (sqlite3_column_type(query.value, 0) != SQLITE_INTEGER || sqlite3_column_type(query.value, 1) != SQLITE_BLOB ||
            sqlite3_column_type(query.value, 2) != SQLITE_BLOB || sqlite3_column_type(query.value, 3) != SQLITE_INTEGER) return Result::Corrupt;
        const auto schema = sqlite3_column_int64(query.value, 0);
        if (schema != 1 && schema != 2) return Result::Unsupported;
        if (sqlite3_column_int64(query.value, 4) != 1 || sqlite3_column_bytes(query.value, 1) != 8) return Result::Corrupt;
        const auto* encoded_generation = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 1));
        if (!encoded_generation) return Result::Corrupt;
        Reader reader{encoded_generation, 8};
        generation = reader.integer(8);
        const auto length = sqlite3_column_bytes(query.value, 2);
        const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(query.value, 2));
        const auto expected = sqlite3_column_int64(query.value, 3);
        if (!generation || !bytes || length < 16 || static_cast<size_t>(length) > max_payload || expected < 0 || expected > UINT32_MAX) return Result::Corrupt;
        payload.assign(bytes, bytes + length);
        if (payload[4] != schema) return Result::Corrupt;
        if (schema_out) *schema_out = static_cast<int>(schema);
        if (checksum(generation, payload) != static_cast<uint32_t>(expected)) return Result::Corrupt;
        return decode(payload, restored);
    }
    bool write(const Bytes& payload, uint64_t generation, bool insert)
    {
        Statement query;
        if (!query.prepare(db, insert ? "INSERT INTO linux_vmp_voice_snapshot(singleton,schema,generation,payload,checksum) VALUES(1,2,?,?,?)"
                                      : "UPDATE linux_vmp_voice_snapshot SET schema=2,generation=?,payload=?,checksum=? WHERE singleton=1")) return false;
        const auto encoded = generationBytes(generation);
        sqlite3_bind_blob(query.value, 1, encoded.data(), static_cast<int>(encoded.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(query.value, 2, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(query.value, 3, checksum(generation, payload));
        return sqlite3_step(query.value) == SQLITE_DONE && sqlite3_changes(db) == 1;
    }
    Result fence(bool initialize, uint64_t& count, uint32_t& root)
    {
        Statement exists;
        if (!exists.prepare(db, "SELECT COUNT(*) FROM sqlite_schema WHERE name IN ('linux_vmp_private_reservations','linux_vmp_private_fence')") || sqlite3_step(exists.value) != SQLITE_ROW) return Result::Error;
        const auto tables = sqlite3_column_int(exists.value, 0);
        if (!tables && initialize)
        {
            if (sqlite3_exec(db,
                             "CREATE TABLE linux_vmp_private_reservations(key_id BLOB PRIMARY KEY CHECK(length(key_id)=32),sequence INTEGER NOT NULL UNIQUE CHECK(sequence>0),checksum INTEGER NOT NULL) WITHOUT ROWID;"
                             "CREATE TABLE linux_vmp_private_fence(singleton INTEGER PRIMARY KEY CHECK(singleton=1),count INTEGER NOT NULL,checksum INTEGER NOT NULL);"
                             "INSERT INTO linux_vmp_private_fence VALUES(1,0,0)",
                             nullptr, nullptr, nullptr) != SQLITE_OK) return Result::Error;
        }
        else if (tables != 2) return Result::Corrupt;
        Statement state, rows;
        if (!state.prepare(db, "SELECT count,checksum,(SELECT COUNT(*) FROM linux_vmp_private_fence) FROM linux_vmp_private_fence WHERE singleton=1") ||
            sqlite3_step(state.value) != SQLITE_ROW) return Result::Corrupt;
        const auto expected_count = sqlite3_column_int64(state.value, 0), expected_root = sqlite3_column_int64(state.value, 1);
        if (sqlite3_column_type(state.value, 0) != SQLITE_INTEGER || sqlite3_column_type(state.value, 1) != SQLITE_INTEGER ||
            sqlite3_column_int64(state.value, 2) != 1 || expected_count < 0 || expected_root < 0 || expected_root > UINT32_MAX) return Result::Corrupt;
        if (!rows.prepare(db, "SELECT sequence,key_id,checksum FROM linux_vmp_private_reservations ORDER BY sequence")) return Result::Error;
        count = 0;
        root = 0;
        int step;
        while ((step = sqlite3_step(rows.value)) == SQLITE_ROW)
        {
            const auto sequence = sqlite3_column_int64(rows.value, 0), crc = sqlite3_column_int64(rows.value, 2);
            const auto* key = static_cast<const uint8_t*>(sqlite3_column_blob(rows.value, 1));
            if (sqlite3_column_type(rows.value, 0) != SQLITE_INTEGER || sqlite3_column_type(rows.value, 1) != SQLITE_BLOB ||
                sqlite3_column_type(rows.value, 2) != SQLITE_INTEGER || sequence <= 0 || static_cast<uint64_t>(sequence) != count + 1 ||
                !key || sqlite3_column_bytes(rows.value, 1) != 32 || crc < 0 || crc > UINT32_MAX || count >= static_cast<uint64_t>(expected_count)) return Result::Corrupt;
            const Bytes identifier(key, key + 32);
            if (checksum(sequence, identifier) != static_cast<uint32_t>(crc)) return Result::Corrupt;
            root = fenceChecksum(root, sequence, identifier);
            ++count;
        }
        return step == SQLITE_DONE && count == static_cast<uint64_t>(expected_count) && root == expected_root ? Result::Ready : Result::Corrupt;
    }
    Result check()
    {
        if (!ready || !db) return Result::Error;
        Transaction transaction(db);
        if (!transaction.active) return Result::Error;
        Bytes payload;
        uint64_t current = 0;
        auto validation = makeInbox();
        const auto checked = read(payload, current, *validation);
        return checked == Result::Ready && current != generation ? Result::Conflict : checked;
    }
    Result save(InboxPtr candidate)
    {
        if (!ready || !db) return Result::Error;
        if (generation == UINT64_MAX) return Result::Exhausted;
        Transaction transaction(db);
        if (!transaction.active) return Result::Error;
        Bytes previous;
        uint64_t current = 0;
        auto validation = makeInbox();
        const auto checked = read(previous, current, *validation);
        if (checked != Result::Ready) return checked;
        if (current != generation) return Result::Conflict;
        const auto payload = encode(*candidate);
        if (payload.empty() || payload.size() > max_payload || !write(payload, generation + 1, false) || !transaction.commit()) return Result::Error;
        inbox->clear();
        inbox = std::move(candidate);
        ++generation;
        return Result::Ready;
    }
    std::filesystem::path path;
    sqlite3* db = nullptr;
    InboxPtr inbox = makeInbox();
    uint64_t generation = 0;
    bool ready = false;
};
LinuxVoiceStore::LinuxVoiceStore(std::filesystem::path path) : impl_(std::make_unique<Impl>(std::move(path))) {}
LinuxVoiceStore::~LinuxVoiceStore() = default;
LinuxVoiceStore::Result LinuxVoiceStore::open()
{
    if (impl_->ready) return Result::Ready;
    if (!impl_->db)
    {
        std::error_code error;
        if (!impl_->path.parent_path().empty()) std::filesystem::create_directories(impl_->path.parent_path(), error);
        if (error) return Result::Error;
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
        flags |= SQLITE_OPEN_NOFOLLOW;
#endif
        if (sqlite3_open_v2(impl_->path.string().c_str(), &impl_->db, flags, nullptr) != SQLITE_OK)
        {
            if (impl_->db) sqlite3_close(impl_->db);
            impl_->db = nullptr;
            return Result::Error;
        }
        sqlite3_busy_timeout(impl_->db, 2000);
        sqlite3_limit(impl_->db, SQLITE_LIMIT_LENGTH, static_cast<int>(max_payload + 1024));
    }
    if (sqlite3_exec(impl_->db, "PRAGMA synchronous=FULL", nullptr, nullptr, nullptr) != SQLITE_OK) return Result::Error;
    Transaction transaction(impl_->db);
    if (!transaction.active) return Result::Error;
    Statement exists;
    if (!exists.prepare(impl_->db, "SELECT COUNT(*) FROM sqlite_schema WHERE name='linux_vmp_voice_snapshot'")) return Result::Error;
    if (sqlite3_step(exists.value) != SQLITE_ROW) return Result::Error;
    const bool fresh = !sqlite3_column_int(exists.value, 0);
    if (fresh)
    {
        if (sqlite3_exec(impl_->db, "CREATE TABLE linux_vmp_voice_snapshot(singleton INTEGER PRIMARY KEY CHECK(singleton=1),schema INTEGER NOT NULL,generation BLOB NOT NULL CHECK(length(generation)=8),payload BLOB NOT NULL,checksum INTEGER NOT NULL)", nullptr, nullptr, nullptr) != SQLITE_OK ||
            !impl_->write(encode(*impl_->inbox), 1, true)) return Result::Error;
    }
    auto candidate = makeInbox();
    Bytes payload;
    uint64_t generation = 0;
    int schema = 0;
    const auto checked = impl_->read(payload, generation, *candidate, &schema);
    if (checked != Result::Ready) return checked;
    uint64_t reservations = 0;
    uint32_t root = 0;
    const auto fenced = impl_->fence(fresh || schema == 1, reservations, root);
    if (fenced != Result::Ready) return fenced;
    const auto normalized = encode(*candidate);
    if (normalized != payload)
    {
        if (generation == UINT64_MAX) return Result::Exhausted;
        if (!impl_->write(normalized, ++generation, false)) return Result::Error;
    }
    if (!transaction.commit()) return Result::Error;
    impl_->inbox = std::move(candidate);
    impl_->generation = generation;
    impl_->ready = true;
    return Result::Ready;
}
LinuxVoiceStore::Result LinuxVoiceStore::reservePrivateControl(const std::array<uint8_t, 32>& identifier)
{
    if (!impl_->ready) return Result::Error;
    if (std::all_of(identifier.begin(), identifier.end(), [](auto byte)
                    { return byte == 0; })) return Result::Invalid;
    Transaction transaction(impl_->db);
    if (!transaction.active) return Result::Error;
    Bytes snapshot;
    uint64_t generation = 0;
    auto validation = makeInbox();
    const auto checked = impl_->read(snapshot, generation, *validation);
    if (checked != Result::Ready) return checked;
    if (generation != impl_->generation) return Result::Conflict;
    uint64_t count = 0;
    uint32_t root = 0;
    const auto fenced = impl_->fence(false, count, root);
    if (fenced != Result::Ready) return fenced;
    Statement known;
    if (!known.prepare(impl_->db, "SELECT 1 FROM linux_vmp_private_reservations WHERE key_id=?")) return Result::Error;
    sqlite3_bind_blob(known.value, 1, identifier.data(), identifier.size(), SQLITE_TRANSIENT);
    const auto step = sqlite3_step(known.value);
    if (step == SQLITE_ROW) return Result::Duplicate;
    if (step != SQLITE_DONE) return Result::Error;
    if (count == static_cast<uint64_t>(INT64_MAX)) return Result::Exhausted;
    const Bytes key(identifier.begin(), identifier.end());
    Statement insert, state;
    if (!insert.prepare(impl_->db, "INSERT INTO linux_vmp_private_reservations(key_id,sequence,checksum) VALUES(?,?,?)") ||
        !state.prepare(impl_->db, "UPDATE linux_vmp_private_fence SET count=?,checksum=? WHERE singleton=1")) return Result::Error;
    sqlite3_bind_blob(insert.value, 1, identifier.data(), identifier.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert.value, 2, count + 1);
    sqlite3_bind_int64(insert.value, 3, checksum(count + 1, key));
    if (sqlite3_step(insert.value) != SQLITE_DONE) return Result::Error;
    sqlite3_bind_int64(state.value, 1, count + 1);
    sqlite3_bind_int64(state.value, 2, fenceChecksum(root, count + 1, key));
    if (sqlite3_step(state.value) != SQLITE_DONE || sqlite3_changes(impl_->db) != 1 || !transaction.commit()) return Result::Error;
    return Result::Ready;
}
LinuxVoiceStore::Result LinuxVoiceStore::append(const vmp::ControlFrame& control, const VoiceClip& clip, bool outgoing,
                                                uint32_t time, vmp::VoicePresentationProtocol protocol, uint8_t channel, uint64_t& local_id)
{
    local_id = 0;
    if (!impl_->ready) return Result::Error;
    if (!clip.valid() || control.codec != vmp::Codec::Codec2_1300 || channel != control.conversation_channel) return Result::Invalid;
    auto validation = makeInbox();
    const auto validate = outgoing ? validation->storeOutgoing(control, clip.bytes.data(), clip.size, time, protocol, channel)
                                   : validation->store(control, clip.bytes.data(), clip.size, true, time, protocol, channel);
    if (validate != vmp::VoiceInboxStoreResult::Stored) return Result::Invalid;
    std::array<Metadata, vmp::kVoiceInboxCapacity> rows;
    const auto count = impl_->inbox->listMetadata(rows.data(), rows.size());
    for (size_t i = 0; i < count; ++i)
    {
        const auto& row = rows[i];
        if (row.sender_id != control.sender_id || row.session_id != control.session_id) continue;
        vmp::VoiceMessageView old;
        if (!impl_->inbox->get(row.local_id, &old) || row.target_id != control.target_id || row.object_fingerprint != control.object_fingerprint ||
            row.encoded_media_len != clip.size || vmp::voiceMessageOutgoing(row) != outgoing || row.presentation_protocol != protocol || row.presentation_channel != channel ||
            std::memcmp(old.encoded_media, clip.bytes.data(), clip.size)) return Result::Conflict;
        const auto current = impl_->check();
        if (current != Result::Ready) return current;
        local_id = row.local_id;
        return Result::Duplicate;
    }
    if (impl_->inbox->nextLocalId() == UINT64_MAX) return Result::Exhausted;
    auto candidate = makeInbox(impl_->inbox.get());
    uint64_t assigned = 0;
    const auto stored = outgoing ? candidate->storeOutgoing(control, clip.bytes.data(), clip.size, time, protocol, channel, &assigned)
                                 : candidate->store(control, clip.bytes.data(), clip.size, true, time, protocol, channel, &assigned);
    if (stored != vmp::VoiceInboxStoreResult::Stored) return Result::Invalid;
    const auto saved = impl_->save(std::move(candidate));
    if (saved == Result::Ready) local_id = assigned;
    return saved;
}
LinuxVoiceStore::Result LinuxVoiceStore::delivery(uint64_t id, vmp::VoiceDeliveryState state)
{
    if (!impl_->ready || state == vmp::VoiceDeliveryState::Received) return Result::Invalid;
    vmp::VoiceMessageView view;
    if (!impl_->inbox->get(id, &view)) return Result::Missing;
    if (!vmp::voiceMessageOutgoing(view.metadata)) return Result::Invalid;
    if (view.metadata.delivery == state) return impl_->check();
    if (view.metadata.delivery != vmp::VoiceDeliveryState::Sending || (state != vmp::VoiceDeliveryState::Sent && state != vmp::VoiceDeliveryState::Failed)) return Result::Invalid;
    auto candidate = makeInbox(impl_->inbox.get());
    if (!candidate->updateDeliveryState(id, state)) return Result::Invalid;
    return impl_->save(std::move(candidate));
}
LinuxVoiceStore::Result LinuxVoiceStore::markRead(vmp::VoicePresentationProtocol protocol, uint8_t channel, uint32_t peer, bool broadcast)
{
    if (!impl_->ready || !vmp::isValidVoicePresentationBinding(protocol, channel)) return Result::Invalid;
    auto candidate = makeInbox(impl_->inbox.get());
    if (!candidate->markConversationRead(protocol, channel, peer, broadcast)) return impl_->check();
    return impl_->save(std::move(candidate));
}
LinuxVoiceStore::Result LinuxVoiceStore::erase(uint64_t id)
{
    if (!impl_->ready) return Result::Error;
    auto candidate = makeInbox(impl_->inbox.get());
    if (!candidate->erase(id)) return Result::Missing;
    return impl_->save(std::move(candidate));
}
size_t LinuxVoiceStore::list(Metadata* rows, size_t capacity) const { return impl_->ready ? impl_->inbox->listMetadata(rows, capacity) : 0; }
bool LinuxVoiceStore::media(uint64_t id, VoiceClip& out) const
{
    out.clear();
    vmp::VoiceMessageView view;
    if (!impl_->ready || !impl_->inbox->get(id, &view) || view.metadata.encoded_media_len > out.bytes.size()) return false;
    out.size = view.metadata.encoded_media_len;
    std::copy_n(view.encoded_media, out.size, out.bytes.data());
    return out.valid();
}
} // namespace platform::linux_runtime
