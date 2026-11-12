#include "hostlink/chat_management_codec.h"
#include "hostlink/chat_submission_journal.h"
#include <array>
#include <cassert>
#include <vector>
using namespace hostlink::chat_management;
struct Memory final : JournalIo
{
    std::array<std::vector<uint8_t>, kJournalSlots> slots;
    bool fail_read = false, fail_write = false;
    int reads = 0, writes = 0;
    SlotResult read(uint8_t slot, SubmissionRecord& record) override
    {
        ++reads;
        if (fail_read) return SlotResult::Unavailable;
        if (slots[slot].empty()) return SlotResult::Empty;
        return decodeRecord({slots[slot].data(), slots[slot].size()}, record) ? SlotResult::Found : SlotResult::Unavailable;
    }
    bool write(uint8_t slot, const SubmissionRecord& record) override
    {
        ++writes;
        if (fail_write) return false;
        uint8_t bytes[320];
        size_t size;
        assert(encodeRecord(record, bytes, sizeof(bytes), size));
        slots[slot].assign(bytes, bytes + size);
        return true;
    }
};
SubmissionRecord make(uint8_t op)
{
    SubmissionRecord record;
    record.store[0] = 1;
    record.owner[0] = 2;
    record.operation[0] = op;
    const uint8_t conversation[] = {0x94, 1, 0, 0, 0xc4, 0};
    std::memcpy(record.conversation, conversation, sizeof(conversation));
    record.conversation_size = sizeof(conversation);
    record.text_size = 5;
    return record;
}
LookupResult find(SubmissionJournal& journal, const SubmissionRecord& key, SubmissionRecord& result)
{
    LookupResult found;
    unsigned calls = 0;
    do
    {
        found = journal.lookup(key, result);
        assert(++calls <= kJournalSlots);
    } while (found == LookupResult::Pending);
    return found;
}
int main()
{
    Memory memory;
    SubmissionJournal journal(memory);
    SubmissionRecord result;
    auto record = make(3);
    assert(find(journal, record, result) == LookupResult::Missing);
    assert(memory.reads == kJournalSlots);
    assert(journal.reserve(record) == ReservationResult::Created);
    SubmissionJournal reboot(memory);
    assert(find(reboot, record, result) == LookupResult::Found);
    assert(result.state == SubmissionState::Reserved && result.revision == 1);
    for (uint8_t i = 1; i < kJournalSlots; ++i)
    {
        auto next = make(i + 3);
        assert(find(journal, next, result) == LookupResult::Missing);
        assert(journal.reserve(next) == ReservationResult::Created);
    }
    auto next = make(99);
    assert(find(journal, next, result) == LookupResult::Missing);
    assert(journal.reserve(next) == ReservationResult::Rejected); // Never evict unresolved sends.
    record.state = SubmissionState::Submitted;
    record.revision = 1;
    assert(memory.write(0, record));
    assert(find(journal, next, result) == LookupResult::Missing);
    memory.fail_write = true;
    assert(journal.reserve(next) == ReservationResult::Ambiguous);
    memory.fail_write = false;
    assert(journal.reserve(next) == ReservationResult::Created);
    assert(find(journal, next, result) == LookupResult::Found && result.revision == 17);
    memory.fail_read = true;
    assert(find(journal, record, result) == LookupResult::Unavailable);
    memory.fail_read = false;
    memory.slots[0].push_back(0); // No truncated/corrupt slot becomes verified absence.
    assert(find(journal, next, result) == LookupResult::Unavailable);

    uint8_t frame[512], id_bytes[16] = {1};
    CmpWriter writer(frame, sizeof(frame));
    assert(writer.array(4) && writer.array(2) && writer.binary({id_bytes, 16}) && writer.binary({id_bytes, 16}) &&
           writer.binary({id_bytes, 16}) && writer.array(4) && writer.unsignedInteger(4) && writer.unsignedInteger(0) &&
           writer.unsignedInteger(1) && writer.binary({id_bytes, 16}) && writer.text("hello"));
    SendRequestView view;
    assert(decodeSend({frame, writer.size()}, view));
    assert(view.target.protocol == 4 && view.target.identity.size == 16 && view.text == "hello");
    assert(!decodeSend({frame, writer.size() - 1}, view));
    frame[writer.size()] = 0;
    assert(!decodeSend({frame, writer.size() + 1}, view));
}
