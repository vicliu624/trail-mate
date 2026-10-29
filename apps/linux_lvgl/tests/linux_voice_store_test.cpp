#include "platform/linux/voice_store.h"
#include <cassert>
#include <filesystem>
#include <sqlite3.h>
#include <unistd.h>
#include <vector>

using Store = platform::linux_runtime::LinuxVoiceStore;
using Clip = platform::linux_runtime::VoiceClip;
using Result = Store::Result;
namespace vmp = chat::voice::vmp;
constexpr auto protocol = vmp::VoicePresentationProtocol::MeshCore;
vmp::ControlFrame offer(uint64_t session, bool broadcast = false)
{
    vmp::ControlFrame out;
    out.type = broadcast ? vmp::ControlType::Announce : vmp::ControlType::Offer;
    out.flags = broadcast ? vmp::ControlFlagBroadcast | vmp::ControlFlagPublicBroadcast : vmp::ControlFlagPrivate;
    out.sender_id = 42;
    out.target_id = broadcast ? vmp::kBroadcastTargetId : 7;
    out.session_id = session;
    out.session_nonce[0] = 1;
    out.phy_profile_id = 1;
    out.encoded_media_len = 7;
    out.total_blocks = 1;
    out.data_start_delay_ms = broadcast ? 700 : 120;
    if (!broadcast) out.ephemeral_public_key[0] = 2;
    out.object_fingerprint = static_cast<uint32_t>(session);
    return out;
}
void sql(const std::filesystem::path& path, const char* command)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, command, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
int64_t scalar(const std::filesystem::path& path, const char* query)
{
    sqlite3* db = nullptr;
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_prepare_v2(db, query, -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return value;
}
void legacySnapshot(const std::filesystem::path& path)
{
    sqlite3* db = nullptr;
    sqlite3_stmt* query = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_prepare_v2(db, "SELECT generation,payload FROM linux_vmp_voice_snapshot", -1, &query, nullptr) == SQLITE_OK);
    assert(sqlite3_step(query) == SQLITE_ROW);
    const auto* generation = static_cast<const uint8_t*>(sqlite3_column_blob(query, 0));
    std::vector<uint8_t> bytes(generation, generation + 8);
    const auto* payload = static_cast<const uint8_t*>(sqlite3_column_blob(query, 1));
    std::vector<uint8_t> old(payload, payload + sqlite3_column_bytes(query, 1));
    sqlite3_finalize(query);
    old[4] = 1;
    bytes.insert(bytes.end(), old.begin(), old.end());
    uint32_t crc = 0xffffffff;
    for (auto byte : bytes)
    {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    assert(sqlite3_prepare_v2(db, "UPDATE linux_vmp_voice_snapshot SET schema=1,payload=?,checksum=?", -1, &query, nullptr) == SQLITE_OK);
    sqlite3_bind_blob(query, 1, old.data(), old.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 2, crc ^ 0xffffffff);
    assert(sqlite3_step(query) == SQLITE_DONE);
    sqlite3_finalize(query);
    assert(sqlite3_exec(db, "DROP TABLE linux_vmp_private_reservations; DROP TABLE linux_vmp_private_fence", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-voice-store-" + std::to_string(getpid()));
    const auto database = root / "attachments.sqlite3";
    Clip clip;
    clip.size = 7;
    clip.bytes[0] = 21;
    uint64_t received = 0, outgoing = 0, interrupted = 0;
    {
        Store store(database);
        assert(store.open() == Result::Ready);
        assert(store.append(offer(1), clip, false, 100, protocol, 0, received) == Result::Ready && received);
        uint64_t duplicate = 0;
        assert(store.append(offer(1), clip, false, 101, protocol, 0, duplicate) == Result::Duplicate && duplicate == received);
        Clip altered = clip;
        altered.bytes[0] ^= 1;
        assert(store.append(offer(1), altered, false, 101, protocol, 0, duplicate) == Result::Conflict && !duplicate);
        assert(store.markRead(protocol, 0, 42, false) == Result::Ready);
        assert(store.append(offer(2, true), clip, true, 102, protocol, 0, outgoing) == Result::Ready);
        assert(store.delivery(outgoing, vmp::VoiceDeliveryState::Sent) == Result::Ready);
        assert(store.delivery(outgoing, vmp::VoiceDeliveryState::Failed) == Result::Invalid);
        assert(store.append(offer(3), clip, true, 103, protocol, 0, interrupted) == Result::Ready);
        sql(database, "CREATE TRIGGER fail_voice BEFORE UPDATE ON linux_vmp_voice_snapshot BEGIN SELECT RAISE(ABORT,'injected write failure'); END");
        uint64_t failed = 9;
        assert(store.append(offer(4), clip, false, 104, protocol, 0, failed) == Result::Error && !failed);
        std::array<vmp::VoiceMessageMetadata, 8> rows;
        assert(store.list(rows.data(), rows.size()) == 3);
        sql(database, "DROP TRIGGER fail_voice");
    }
    {
        Store store(database);
        assert(store.open() == Result::Ready);
        std::array<vmp::VoiceMessageMetadata, 8> rows;
        assert(store.list(rows.data(), rows.size()) == 3);
        assert(rows[0].local_id == interrupted && rows[0].delivery == vmp::VoiceDeliveryState::Failed);
        assert(rows[1].local_id == outgoing && rows[1].delivery == vmp::VoiceDeliveryState::Sent);
        assert(rows[2].local_id == received && vmp::voiceMessageRead(rows[2]));
        Clip restored;
        assert(store.media(received, restored) && restored.size == clip.size && restored.bytes == clip.bytes);
        assert(store.erase(interrupted) == Result::Ready && store.erase(outgoing) == Result::Ready && store.erase(received) == Result::Ready);
        assert(!store.media(received, restored) && !restored.valid());
    }
    uint64_t newest = 0;
    {
        Store store(database);
        assert(store.open() == Result::Ready);
        assert(store.append(offer(4), clip, false, 104, protocol, 0, newest) == Result::Ready && newest > interrupted);
        Store stale(database);
        assert(stale.open() == Result::Ready);
        uint64_t next = 0;
        assert(store.append(offer(5), clip, false, 105, protocol, 0, next) == Result::Ready);
        uint64_t conflict = 0;
        assert(stale.append(offer(6), clip, false, 106, protocol, 0, conflict) == Result::Conflict && !conflict);
        assert(stale.append(offer(4), clip, false, 104, protocol, 0, conflict) == Result::Conflict && !conflict);
        for (unsigned i = 6; i <= 15; ++i) assert(store.append(offer(i), clip, false, 100 + i, protocol, 0, newest) == Result::Ready);
    }
    {
        Store store(database);
        assert(store.open() == Result::Ready);
        std::array<vmp::VoiceMessageMetadata, 8> rows;
        assert(store.list(rows.data(), rows.size()) == 8 && rows[0].local_id == newest);
        for (size_t i = 0; i < rows.size(); ++i) assert(rows[i].session_id == 15 - i);
        Clip unused;
        assert(!store.media(received, unused));
    }
    sql(database, "UPDATE linux_vmp_voice_snapshot SET checksum=checksum+1");
    const auto damaged_checksum = scalar(database, "SELECT checksum FROM linux_vmp_voice_snapshot");
    {
        Store store(database);
        assert(store.open() == Result::Corrupt);
        uint64_t id = 0;
        assert(store.append(offer(20), clip, false, 120, protocol, 0, id) == Result::Error);
    }
    assert(scalar(database, "SELECT checksum FROM linux_vmp_voice_snapshot") == damaged_checksum);
    const auto future = root / "future.sqlite3";
    {
        Store store(future);
        assert(store.open() == Result::Ready);
    }
    sql(future, "UPDATE linux_vmp_voice_snapshot SET schema=99");
    {
        Store store(future);
        assert(store.open() == Result::Unsupported);
    }
    assert(scalar(future, "SELECT schema FROM linux_vmp_voice_snapshot") == 99);
    const auto missing = root / "missing.sqlite3";
    {
        Store store(missing);
        assert(store.open() == Result::Ready);
    }
    sql(missing, "DELETE FROM linux_vmp_voice_snapshot");
    {
        Store store(missing);
        assert(store.open() == Result::Corrupt);
    }
    assert(scalar(missing, "SELECT COUNT(*) FROM linux_vmp_voice_snapshot") == 0);
    const auto reservations = root / "reservations.sqlite3";
    std::array<uint8_t, 32> first{}, second{};
    first.fill(0x26);
    second.fill(0x27);
    {
        Store store(reservations), other(reservations);
        assert(store.open() == Result::Ready && other.open() == Result::Ready);
        assert(store.reservePrivateControl(first) == Result::Ready);
        assert(other.reservePrivateControl(first) == Result::Duplicate);
        sql(reservations, "CREATE TRIGGER fail_reserve BEFORE INSERT ON linux_vmp_private_reservations BEGIN SELECT RAISE(ABORT,'injected'); END");
        assert(store.reservePrivateControl(second) == Result::Error);
        assert(scalar(reservations, "SELECT count FROM linux_vmp_private_fence") == 1);
        sql(reservations, "DROP TRIGGER fail_reserve");
        assert(store.reservePrivateControl(second) == Result::Ready);
    }
    {
        Store store(reservations);
        assert(store.open() == Result::Ready && store.reservePrivateControl(first) == Result::Duplicate && store.reservePrivateControl(second) == Result::Duplicate);
    }
    sql(reservations, "UPDATE linux_vmp_private_reservations SET key_id=zeroblob(32) WHERE sequence=1");
    {
        Store store(reservations);
        assert(store.open() == Result::Corrupt);
    }
    assert(scalar(reservations, "SELECT count FROM linux_vmp_private_fence") == 2);
    const auto lost = root / "lost-fence.sqlite3";
    {
        Store store(lost);
        assert(store.open() == Result::Ready);
    }
    sql(lost, "DROP TABLE linux_vmp_private_reservations; DROP TABLE linux_vmp_private_fence");
    {
        Store store(lost);
        assert(store.open() == Result::Corrupt);
    }
    assert(scalar(lost, "SELECT COUNT(*) FROM sqlite_schema WHERE name='linux_vmp_private_fence'") == 0);
    const auto legacy = root / "legacy.sqlite3";
    uint64_t legacy_id = 0;
    {
        Store store(legacy);
        assert(store.open() == Result::Ready);
        assert(store.append(offer(42), clip, false, 42, protocol, 0, legacy_id) == Result::Ready);
    }
    legacySnapshot(legacy); // Known attachment-only v1 format, before native RF replies.
    {
        Store store(legacy);
        assert(store.open() == Result::Ready);
        Clip restored;
        assert(store.media(legacy_id, restored) && restored.bytes == clip.bytes && restored.size == clip.size);
        assert(store.reservePrivateControl(first) == Result::Ready);
    }
    assert(scalar(legacy, "SELECT schema FROM linux_vmp_voice_snapshot") == 2);
    std::filesystem::remove_all(root);
}
