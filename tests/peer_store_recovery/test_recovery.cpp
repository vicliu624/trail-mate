#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace v2 = chat::storage::v2;
namespace contract = platform::esp::common::storage;
namespace fs = std::filesystem;
namespace team_store_test
{
extern bool fail_update_size_once;
extern bool fail_contacts_open_once;
extern bool fail_repair_publish;
extern bool fail_forensic_rename_once;
} // namespace team_store_test

struct ChatStore final : chat::IChatStore
{
    void append(const chat::ChatMessage&) override {}
    std::vector<chat::ChatMessage> loadRecent(const chat::ConversationId&, size_t) override { return {}; }
    std::vector<chat::ConversationMeta> loadConversationPage(size_t, size_t, size_t* total) override
    {
        if (total) *total = 0;
        return {};
    }
    bool setUnread(const chat::ConversationId&, int) override { return true; }
    int getUnread(const chat::ConversationId&) const override { return 0; }
    void clearConversation(const chat::ConversationId&) override {}
    void clearAll() override {}
    bool updateMessageStatus(chat::MessageId, chat::MessageStatus) override { return false; }
    bool getMessage(chat::MessageId, chat::ChatMessage*) const override { return false; }
};

std::vector<uint8_t> bytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string scenario = argv[1];
    char root_template[] = "/tmp/trailmate-peer-recovery-XXXXXX";
    const char* created = mkdtemp(root_template);
    assert(created);
    const fs::path root(created);
    setenv("TRAIL_MATE_TEST_SD_ROOT", created, 1);
    fs::create_directories(root / "data/v2/mt");
    fs::create_directories(root / "team");
    std::ofstream(root / "team/credentials-sentinel") << "original-team-unchanged";
    const auto sentinel = bytes(root / "team/credentials-sentinel");
    constexpr auto protocol = chat::MeshProtocol::Meshtastic;
    v2::FixedSlotJournalEngine journal;
    const char* peers = "/data/v2/mt/peers.snapshot";
    const char* contacts = "/data/v2/mt/contacts.delta";
    assert(journal.create(peers, protocol, v2::JournalKind::PeerSnapshot, v2::peerSlotSize(protocol)));
    std::vector<uint8_t> slot(512);
    for (uint32_t node : {0xF48DA2AEU, 0x4BE5DDEFU, 0xA1B3B57CU})
    {
        v2::PeerProjection peer{};
        peer.record.valid = true;
        peer.record.identity = chat::makeMeshPeerNodeIdentity(protocol, node);
        peer.record.source = chat::MeshPeerSource::RuntimeRx;
        peer.record.first_seen_s = 10;
        peer.record.last_seen_s = 100;
        assert(v2::encodePeerSlot(protocol, peer, slot.data(), v2::peerSlotSize(protocol)));
        assert(journal.append(peers, protocol, v2::JournalKind::PeerSnapshot, v2::peerSlotSize(protocol), slot.data()));
    }
    v2::ContactProjection contact{};
    contact.identity = chat::makeMeshPeerNodeIdentity(protocol, 0x4BE5DDEFU);
    contact.flags.favorite = true;
    std::strcpy(contact.alias, "Before");
    assert(journal.create(contacts, protocol, v2::JournalKind::ContactDelta, v2::contactSlotSize(protocol)));
    assert(v2::encodeContactSlot(protocol, contact, slot.data(), v2::contactSlotSize(protocol)));
    for (unsigned index = 0; index < 146; ++index)
        assert(journal.append(contacts, protocol, v2::JournalKind::ContactDelta, v2::contactSlotSize(protocol), slot.data()));
    const auto original = bytes(root / "data/v2/mt/contacts.delta");
    assert(original.size() == 4250 && v2::contactSlotSize(protocol) == 29);
    std::strcpy(contact.alias, "After");
    assert(v2::encodeContactSlot(protocol, contact, slot.data(), v2::contactSlotSize(protocol)));
    if (scenario == "metadata_busy")
    {
        team_store_test::fail_update_size_once = true;
        const bool appended = journal.append(contacts, protocol, v2::JournalKind::ContactDelta, v2::contactSlotSize(protocol), slot.data());
        const auto after = bytes(root / "data/v2/mt/contacts.delta");
        uint32_t magic = 0;
        std::memcpy(&magic, after.data(), 4);
        std::printf("metadata miss: append=%d preserved=%d magic=%08x\n", appended, after == original, magic);
        if (appended || after != original) return 1;
        assert(journal.append(contacts, protocol, v2::JournalKind::ContactDelta, v2::contactSlotSize(protocol), slot.data()));
    }
    else if (scenario == "overwritten_header" || scenario == "interrupted_publish" || scenario == "forensic_retry" || scenario == "unknown_header")
    {
        std::fstream file(root / "data/v2/mt/contacts.delta", std::ios::in | std::ios::out | std::ios::binary);
        if (scenario == "unknown_header") slot[0] ^= 1U;
        file.write(reinterpret_cast<const char*>(slot.data()), v2::contactSlotSize(protocol));
        file.close();
    }
    else if (scenario == "transient_open")
    {
        team_store_test::fail_contacts_open_once = true;
    }
    const auto damaged = bytes(root / "data/v2/mt/contacts.delta");
    if (scenario == "interrupted_publish") team_store_test::fail_repair_publish = true;
    if (scenario == "forensic_retry") team_store_test::fail_forensic_rename_once = true;
    ChatStore chat;
    auto repository = std::make_unique<chat::SdProtocolPeerRepository>(chat);
    assert(repository->begin().succeeded());
    auto result = repository->beginMaintenance(contract::StorageOperation::Hydrate, 1);
    unsigned steps = 0;
    while (!result.completed() && ++steps < 4096)
    {
        result = result.inProgress() ? repository->stepMaintenance(contract::StorageOperation::Hydrate, 1, {1})
                                     : repository->beginMaintenance(contract::StorageOperation::Hydrate, 1);
        if (scenario == "interrupted_publish" && fs::exists(root / "data/v2/mt/contacts.delta.repair.bak"))
        {
            assert(!fs::exists(root / "data/v2/mt/contacts.delta"));
            assert(bytes(root / "data/v2/mt/contacts.delta.repair.bak") == damaged);
            repository->cancelMaintenance(contract::StorageOperation::Hydrate, 1);
            repository = std::make_unique<chat::SdProtocolPeerRepository>(chat);
            assert(repository->begin().succeeded());
            team_store_test::fail_repair_publish = false;
            result = repository->beginMaintenance(contract::StorageOperation::Hydrate, 1);
        }
    }
    const bool ready = result.completed() && !repository->isHydrating();
    std::printf("actual peer recovery: scenario=%s ready=%d steps=%u result=%u\n", scenario.c_str(), ready, steps, unsigned(result.kind));
    if (scenario == "unknown_header")
    {
        assert(!ready && bytes(root / "data/v2/mt/contacts.delta") == damaged);
        assert(!fs::exists(root / "data/v2/mt/contacts.delta.corrupt"));
        repository->cancelMaintenance(contract::StorageOperation::Hydrate, 1);
    }
    else
    {
        if (!ready)
        {
            repository->cancelMaintenance(contract::StorageOperation::Hydrate, 1);
            return 1;
        }
        chat::MeshPeerRecord loaded{};
        assert(repository->findByNodeId(protocol, 0x4BE5DDEFU, loaded).succeeded());
        const bool repaired = scenario == "overwritten_header" || scenario == "interrupted_publish" || scenario == "forensic_retry";
        const char* wanted = repaired || scenario == "metadata_busy" ? "After" : "Before";
        assert(loaded.flags.favorite && std::strcmp(loaded.user_alias, wanted) == 0);
        // A fresh repository must hydrate the published journal after restart.
        repository = std::make_unique<chat::SdProtocolPeerRepository>(chat);
        assert(repository->begin().succeeded());
        result = repository->beginMaintenance(contract::StorageOperation::Hydrate, 2);
        for (unsigned index = 0; !result.completed() && index < 4096; ++index)
            result = result.inProgress() ? repository->stepMaintenance(contract::StorageOperation::Hydrate, 2, {1})
                                         : repository->beginMaintenance(contract::StorageOperation::Hydrate, 2);
        assert(result.completed());
        assert(repository->findByNodeId(protocol, 0x4BE5DDEFU, loaded).succeeded());
        assert(loaded.flags.favorite && std::strcmp(loaded.user_alias, wanted) == 0);
        if (repaired)
        {
            assert(bytes(root / "data/v2/mt/contacts.delta.corrupt") == damaged);
            assert(!fs::exists(root / "data/v2/mt/contacts.delta.repair.bak"));
            assert(v2::encodeContactSlot(protocol, contact, slot.data(), v2::contactSlotSize(protocol)));
            assert(journal.append(contacts, protocol, v2::JournalKind::ContactDelta, v2::contactSlotSize(protocol), slot.data()));
        }
    }
    assert(bytes(root / "team/credentials-sentinel") == sentinel);
    assert(root.parent_path() == fs::temp_directory_path() && root.filename().string().find("trailmate-peer-recovery-") == 0);
    fs::remove_all(root);
}
