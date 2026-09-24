#include "agenda/domain/event_codec.h"
#include "agenda/usecase/agenda_service.h"

#include <cassert>
#include <cstring>
#include <limits>

namespace
{
// The test double emulates persistent bytes. Production stores must not keep
// this table in RAM. A fresh service over these bytes models process reload.
class Store final : public agenda::IAgendaStore
{
  public:
    uint8_t slots[agenda::kMaxActiveEvents][agenda::kSlotBytes]{};
    bool fail_write = false;
    bool fail_read = false;

    Store()
    {
        agenda::EventRecord empty{};
        for (auto& slot : slots) assert(agenda::encodeEvent(empty, slot));
    }
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        if (fail_read) return agenda::StoreResult::IoError;
        if (slot >= slotCount()) return agenda::StoreResult::OutOfRange;
        return agenda::decodeEvent(slots[slot], out) == agenda::DecodeResult::Ok
                   ? agenda::StoreResult::Ok
                   : agenda::StoreResult::Corrupt;
    }
    agenda::StoreResult writeSlot(uint16_t slot, const agenda::EventRecord& record) override
    {
        if (fail_write) return agenda::StoreResult::IoError;
        if (slot >= slotCount()) return agenda::StoreResult::OutOfRange;
        return agenda::encodeEvent(record, slots[slot]) ? agenda::StoreResult::Ok : agenda::StoreResult::InvalidRecord;
    }
    agenda::StoreResult eraseSlot(uint16_t slot) override
    {
        agenda::EventRecord record{};
        auto result = readSlot(slot, record);
        if (result != agenda::StoreResult::Ok) return result;
        record.state = agenda::RecordState::Deleted;
        return writeSlot(slot, record);
    }
    uint16_t slotCount() const override { return agenda::kMaxActiveEvents; }
};
} // namespace

int main()
{
    using namespace agenda;
    Store store;
    AgendaService service(store);
    EventRecord draft{};
    draft.start_time = 1790000000;
    uint32_t id = 999;
    assert(service.create(draft, id) == AgendaResult::Invalid && id == 0);
    std::strcpy(draft.title, "Radio check");
    assert(service.create(draft, id) == AgendaResult::Ok && id == 1);
    assert(draft.id == 0 && draft.state == RecordState::Empty);
    EventRecord loaded;
    assert(service.read(1, loaded) == AgendaResult::Ok);
    loaded.flags = HasReminder;
    loaded.reminder_offset_sec = 600;
    assert(service.update(loaded) == AgendaResult::Ok);
    AgendaService reloaded(store);
    assert(reloaded.read(1, loaded) == AgendaResult::Ok && loaded.reminder_offset_sec == 600);
    assert(reloaded.remove(1) == AgendaResult::Ok);
    assert(reloaded.read(1, loaded) == AgendaResult::NotFound);
    assert(reloaded.create(draft, id) == AgendaResult::Ok && id == 2);
    assert(store.readSlot(0, loaded) == StoreResult::Ok && loaded.id == 2);
    for (uint32_t expected = 3; expected <= 65; ++expected)
        assert(reloaded.create(draft, id) == AgendaResult::Ok && id == expected);
    assert(reloaded.create(draft, id) == AgendaResult::Full && id == 0);
    assert(reloaded.remove(65) == AgendaResult::Ok);
    assert(reloaded.create(draft, id) == AgendaResult::Ok && id == 66);
    assert(reloaded.remove(66) == AgendaResult::Ok);
    store.fail_write = true;
    assert(reloaded.create(draft, id) == AgendaResult::StorageError && id == 0);
    store.fail_write = false;
    assert(reloaded.create(draft, id) == AgendaResult::Ok && id == 67);
    store.fail_read = true;
    assert(reloaded.remove(2) == AgendaResult::StorageError);
    store.fail_read = false;
    store.slots[0][100] ^= 1;
    assert(reloaded.create(draft, id) == AgendaResult::StorageError);
    assert(reloaded.read(2, loaded) == AgendaResult::StorageError);
    store.slots[0][100] ^= 1;
    assert(reloaded.remove(67) == AgendaResult::Ok);
    assert(store.readSlot(63, loaded) == StoreResult::Ok);
    loaded.id = std::numeric_limits<uint32_t>::max();
    assert(store.writeSlot(63, loaded) == StoreResult::Ok);
    assert(reloaded.create(draft, id) == AgendaResult::IdExhausted);
}
