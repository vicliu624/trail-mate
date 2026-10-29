#include "chat/linux_sqlite_chat_store.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#include <sqlite3.h>

#include "platform/linux/runtime_paths.h"

namespace trailmate::linux_app
{
namespace
{

sqlite3* openDatabase()
{
    const std::filesystem::path db_path =
        ::platform::linux_runtime::sqlite_database_path();
    if (!::platform::linux_runtime::ensure_directory(db_path.parent_path()))
    {
        return nullptr;
    }

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(db_path.string().c_str(),
                                   &db,
                                   SQLITE_OPEN_READWRITE |
                                       SQLITE_OPEN_CREATE |
                                       SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK)
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return nullptr;
    }

    sqlite3_busy_timeout(db, 5000);
    return db;
}

bool execSql(sqlite3* db, const char* sql)
{
    if (db == nullptr || sql == nullptr)
    {
        return false;
    }

    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error != nullptr)
    {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

bool tableHasColumn(sqlite3* db, const char* table, const char* column)
{
    if (db == nullptr || table == nullptr || column == nullptr)
    {
        return false;
    }

    std::string sql = "PRAGMA table_info(";
    sql += table;
    sql += ");";

    sqlite3_stmt* stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            if (name != nullptr &&
                std::strcmp(reinterpret_cast<const char*>(name), column) == 0)
            {
                found = true;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool tableExists(sqlite3* db, const char* table)
{
    if (db == nullptr || table == nullptr)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT 1 FROM sqlite_master "
        "WHERE type='table' AND name=?1 LIMIT 1;";
    bool found = false;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_bind_text(stmt, 1, table, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool ensureColumn(sqlite3* db,
                  const char* table,
                  const char* column,
                  const char* definition)
{
    if (tableHasColumn(db, table, column))
    {
        return true;
    }

    std::string sql = "ALTER TABLE ";
    sql += table;
    sql += " ADD COLUMN ";
    sql += definition;
    sql += ";";
    return execSql(db, sql.c_str()) || tableHasColumn(db, table, column);
}

bool ensureUnreadSchema(sqlite3* db)
{
    constexpr const char* kCreateSql =
        "CREATE TABLE IF NOT EXISTS chat_unread ("
        "protocol INTEGER NOT NULL,"
        "channel INTEGER NOT NULL,"
        "peer INTEGER NOT NULL,"
        "reticulum_destination_key TEXT NOT NULL DEFAULT '',"
        "unread INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(protocol, channel, peer, reticulum_destination_key)"
        ");";

    if (!tableExists(db, "chat_unread"))
    {
        return execSql(db, kCreateSql);
    }

    if (tableHasColumn(db, "chat_unread", "reticulum_destination_key"))
    {
        return true;
    }

    if (!execSql(db, "BEGIN IMMEDIATE;"))
    {
        return false;
    }

    const bool migrated =
        execSql(db, "ALTER TABLE chat_unread RENAME TO chat_unread_legacy;") &&
        execSql(db, kCreateSql) &&
        execSql(db,
                "INSERT OR REPLACE INTO chat_unread("
                "protocol, channel, peer, reticulum_destination_key, unread) "
                "SELECT protocol, channel, peer, '', unread "
                "FROM chat_unread_legacy;") &&
        execSql(db,
                "INSERT OR REPLACE INTO chat_unread("
                "protocol, channel, peer, reticulum_destination_key, unread) "
                "SELECT u.protocol, u.channel, 0, "
                "lower(hex(m.reticulum_destination_hash)), SUM(u.unread) "
                "FROM chat_unread_legacy u "
                "JOIN ("
                "SELECT protocol, channel, peer, reticulum_destination_hash "
                "FROM chat_messages "
                "WHERE protocol=4 AND reticulum_identity_valid != 0 AND "
                "reticulum_destination_hash IS NOT NULL "
                "GROUP BY protocol, channel, peer, reticulum_destination_hash"
                ") m ON m.protocol=u.protocol AND m.channel=u.channel "
                "AND m.peer=u.peer "
                "WHERE u.protocol=4 AND u.unread != 0 "
                "GROUP BY u.protocol, u.channel, "
                "lower(hex(m.reticulum_destination_hash));") &&
        execSql(db, "DROP TABLE chat_unread_legacy;");
    (void)execSql(db, migrated ? "COMMIT;" : "ROLLBACK;");
    return migrated;
}

bool dedupeReticulumIncomingMessages(sqlite3* db)
{
    return execSql(db,
                   "DELETE FROM chat_messages "
                   "WHERE status=0 AND msg_id != 0 AND "
                   "reticulum_identity_valid != 0 AND "
                   "reticulum_destination_hash IS NOT NULL AND "
                   "sequence NOT IN ("
                   "SELECT MIN(sequence) FROM chat_messages "
                   "WHERE status=0 AND msg_id != 0 AND "
                   "reticulum_identity_valid != 0 AND "
                   "reticulum_destination_hash IS NOT NULL "
                   "GROUP BY protocol, channel, reticulum_destination_hash, "
                   "msg_id"
                   ");");
}

bool ensureSchema(sqlite3* db)
{
    return execSql(db, "PRAGMA busy_timeout=5000;") &&
           execSql(db, "PRAGMA journal_mode=WAL;") &&
           execSql(db,
                   "CREATE TABLE IF NOT EXISTS chat_messages ("
                   "sequence INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "protocol INTEGER NOT NULL,"
                   "channel INTEGER NOT NULL,"
                   "peer INTEGER NOT NULL,"
                   "from_node INTEGER NOT NULL,"
                   "msg_id INTEGER NOT NULL,"
                   "timestamp INTEGER NOT NULL,"
                   "text TEXT NOT NULL,"
                   "team_location_icon INTEGER NOT NULL DEFAULT 0,"
                   "has_geo INTEGER NOT NULL DEFAULT 0,"
                   "geo_lat_e7 INTEGER NOT NULL DEFAULT 0,"
                   "geo_lon_e7 INTEGER NOT NULL DEFAULT 0,"
                   "status INTEGER NOT NULL,"
                   "reticulum_identity_valid INTEGER NOT NULL DEFAULT 0,"
                   "reticulum_destination_hash BLOB,"
                   "reticulum_identity_hash BLOB"
                   ");") &&
           ensureColumn(db,
                        "chat_messages",
                        "reticulum_identity_valid",
                        "reticulum_identity_valid INTEGER NOT NULL DEFAULT 0") &&
           ensureColumn(db,
                        "chat_messages",
                        "reticulum_destination_hash",
                        "reticulum_destination_hash BLOB") &&
           ensureColumn(db,
                        "chat_messages",
                        "reticulum_identity_hash",
                        "reticulum_identity_hash BLOB") &&
           execSql(db,
                   "CREATE UNIQUE INDEX IF NOT EXISTS "
                   "chat_messages_unique_incoming "
                   "ON chat_messages(protocol, channel, peer, from_node, "
                   "msg_id) "
                   "WHERE status=0 AND msg_id != 0;") &&
           (tableHasColumn(db, "chat_messages", "reticulum_lxmf_hash") || dedupeReticulumIncomingMessages(db)) &&
           execSql(db,
                   "CREATE UNIQUE INDEX IF NOT EXISTS "
                   "chat_messages_unique_reticulum_incoming "
                   "ON chat_messages(protocol, channel, reticulum_destination_hash, "
                   "msg_id) "
                   "WHERE status=0 AND msg_id != 0 AND "
                   "reticulum_identity_valid != 0 AND "
                   "reticulum_destination_hash IS NOT NULL;") &&
           execSql(db,
                   "CREATE INDEX IF NOT EXISTS "
                   "chat_messages_conversation_idx "
                   "ON chat_messages(protocol, channel, peer, sequence);") &&
           execSql(db,
                   "CREATE INDEX IF NOT EXISTS "
                   "chat_messages_reticulum_conversation_idx "
                   "ON chat_messages(protocol, channel, reticulum_destination_hash, "
                   "sequence) "
                   "WHERE reticulum_identity_valid != 0 AND "
                   "reticulum_destination_hash IS NOT NULL;") &&
           execSql(db,
                   "CREATE INDEX IF NOT EXISTS chat_messages_msg_id_idx "
                   "ON chat_messages(msg_id, from_node, sequence);") &&
           ensureUnreadSchema(db);
}

bool ensureDeliverySchema(sqlite3* db)
{
    if (tableHasColumn(db, "chat_messages", "reticulum_lxmf_hash") &&
        tableHasColumn(db, "chat_messages", "source_unverified") &&
        tableHasColumn(db, "chat_messages", "rx_origin") && tableExists(db, "chat_reticulum_received_hashes")) return true;
    if (!execSql(db, "BEGIN IMMEDIATE;")) return false;
    const bool ok =
        ensureColumn(db, "chat_messages", "reticulum_lxmf_hash", "reticulum_lxmf_hash BLOB") &&
        ensureColumn(db, "chat_messages", "source_unverified", "source_unverified INTEGER NOT NULL DEFAULT 0") &&
        ensureColumn(db, "chat_messages", "rx_origin", "rx_origin INTEGER NOT NULL DEFAULT 0") &&
        execSql(db, "DROP INDEX IF EXISTS chat_messages_unique_incoming;") &&
        execSql(db, "DROP INDEX IF EXISTS chat_messages_unique_reticulum_incoming;") &&
        execSql(db, "CREATE UNIQUE INDEX chat_messages_unique_incoming "
                    "ON chat_messages(protocol,channel,peer,from_node,msg_id) "
                    "WHERE status=0 AND msg_id!=0 AND reticulum_lxmf_hash IS NULL;") &&
        execSql(db, "CREATE UNIQUE INDEX chat_messages_unique_reticulum_incoming "
                    "ON chat_messages(protocol,channel,reticulum_destination_hash,msg_id) "
                    "WHERE status=0 AND msg_id!=0 AND reticulum_identity_valid!=0 "
                    "AND reticulum_destination_hash IS NOT NULL AND reticulum_lxmf_hash IS NULL;") &&
        execSql(db, "CREATE UNIQUE INDEX IF NOT EXISTS chat_messages_lxmf_incoming "
                    "ON chat_messages(reticulum_lxmf_hash) WHERE protocol=4 AND status=0 AND reticulum_lxmf_hash IS NOT NULL;") &&
        execSql(db, "CREATE TABLE IF NOT EXISTS chat_reticulum_received_hashes ("
                    "lxmf_hash BLOB PRIMARY KEY CHECK(length(lxmf_hash)=32), received_at INTEGER NOT NULL);");
    if (ok && execSql(db, "COMMIT;")) return true;
    (void)execSql(db, "ROLLBACK;");
    return false;
}

bool receivedHash(sqlite3* db, const uint8_t* hash)
{
    sqlite3_stmt* statement = nullptr;
    const bool found = sqlite3_prepare_v2(db, "SELECT 1 FROM chat_reticulum_received_hashes WHERE lxmf_hash=?1;", -1, &statement, nullptr) == SQLITE_OK &&
                       sqlite3_bind_blob(statement, 1, hash, 32, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

struct DatabaseHandle
{
    sqlite3* db = nullptr;

    DatabaseHandle()
    {
        db = openDatabase();
        if (db != nullptr && (!ensureSchema(db) || !ensureDeliverySchema(db) || !execSql(db, "PRAGMA synchronous=FULL;")))
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    ~DatabaseHandle()
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
    }

    DatabaseHandle(const DatabaseHandle&) = delete;
    DatabaseHandle& operator=(const DatabaseHandle&) = delete;

    explicit operator bool() const noexcept
    {
        return db != nullptr;
    }
};

int protocolValue(::chat::MeshProtocol protocol)
{
    return static_cast<int>(protocol);
}

int channelValue(::chat::ChannelId channel)
{
    return static_cast<int>(channel);
}

int statusValue(::chat::MessageStatus status)
{
    return static_cast<int>(status);
}

::chat::MeshProtocol protocolFromInt(int value)
{
    switch (value)
    {
    case static_cast<int>(::chat::MeshProtocol::MeshCore):
        return ::chat::MeshProtocol::MeshCore;
    case static_cast<int>(::chat::MeshProtocol::RNode):
    case static_cast<int>(::chat::MeshProtocol::Reticulum):
        return ::chat::MeshProtocol::Reticulum;
    case static_cast<int>(::chat::MeshProtocol::Meshtastic):
    default:
        return ::chat::MeshProtocol::Meshtastic;
    }
}

::chat::ChannelId channelFromInt(int value)
{
    switch (value)
    {
    case static_cast<int>(::chat::ChannelId::SECONDARY):
        return ::chat::ChannelId::SECONDARY;
    case static_cast<int>(::chat::ChannelId::PRIMARY):
    default:
        return ::chat::ChannelId::PRIMARY;
    }
}

::chat::MessageStatus statusFromInt(int value)
{
    switch (value)
    {
    case static_cast<int>(::chat::MessageStatus::Queued):
        return ::chat::MessageStatus::Queued;
    case static_cast<int>(::chat::MessageStatus::Sent):
        return ::chat::MessageStatus::Sent;
    case static_cast<int>(::chat::MessageStatus::Failed):
        return ::chat::MessageStatus::Failed;
    case static_cast<int>(::chat::MessageStatus::Delivered):
        return ::chat::MessageStatus::Delivered;
    case static_cast<int>(::chat::MessageStatus::Incoming):
    default:
        return ::chat::MessageStatus::Incoming;
    }
}

bool bindConversation(sqlite3_stmt* stmt,
                      int first_index,
                      const ::chat::ConversationId& conv)
{
    return sqlite3_bind_int(stmt, first_index, protocolValue(conv.protocol)) ==
               SQLITE_OK &&
           sqlite3_bind_int(stmt,
                            first_index + 1,
                            channelValue(conv.channel)) == SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              first_index + 2,
                              static_cast<sqlite3_int64>(conv.peer)) ==
               SQLITE_OK;
}

bool hasReticulumConversationKey(const ::chat::ConversationId& conv)
{
    return conv.protocol == ::chat::MeshProtocol::Reticulum &&
           ::chat::hasReticulumDestinationIdentity(conv.reticulum_identity);
}

std::string reticulumDestinationKey(
    const ::chat::ReticulumPeerIdentity& identity)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::uint8_t destination_hash[::chat::kReticulumPeerHashSize] = {};
    if (!::chat::copyReticulumDestinationHash(destination_hash, identity))
    {
        return {};
    }

    std::string key;
    key.resize(::chat::kReticulumPeerHashSize * 2U);
    for (std::size_t index = 0; index < ::chat::kReticulumPeerHashSize; ++index)
    {
        const std::uint8_t value = destination_hash[index];
        key[index * 2U] = kHex[(value >> 4U) & 0x0FU];
        key[(index * 2U) + 1U] = kHex[value & 0x0FU];
    }
    return key;
}

::chat::NodeId unreadPeerForConversation(const ::chat::ConversationId& conv)
{
    return hasReticulumConversationKey(conv) ? 0U : conv.peer;
}

bool bindReticulumDestinationHash(sqlite3_stmt* stmt,
                                  int index,
                                  const ::chat::ReticulumPeerIdentity& identity)
{
    std::uint8_t destination_hash[::chat::kReticulumPeerHashSize] = {};
    if (!::chat::copyReticulumDestinationHash(destination_hash, identity))
    {
        return false;
    }
    return sqlite3_bind_blob(stmt,
                             index,
                             destination_hash,
                             static_cast<int>(sizeof(destination_hash)),
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bindUnreadConversation(sqlite3_stmt* stmt,
                            int first_index,
                            const ::chat::ConversationId& conv)
{
    const std::string key = hasReticulumConversationKey(conv)
                                ? reticulumDestinationKey(conv.reticulum_identity)
                                : std::string();
    return sqlite3_bind_int(stmt, first_index, protocolValue(conv.protocol)) ==
               SQLITE_OK &&
           sqlite3_bind_int(stmt,
                            first_index + 1,
                            channelValue(conv.channel)) == SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              first_index + 2,
                              static_cast<sqlite3_int64>(
                                  unreadPeerForConversation(conv))) ==
               SQLITE_OK &&
           sqlite3_bind_text(stmt,
                             first_index + 3,
                             key.c_str(),
                             -1,
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bindReticulumIdentity(sqlite3_stmt* stmt,
                           int first_index,
                           const ::chat::ReticulumPeerIdentity& identity)
{
    if (sqlite3_bind_int(stmt, first_index, identity.valid ? 1 : 0) !=
        SQLITE_OK)
    {
        return false;
    }

    if (!identity.valid)
    {
        return sqlite3_bind_null(stmt, first_index + 1) == SQLITE_OK &&
               sqlite3_bind_null(stmt, first_index + 2) == SQLITE_OK;
    }

    std::uint8_t destination_hash[::chat::kReticulumPeerHashSize] = {};
    std::uint8_t identity_hash[::chat::kReticulumPeerHashSize] = {};
    if (!::chat::copyReticulumIdentityHashes(destination_hash,
                                             identity_hash,
                                             identity))
    {
        return false;
    }

    return sqlite3_bind_blob(stmt,
                             first_index + 1,
                             destination_hash,
                             static_cast<int>(sizeof(destination_hash)),
                             SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_blob(stmt,
                             first_index + 2,
                             identity_hash,
                             static_cast<int>(sizeof(identity_hash)),
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

::chat::ReticulumPeerIdentity readReticulumIdentity(sqlite3_stmt* stmt,
                                                    int first_column)
{
    if (sqlite3_column_int(stmt, first_column) == 0)
    {
        return {};
    }

    const void* destination_hash = sqlite3_column_blob(stmt, first_column + 1);
    const void* identity_hash = sqlite3_column_blob(stmt, first_column + 2);
    if (destination_hash == nullptr || identity_hash == nullptr ||
        sqlite3_column_bytes(stmt, first_column + 1) !=
            static_cast<int>(::chat::kReticulumPeerHashSize) ||
        sqlite3_column_bytes(stmt, first_column + 2) !=
            static_cast<int>(::chat::kReticulumPeerHashSize))
    {
        return {};
    }

    return ::chat::makeReticulumPeerIdentity(
        static_cast<const std::uint8_t*>(destination_hash),
        static_cast<const std::uint8_t*>(identity_hash));
}

bool bindMessage(sqlite3_stmt* stmt, const ::chat::ChatMessage& msg)
{
    return sqlite3_bind_int(stmt, 1, protocolValue(msg.protocol)) ==
               SQLITE_OK &&
           sqlite3_bind_int(stmt, 2, channelValue(msg.channel)) == SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              3,
                              static_cast<sqlite3_int64>(msg.peer)) ==
               SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              4,
                              static_cast<sqlite3_int64>(msg.from)) ==
               SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              5,
                              static_cast<sqlite3_int64>(msg.msg_id)) ==
               SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              6,
                              static_cast<sqlite3_int64>(msg.timestamp)) ==
               SQLITE_OK &&
           sqlite3_bind_text(stmt,
                             7,
                             msg.text.c_str(),
                             -1,
                             SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 8, msg.team_location_icon) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 9, msg.has_geo ? 1 : 0) == SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              10,
                              static_cast<sqlite3_int64>(msg.geo_lat_e7)) ==
               SQLITE_OK &&
           sqlite3_bind_int64(stmt,
                              11,
                              static_cast<sqlite3_int64>(msg.geo_lon_e7)) ==
               SQLITE_OK &&
           sqlite3_bind_int(stmt, 12, statusValue(msg.status)) == SQLITE_OK &&
           bindReticulumIdentity(stmt, 13, msg.reticulum_identity) &&
           (::chat::hasReticulumLxmfMessageHash(msg)
                ? sqlite3_bind_blob(stmt, 16, msg.reticulum_lxmf_hash, 32, SQLITE_TRANSIENT)
                : sqlite3_bind_null(stmt, 16)) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 17, msg.source_unverified ? 1 : 0) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 18, static_cast<int>(msg.rx_origin)) == SQLITE_OK;
}

::chat::ChatMessage readMessage(sqlite3_stmt* stmt, int first_column)
{
    ::chat::ChatMessage msg{};
    msg.protocol = protocolFromInt(sqlite3_column_int(stmt, first_column));
    msg.channel = channelFromInt(sqlite3_column_int(stmt, first_column + 1));
    msg.peer = static_cast<::chat::NodeId>(
        sqlite3_column_int64(stmt, first_column + 2));
    msg.from = static_cast<::chat::NodeId>(
        sqlite3_column_int64(stmt, first_column + 3));
    msg.msg_id = static_cast<::chat::MessageId>(
        sqlite3_column_int64(stmt, first_column + 4));
    msg.timestamp = static_cast<std::uint32_t>(
        sqlite3_column_int64(stmt, first_column + 5));
    const unsigned char* text = sqlite3_column_text(stmt, first_column + 6);
    msg.text = text != nullptr
                   ? reinterpret_cast<const char*>(text)
                   : "";
    msg.team_location_icon =
        static_cast<std::uint8_t>(sqlite3_column_int(stmt, first_column + 7));
    msg.has_geo = sqlite3_column_int(stmt, first_column + 8) != 0;
    msg.geo_lat_e7 = static_cast<std::int32_t>(
        sqlite3_column_int64(stmt, first_column + 9));
    msg.geo_lon_e7 = static_cast<std::int32_t>(
        sqlite3_column_int64(stmt, first_column + 10));
    msg.status = statusFromInt(sqlite3_column_int(stmt, first_column + 11));
    msg.reticulum_identity = readReticulumIdentity(stmt, first_column + 12);
    const auto* hash = sqlite3_column_blob(stmt, first_column + 15);
    if (msg.protocol == ::chat::MeshProtocol::Reticulum && hash && sqlite3_column_bytes(stmt, first_column + 15) == 32)
    {
        std::memcpy(msg.reticulum_lxmf_hash, hash, 32);
        msg.has_reticulum_lxmf_hash = true;
    }
    msg.source_unverified = sqlite3_column_int(stmt, first_column + 16) != 0;
    msg.rx_origin = static_cast<::chat::RxOrigin>(sqlite3_column_int(stmt, first_column + 17));
    return msg;
}

std::string conversationName(const ::chat::ConversationId& conv)
{
    if (conv.peer == 0)
    {
        return "Broadcast";
    }

    char buffer[16] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04lX",
                  static_cast<unsigned long>(conv.peer & 0xFFFFU));
    return buffer;
}

} // namespace

LinuxSqliteChatStore::LinuxSqliteChatStore() = default;

LinuxSqliteChatStore::~LinuxSqliteChatStore() = default;

void LinuxSqliteChatStore::append(const ::chat::ChatMessage& msg)
{
    (void)appendDurably(msg);
}

bool LinuxSqliteChatStore::appendIncomingDurably(const ::chat::ChatMessage& msg)
{
    return msg.status == ::chat::MessageStatus::Incoming && appendDurably(msg);
}

bool LinuxSqliteChatStore::hasReticulumLxmfMessageHash(const uint8_t* hash) const
{
    if (!hash || ::chat::isAllZeroKeyBytes(hash, 32)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    return handle && receivedHash(handle.db, hash);
}

bool LinuxSqliteChatStore::appendDurably(const ::chat::ChatMessage& msg)
{
    const bool hashed = ::chat::hasReticulumLxmfMessageHash(msg);
    if (msg.has_reticulum_lxmf_hash && (!hashed || !::chat::hasReticulumDestinationIdentity(msg.reticulum_identity))) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle || !execSql(handle.db, "BEGIN IMMEDIATE;")) return false;
    const bool incoming = msg.status == ::chat::MessageStatus::Incoming;
    if (hashed && incoming && receivedHash(handle.db, msg.reticulum_lxmf_hash))
    {
        const bool durable = execSql(handle.db, "COMMIT;");
        if (!durable) (void)execSql(handle.db, "ROLLBACK;");
        return durable;
    }
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "INSERT OR IGNORE INTO chat_messages("
        "protocol, channel, peer, from_node, msg_id, timestamp, text, "
        "team_location_icon, has_geo, geo_lat_e7, geo_lon_e7, status, "
        "reticulum_identity_valid, reticulum_destination_hash, reticulum_identity_hash, "
        "reticulum_lxmf_hash, source_unverified, rx_origin) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18);";
    bool ok = sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK &&
              bindMessage(stmt, msg) && sqlite3_step(stmt) == SQLITE_DONE;
    const bool inserted = ok && sqlite3_changes(handle.db) > 0;
    sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ok && !inserted)
    {
        // OR IGNORE must not turn a conflicting 32-bit ID or an unrelated
        // constraint failure into a claim that this message was persisted.
        const bool destination_key = msg.protocol == ::chat::MeshProtocol::Reticulum &&
                                     ::chat::hasReticulumDestinationIdentity(msg.reticulum_identity);
        constexpr const char* kExisting =
            "SELECT 1 FROM chat_messages WHERE protocol=?1 AND channel=?2 AND from_node=?3 "
            "AND msg_id=?4 AND text=?5 AND status=?6 AND "
            "((?7=1 AND reticulum_identity_valid!=0 AND reticulum_destination_hash=?8) OR (?7=0 AND peer=?9)) "
            "AND ((?10 IS NULL AND reticulum_lxmf_hash IS NULL) OR reticulum_lxmf_hash=?10);";
        ok = sqlite3_prepare_v2(handle.db, kExisting, -1, &stmt, nullptr) == SQLITE_OK &&
             sqlite3_bind_int(stmt, 1, protocolValue(msg.protocol)) == SQLITE_OK &&
             sqlite3_bind_int(stmt, 2, channelValue(msg.channel)) == SQLITE_OK &&
             sqlite3_bind_int64(stmt, 3, msg.from) == SQLITE_OK &&
             sqlite3_bind_int64(stmt, 4, msg.msg_id) == SQLITE_OK &&
             sqlite3_bind_text(stmt, 5, msg.text.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
             sqlite3_bind_int(stmt, 6, statusValue(msg.status)) == SQLITE_OK &&
             sqlite3_bind_int(stmt, 7, destination_key ? 1 : 0) == SQLITE_OK &&
             sqlite3_bind_blob(stmt, 8, msg.reticulum_identity.destination_hash, 16, SQLITE_TRANSIENT) == SQLITE_OK &&
             sqlite3_bind_int64(stmt, 9, msg.peer) == SQLITE_OK &&
             (hashed ? sqlite3_bind_blob(stmt, 10, msg.reticulum_lxmf_hash, 32, SQLITE_TRANSIENT) : sqlite3_bind_null(stmt, 10)) == SQLITE_OK &&
             sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
    if (ok && inserted && incoming)
    {
        const auto conv = ::chat::conversationIdForMessage(msg);
        constexpr const char* kUnread =
            "INSERT INTO chat_unread(protocol,channel,peer,reticulum_destination_key,unread) "
            "VALUES(?1,?2,?3,?4,1) ON CONFLICT(protocol,channel,peer,reticulum_destination_key) "
            "DO UPDATE SET unread=chat_unread.unread+1;";
        ok = sqlite3_prepare_v2(handle.db, kUnread, -1, &stmt, nullptr) == SQLITE_OK &&
             bindUnreadConversation(stmt, 1, conv) && sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
    if (ok && hashed && incoming)
    {
        ok = sqlite3_prepare_v2(handle.db, "INSERT OR IGNORE INTO chat_reticulum_received_hashes(lxmf_hash,received_at) VALUES(?1,?2);",
                                -1, &stmt, nullptr) == SQLITE_OK &&
             sqlite3_bind_blob(stmt, 1, msg.reticulum_lxmf_hash, 32, SQLITE_TRANSIENT) == SQLITE_OK &&
             sqlite3_bind_int64(stmt, 2, msg.timestamp) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
    }
    if (ok && execSql(handle.db, "COMMIT;")) return true;
    (void)execSql(handle.db, "ROLLBACK;");
    return false;
}

std::vector<::chat::ChatMessage> LinuxSqliteChatStore::loadRecent(
    const ::chat::ConversationId& conv,
    std::size_t n)
{
    return loadPageFromLatest(conv, 0, n, nullptr);
}

std::vector<::chat::ChatMessage> LinuxSqliteChatStore::loadPageFromLatest(
    const ::chat::ConversationId& conv,
    std::size_t offset_from_latest,
    std::size_t limit,
    std::size_t* total)
{
    if (total != nullptr)
    {
        *total = 0;
    }
    if (limit == 0U)
    {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return {};
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kLegacyCountSql =
        "SELECT COUNT(*) FROM chat_messages "
        "WHERE protocol=?1 AND channel=?2 AND peer=?3;";
    constexpr const char* kReticulumCountSql =
        "SELECT COUNT(*) FROM chat_messages "
        "WHERE protocol=?1 AND channel=?2 AND "
        "reticulum_identity_valid != 0 AND reticulum_destination_hash=?3;";
    constexpr const char* kLegacySql =
        "SELECT protocol, channel, peer, from_node, msg_id, timestamp, text, "
        "team_location_icon, has_geo, geo_lat_e7, geo_lon_e7, status, "
        "reticulum_identity_valid, reticulum_destination_hash, "
        "reticulum_identity_hash, reticulum_lxmf_hash, source_unverified, rx_origin "
        "FROM chat_messages "
        "WHERE protocol=?1 AND channel=?2 AND peer=?3 "
        "ORDER BY sequence DESC LIMIT ?4 OFFSET ?5;";
    constexpr const char* kReticulumSql =
        "SELECT protocol, channel, peer, from_node, msg_id, timestamp, text, "
        "team_location_icon, has_geo, geo_lat_e7, geo_lon_e7, status, "
        "reticulum_identity_valid, reticulum_destination_hash, "
        "reticulum_identity_hash, reticulum_lxmf_hash, source_unverified, rx_origin "
        "FROM chat_messages "
        "WHERE protocol=?1 AND channel=?2 AND "
        "reticulum_identity_valid != 0 AND reticulum_destination_hash=?3 "
        "ORDER BY sequence DESC LIMIT ?4 OFFSET ?5;";
    const bool use_reticulum_key = hasReticulumConversationKey(conv);

    if (total != nullptr)
    {
        const char* count_sql =
            use_reticulum_key ? kReticulumCountSql : kLegacyCountSql;
        if (sqlite3_prepare_v2(handle.db,
                               count_sql,
                               -1,
                               &stmt,
                               nullptr) == SQLITE_OK)
        {
            const bool bound =
                use_reticulum_key
                    ? (sqlite3_bind_int(stmt, 1, protocolValue(conv.protocol)) ==
                           SQLITE_OK &&
                       sqlite3_bind_int(stmt, 2, channelValue(conv.channel)) ==
                           SQLITE_OK &&
                       bindReticulumDestinationHash(
                           stmt, 3, conv.reticulum_identity))
                    : bindConversation(stmt, 1, conv);
            if (bound && sqlite3_step(stmt) == SQLITE_ROW)
            {
                *total = static_cast<std::size_t>(
                    std::max<sqlite3_int64>(
                        0, sqlite3_column_int64(stmt, 0)));
            }
        }
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    const char* sql = use_reticulum_key ? kReticulumSql : kLegacySql;
    if (sqlite3_prepare_v2(handle.db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return {};
    }

    std::vector<::chat::ChatMessage> result;
    bool bound = false;
    if (use_reticulum_key)
    {
        bound = sqlite3_bind_int(stmt, 1, protocolValue(conv.protocol)) ==
                    SQLITE_OK &&
                sqlite3_bind_int(stmt, 2, channelValue(conv.channel)) ==
                    SQLITE_OK &&
                bindReticulumDestinationHash(stmt, 3, conv.reticulum_identity) &&
                sqlite3_bind_int64(stmt,
                                   4,
                                   static_cast<sqlite3_int64>(limit)) ==
                    SQLITE_OK &&
                sqlite3_bind_int64(
                    stmt,
                    5,
                    static_cast<sqlite3_int64>(offset_from_latest)) ==
                    SQLITE_OK;
    }
    else
    {
        bound = bindConversation(stmt, 1, conv) &&
                sqlite3_bind_int64(stmt,
                                   4,
                                   static_cast<sqlite3_int64>(limit)) ==
                    SQLITE_OK &&
                sqlite3_bind_int64(
                    stmt,
                    5,
                    static_cast<sqlite3_int64>(offset_from_latest)) ==
                    SQLITE_OK;
    }
    if (bound)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            result.push_back(readMessage(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<::chat::ConversationMeta> LinuxSqliteChatStore::loadConversationPage(
    std::size_t offset,
    std::size_t limit,
    std::size_t* total)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        if (total != nullptr)
        {
            *total = 0;
        }
        return {};
    }

    if (total != nullptr)
    {
        sqlite3_stmt* count_stmt = nullptr;
        constexpr const char* kCountSql =
            "SELECT COUNT(*) FROM ("
            "SELECT 1 FROM chat_messages "
            "GROUP BY protocol, channel, "
            "CASE WHEN protocol=4 AND reticulum_identity_valid != 0 AND "
            "reticulum_destination_hash IS NOT NULL THEN 0 ELSE peer END, "
            "CASE WHEN protocol=4 AND reticulum_identity_valid != 0 AND "
            "reticulum_destination_hash IS NOT NULL "
            "THEN hex(reticulum_destination_hash) ELSE '' END"
            ");";
        *total = 0;
        if (sqlite3_prepare_v2(handle.db,
                               kCountSql,
                               -1,
                               &count_stmt,
                               nullptr) == SQLITE_OK &&
            sqlite3_step(count_stmt) == SQLITE_ROW)
        {
            *total = static_cast<std::size_t>(
                std::max<sqlite3_int64>(0, sqlite3_column_int64(count_stmt, 0)));
        }
        sqlite3_finalize(count_stmt);
    }

    std::string sql =
        "WITH keyed AS ("
        "SELECT sequence, protocol, channel, peer, "
        "CASE WHEN protocol=4 AND reticulum_identity_valid != 0 AND "
        "reticulum_destination_hash IS NOT NULL THEN 0 ELSE peer END "
        "AS group_peer, "
        "CASE WHEN protocol=4 AND reticulum_identity_valid != 0 AND "
        "reticulum_destination_hash IS NOT NULL "
        "THEN hex(reticulum_destination_hash) ELSE '' END "
        "AS reticulum_destination_key "
        "FROM chat_messages"
        "), "
        "latest AS ("
        "SELECT protocol, channel, group_peer, reticulum_destination_key, "
        "MAX(sequence) AS sequence "
        "FROM keyed "
        "GROUP BY protocol, channel, group_peer, reticulum_destination_key"
        ") "
        "SELECT m.protocol, m.channel, m.peer, m.text, m.timestamp, "
        "COALESCE(u.unread, 0), m.sequence, "
        "m.reticulum_identity_valid, m.reticulum_destination_hash, "
        "m.reticulum_identity_hash "
        "FROM latest l "
        "JOIN chat_messages m ON m.sequence=l.sequence "
        "LEFT JOIN chat_unread u ON u.protocol=l.protocol "
        "AND u.channel=l.channel AND u.peer=l.group_peer "
        "AND u.reticulum_destination_key=lower(l.reticulum_destination_key) "
        "ORDER BY m.timestamp DESC, m.sequence DESC";
    if (limit != 0U)
    {
        sql += " LIMIT ?1 OFFSET ?2";
    }
    else if (offset != 0U)
    {
        sql += " LIMIT -1 OFFSET ?1";
    }
    sql += ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle.db, sql.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK)
    {
        return {};
    }

    bool ok = true;
    if (limit != 0U)
    {
        ok = sqlite3_bind_int64(stmt,
                                1,
                                static_cast<sqlite3_int64>(limit)) ==
                 SQLITE_OK &&
             sqlite3_bind_int64(stmt,
                                2,
                                static_cast<sqlite3_int64>(offset)) ==
                 SQLITE_OK;
    }
    else if (offset != 0U)
    {
        ok = sqlite3_bind_int64(stmt,
                                1,
                                static_cast<sqlite3_int64>(offset)) ==
             SQLITE_OK;
    }

    std::vector<::chat::ConversationMeta> list;
    if (ok)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ::chat::ConversationMeta meta{};
            meta.id.protocol = protocolFromInt(sqlite3_column_int(stmt, 0));
            meta.id.channel = channelFromInt(sqlite3_column_int(stmt, 1));
            meta.id.peer = static_cast<::chat::NodeId>(
                sqlite3_column_int64(stmt, 2));
            const unsigned char* preview = sqlite3_column_text(stmt, 3);
            meta.preview = preview != nullptr
                               ? reinterpret_cast<const char*>(preview)
                               : "";
            meta.last_timestamp = static_cast<std::uint32_t>(
                sqlite3_column_int64(stmt, 4));
            meta.unread = sqlite3_column_int(stmt, 5);
            meta.reticulum_identity = readReticulumIdentity(stmt, 7);
            meta.id.reticulum_identity = meta.reticulum_identity;
            meta.name = conversationName(meta.id);
            list.push_back(std::move(meta));
        }
    }
    sqlite3_finalize(stmt);
    return list;
}

bool LinuxSqliteChatStore::setUnread(const ::chat::ConversationId& conv,
                                     int unread)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "INSERT INTO chat_unread("
        "protocol, channel, peer, reticulum_destination_key, unread) "
        "VALUES(?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(protocol, channel, peer, reticulum_destination_key) "
        "DO UPDATE SET "
        "unread=excluded.unread;";
    bool ok = false;
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        ok = bindUnreadConversation(stmt, 1, conv) &&
             sqlite3_bind_int(stmt, 5, std::max(0, unread)) == SQLITE_OK &&
             sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    return ok;
}

int LinuxSqliteChatStore::getUnread(
    const ::chat::ConversationId& conv) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT unread FROM chat_unread "
        "WHERE protocol=?1 AND channel=?2 AND peer=?3 "
        "AND reticulum_destination_key=?4;";
    int unread = 0;
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK &&
        bindUnreadConversation(stmt, 1, conv) &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        unread = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return std::max(0, unread);
}

void LinuxSqliteChatStore::clearConversation(
    const ::chat::ConversationId& conv)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return;
    }

    (void)execSql(handle.db, "BEGIN IMMEDIATE;");
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kDeleteLegacyMessages =
        "DELETE FROM chat_messages "
        "WHERE protocol=?1 AND channel=?2 AND peer=?3;";
    constexpr const char* kDeleteReticulumMessages =
        "DELETE FROM chat_messages "
        "WHERE protocol=?1 AND channel=?2 AND "
        "reticulum_identity_valid != 0 AND reticulum_destination_hash=?3;";
    const bool use_reticulum_key = hasReticulumConversationKey(conv);
    const char* delete_messages =
        use_reticulum_key ? kDeleteReticulumMessages : kDeleteLegacyMessages;
    if (sqlite3_prepare_v2(handle.db,
                           delete_messages,
                           -1,
                           &stmt,
                           nullptr) == SQLITE_OK)
    {
        bool bound = false;
        if (use_reticulum_key)
        {
            bound = sqlite3_bind_int(stmt, 1, protocolValue(conv.protocol)) ==
                        SQLITE_OK &&
                    sqlite3_bind_int(stmt, 2, channelValue(conv.channel)) ==
                        SQLITE_OK &&
                    bindReticulumDestinationHash(stmt,
                                                 3,
                                                 conv.reticulum_identity);
        }
        else
        {
            bound = bindConversation(stmt, 1, conv);
        }
        (void)(bound && sqlite3_step(stmt) == SQLITE_DONE);
    }
    sqlite3_finalize(stmt);

    constexpr const char* kDeleteUnread =
        "DELETE FROM chat_unread "
        "WHERE protocol=?1 AND channel=?2 AND peer=?3 "
        "AND reticulum_destination_key=?4;";
    if (sqlite3_prepare_v2(handle.db,
                           kDeleteUnread,
                           -1,
                           &stmt,
                           nullptr) == SQLITE_OK)
    {
        (void)(bindUnreadConversation(stmt, 1, conv) &&
               sqlite3_step(stmt) == SQLITE_DONE);
    }
    sqlite3_finalize(stmt);
    (void)execSql(handle.db, "COMMIT;");
}

void LinuxSqliteChatStore::clearAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return;
    }

    (void)execSql(handle.db, "BEGIN IMMEDIATE;");
    (void)execSql(handle.db, "DELETE FROM chat_messages;");
    (void)execSql(handle.db, "DELETE FROM chat_unread;");
    (void)execSql(handle.db,
                  "DELETE FROM sqlite_sequence WHERE name='chat_messages';");
    (void)execSql(handle.db, "COMMIT;");
}

