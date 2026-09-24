#include "agenda/usecase/agenda_service.h"

#include <limits>

namespace agenda
{

AgendaResult AgendaService::find(uint32_t id, uint16_t& slot, EventRecord& scratch_)
{
    if (id == 0) return AgendaResult::Invalid;
    if (store_.slotCount() > kMaxActiveEvents) return AgendaResult::StorageError;
    for (slot = 0; slot < store_.slotCount(); ++slot)
    {
        if (store_.readSlot(slot, scratch_) != StoreResult::Ok)
            return AgendaResult::StorageError;
        if (scratch_.state == RecordState::Active && scratch_.id == id)
            return AgendaResult::Ok;
    }
    return AgendaResult::NotFound;
}

AgendaResult AgendaService::create(const EventRecord& draft, uint32_t& created_id)
{
    created_id = 0;
    EventRecord scratch_ = draft;
    scratch_.state = RecordState::Active;
    scratch_.id = 1;
    if (!validEvent(scratch_)) return AgendaResult::Invalid;
    const uint16_t count = store_.slotCount();
    if (count == 0 || count > kMaxActiveEvents) return AgendaResult::StorageError;
    uint16_t available = count;
    uint32_t highest_id = 0;
    for (uint16_t slot = 0; slot < count; ++slot)
    {
        if (store_.readSlot(slot, scratch_) != StoreResult::Ok)
            return AgendaResult::StorageError;
        // Tombstones retain IDs, including the highest deleted event. Reusing
        // that slot writes a still higher ID, preserving the high-water mark.
        if (scratch_.id > highest_id) highest_id = scratch_.id;
        if (scratch_.state != RecordState::Active && available == count) available = slot;
    }
    if (available == count) return AgendaResult::Full;
    if (highest_id == std::numeric_limits<uint32_t>::max()) return AgendaResult::IdExhausted;
    scratch_ = draft;
    scratch_.state = RecordState::Active;
    scratch_.id = highest_id + 1;
    if (store_.writeSlot(available, scratch_) != StoreResult::Ok) return AgendaResult::StorageError;
    created_id = scratch_.id;
    return AgendaResult::Ok;
}

AgendaResult AgendaService::update(const EventRecord& event)
{
    if (event.state != RecordState::Active || !validEvent(event)) return AgendaResult::Invalid;
    uint16_t slot = 0;
    EventRecord scratch_;
    const auto result = find(event.id, slot, scratch_);
    if (result != AgendaResult::Ok) return result;
    return store_.writeSlot(slot, event) == StoreResult::Ok ? AgendaResult::Ok : AgendaResult::StorageError;
}

AgendaResult AgendaService::remove(uint32_t id)
{
    uint16_t slot = 0;
    EventRecord scratch_;
    const auto result = find(id, slot, scratch_);
    if (result != AgendaResult::Ok) return result;
    scratch_ = {};
    scratch_.id = id;
    scratch_.state = RecordState::Deleted;
    return store_.writeSlot(slot, scratch_) == StoreResult::Ok ? AgendaResult::Ok : AgendaResult::StorageError;
}

AgendaResult AgendaService::read(uint32_t id, EventRecord& out)
{
    out = {};
    uint16_t slot = 0;
    const auto result = find(id, slot, out);
    if (result != AgendaResult::Ok) out = {};
    return result;
}

} // namespace agenda
