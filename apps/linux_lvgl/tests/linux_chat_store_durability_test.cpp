#include "chat/linux_sqlite_chat_store.h"
#include "chat/ports/i_incoming_delivery_commit_port.h"
#include "chat/usecase/chat_service.h"
#include "platform/linux/runtime_paths.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <unistd.h>

namespace
{
struct IncomingPort final : chat::IMeshAdapter, chat::IIncomingDeliveryCommitPort
{
    chat::MeshIncomingText incoming{};
    bool pending = false;
    unsigned accepted = 0, rejected = 0;
    chat::MeshProtocol backendProtocol() const override { return chat::MeshProtocol::Reticulum; }
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText* out) override
    {
        if (!pending) return false;
        *out = incoming;
        pending = false;
        return true;
    }
    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t, chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
    chat::IIncomingDeliveryCommitPort* incomingDeliveryCommitPort() override { return this; }
    void commitIncomingText(const chat::MeshIncomingText&, bool durable) override
    {
        if (durable) ++accepted;
        else ++rejected;
    }
};
void sql(const char* statement)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, statement, nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_close(db) == SQLITE_OK);
}
int count(const char* table)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    const std::string query = std::string("SELECT count(*) FROM ") + table;
    assert(sqlite3_prepare_v2(db, query.c_str(), -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const auto value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    assert(sqlite3_close(db) == SQLITE_OK);
    return value;
}
} // namespace
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-chat-durable-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    std::filesystem::create_directories(platform::linux_runtime::sqlite_database_path().parent_path());
    // Exercise an actual pre-Reticulum database and old unread schema.
    sql("CREATE TABLE chat_messages(sequence INTEGER PRIMARY KEY AUTOINCREMENT,protocol INTEGER NOT NULL,"
        "channel INTEGER NOT NULL,peer INTEGER NOT NULL,from_node INTEGER NOT NULL,msg_id INTEGER NOT NULL,"
        "timestamp INTEGER NOT NULL,text TEXT NOT NULL,team_location_icon INTEGER NOT NULL DEFAULT 0,"
        "has_geo INTEGER NOT NULL DEFAULT 0,geo_lat_e7 INTEGER NOT NULL DEFAULT 0,geo_lon_e7 INTEGER NOT NULL DEFAULT 0,status INTEGER NOT NULL);"
        "CREATE TABLE chat_unread(protocol INTEGER NOT NULL,channel INTEGER NOT NULL,peer INTEGER NOT NULL,unread INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(protocol,channel,peer));");
    trailmate::linux_app::LinuxSqliteChatStore store;
    chat::IChatStore& port = store;
    const auto old_protocol = std::to_string(static_cast<int>(chat::MeshProtocol::Meshtastic));
    sql(("INSERT INTO chat_messages(protocol,channel,peer,from_node,msg_id,timestamp,text,status) VALUES(" + old_protocol +
         ",0,9,9,5,1700000000,'preserved legacy',0); INSERT INTO chat_unread VALUES(" + old_protocol + ",0,9,1);").c_str());
    chat::ChatMessage preserved;
    assert(port.getMessageForProtocol(5, chat::MeshProtocol::Meshtastic, &preserved));
    assert(preserved.text == "preserved legacy" && !preserved.has_reticulum_lxmf_hash && !preserved.source_unverified);
    assert(port.getUnread(chat::conversationIdForMessage(preserved)) == 1);
    port.clearAll();
    chat::ChatMessage message;
    message.protocol = chat::MeshProtocol::Reticulum;
    message.from = message.peer = 11;
    message.msg_id = 7;
    message.timestamp = 1700000000;
    message.text = "hello 中文";
    message.reticulum_identity.valid = true;
    std::memset(message.reticulum_identity.destination_hash, 1, 16);
    std::memset(message.reticulum_identity.identity_hash, 2, 16);
    message.has_reticulum_lxmf_hash = true;
    std::memset(message.reticulum_lxmf_hash, 3, 32);
    message.source_unverified = true;
    message.rx_origin = chat::RxOrigin::LoRa;
    assert(port.appendIncomingDurably(message));
    assert(port.hasReticulumLxmfMessageHash(message.reticulum_lxmf_hash));
    const auto conversation = chat::conversationIdForMessage(message);
    assert(port.getUnread(conversation) == 1);
    assert(port.appendIncomingDurably(message));
    assert(port.getUnread(conversation) == 1 && count("chat_messages") == 1);
    auto second = message;
    second.reticulum_lxmf_hash[31] ^= 1; // Same 32-bit ID and peer, different LXMF identity.
    second.text = "another message";
    assert(port.appendIncomingDurably(second));
    {
        trailmate::linux_app::LinuxSqliteChatStore reopened;
        const auto page = reopened.loadRecent(conversation, 10);
        assert(page.size() == 2 && page[0].msg_id == 7 && page[1].msg_id == 7);
        assert(page[0].has_reticulum_lxmf_hash && page[1].has_reticulum_lxmf_hash);
        assert(std::memcmp(page[0].reticulum_lxmf_hash, page[1].reticulum_lxmf_hash, 32));
        assert(page[0].source_unverified && page[0].rx_origin == chat::RxOrigin::LoRa);
        assert(reopened.hasReticulumLxmfMessageHash(second.reticulum_lxmf_hash));
    }
    const int before = count("chat_messages");
    auto failed = message;
    failed.msg_id = 9;
    failed.reticulum_lxmf_hash[0] ^= 1;
    sql("CREATE TRIGGER fail_unread BEFORE INSERT ON chat_unread BEGIN SELECT RAISE(ABORT,'injected unread failure'); END;");
    assert(!port.appendIncomingDurably(failed));
    assert(count("chat_messages") == before && !port.hasReticulumLxmfMessageHash(failed.reticulum_lxmf_hash));
    sql("DROP TRIGGER fail_unread; CREATE TRIGGER fail_receipt BEFORE INSERT ON chat_reticulum_received_hashes "
        "BEGIN SELECT RAISE(ABORT,'injected receipt failure'); END;");
    assert(!port.appendIncomingDurably(failed));
    assert(count("chat_messages") == before && port.getUnread(conversation) == 2);
    sql("DROP TRIGGER fail_receipt;");
    assert(port.appendIncomingDurably(failed));
    assert(port.getUnread(conversation) == 3);
    auto invalid = message;
    std::memset(invalid.reticulum_lxmf_hash, 0, 32);
    assert(!port.appendIncomingDurably(invalid));
    invalid = message;
    invalid.status = chat::MessageStatus::Queued;
    assert(!port.appendIncomingDurably(invalid));
    port.clearConversation(conversation);
    assert(count("chat_messages") == 0);
    assert(port.hasReticulumLxmfMessageHash(message.reticulum_lxmf_hash));
    assert(port.appendIncomingDurably(message) && count("chat_messages") == 0); // No resurrection after history removal.
    port.clearAll();
    assert(port.hasReticulumLxmfMessageHash(second.reticulum_lxmf_hash));
    chat::ChatMessage legacy;
    legacy.from = legacy.peer = 17;
    legacy.msg_id = 123;
    legacy.text = "legacy";
    assert(port.appendIncomingDurably(legacy) && port.appendIncomingDurably(legacy));
    assert(count("chat_messages") == 1);
    legacy.text = "conflicting ID";
    assert(!port.appendIncomingDurably(legacy));
    sql("CREATE TRIGGER fail_message BEFORE INSERT ON chat_messages BEGIN SELECT RAISE(ABORT,'injected message failure'); END;");
    legacy.status = chat::MessageStatus::Queued;
    assert(!port.appendDurably(legacy));
    sql("DROP TRIGGER fail_message;");
    assert(port.appendDurably(legacy));
    IncomingPort incoming;
    incoming.incoming.from = message.from;
    incoming.incoming.to = 1;
    incoming.incoming.msg_id = 999;
    incoming.incoming.timestamp = message.timestamp;
    incoming.incoming.channel = chat::ChannelId::PRIMARY;
    incoming.incoming.text = "accept only after durable commit";
    incoming.incoming.reticulum_identity = message.reticulum_identity;
    incoming.incoming.has_reticulum_lxmf_hash = true;
    std::memset(incoming.incoming.reticulum_lxmf_hash, 5, 32);
    chat::ChatModel model;
    chat::ChatService service(model, incoming, port, chat::MeshProtocol::Reticulum);
    sql("CREATE TRIGGER reject_delivery BEFORE INSERT ON chat_messages BEGIN SELECT RAISE(ABORT,'injected receive failure'); END;");
    incoming.pending = true;
    service.processIncoming();
    assert(incoming.accepted == 0 && incoming.rejected == 0);
    assert(!port.hasReticulumLxmfMessageHash(incoming.incoming.reticulum_lxmf_hash));
    sql("DROP TRIGGER reject_delivery;");
    service.processIncoming();
    assert(incoming.accepted == 1 && incoming.rejected == 0);
    assert(port.hasReticulumLxmfMessageHash(incoming.incoming.reticulum_lxmf_hash));
    const auto before_duplicate = port.getUnread(conversation);
    incoming.pending = true;
    service.processIncoming();
    assert(incoming.accepted == 2 && port.getUnread(conversation) == before_duplicate);
    std::filesystem::remove_all(root);
}