bool LinuxSqliteChatStore::updateMessageStatus(
    ::chat::MessageId msg_id,
    ::chat::MessageStatus status)
{
    if (msg_id == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "UPDATE chat_messages SET status=?1 "
        "WHERE sequence=("
        "SELECT sequence FROM chat_messages "
        "WHERE msg_id=?2 AND from_node=0 "
        "ORDER BY sequence DESC LIMIT 1"
        ");";
    bool ok = false;
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        ok = sqlite3_bind_int(stmt, 1, statusValue(status)) == SQLITE_OK &&
             sqlite3_bind_int64(stmt,
                                2,
                                static_cast<sqlite3_int64>(msg_id)) ==
                 SQLITE_OK &&
             sqlite3_step(stmt) == SQLITE_DONE &&
             sqlite3_changes(handle.db) > 0;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool LinuxSqliteChatStore::updateMessageStatusForProtocol(
    ::chat::MessageId msg_id,
    ::chat::MeshProtocol protocol,
    ::chat::MessageStatus status)
{
    if (msg_id == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "UPDATE chat_messages SET status=?1 "
        "WHERE sequence=("
        "SELECT sequence FROM chat_messages "
        "WHERE msg_id=?2 AND protocol=?3 AND from_node=0 "
        "ORDER BY sequence DESC LIMIT 1"
        ");";
    bool ok = false;
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        ok = sqlite3_bind_int(stmt, 1, statusValue(status)) == SQLITE_OK &&
             sqlite3_bind_int64(stmt,
                                2,
                                static_cast<sqlite3_int64>(msg_id)) ==
                 SQLITE_OK &&
             sqlite3_bind_int(stmt, 3, protocolValue(protocol)) == SQLITE_OK &&
             sqlite3_step(stmt) == SQLITE_DONE &&
             sqlite3_changes(handle.db) > 0;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool LinuxSqliteChatStore::getMessage(::chat::MessageId msg_id,
                                      ::chat::ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT protocol, channel, peer, from_node, msg_id, timestamp, text, "
        "team_location_icon, has_geo, geo_lat_e7, geo_lon_e7, status, "
        "reticulum_identity_valid, reticulum_destination_hash, "
        "reticulum_identity_hash, reticulum_lxmf_hash, source_unverified, rx_origin "
        "FROM chat_messages "
        "WHERE msg_id=?1 ORDER BY sequence DESC LIMIT 1;";
    bool found = false;
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(msg_id)) ==
            SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (out != nullptr)
        {
            *out = readMessage(stmt, 0);
        }
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool LinuxSqliteChatStore::getMessageForProtocol(
    ::chat::MessageId msg_id,
    ::chat::MeshProtocol protocol,
    ::chat::ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    DatabaseHandle handle;
    if (!handle)
    {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT protocol, channel, peer, from_node, msg_id, timestamp, text, "
        "team_location_icon, has_geo, geo_lat_e7, geo_lon_e7, status, "
        "reticulum_identity_valid, reticulum_destination_hash, "
        "reticulum_identity_hash, reticulum_lxmf_hash, source_unverified, rx_origin "
        "FROM chat_messages "
        "WHERE msg_id=?1 AND protocol=?2 "
        "ORDER BY sequence DESC LIMIT 1;";
    bool found = false;
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(msg_id)) ==
            SQLITE_OK &&
        sqlite3_bind_int(stmt, 2, protocolValue(protocol)) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (out != nullptr)
        {
            *out = readMessage(stmt, 0);
        }
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

void LinuxSqliteChatStore::flush()
{
}

} // namespace trailmate::linux_app
