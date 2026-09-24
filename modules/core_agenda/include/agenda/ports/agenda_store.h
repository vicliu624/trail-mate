#pragma once

#include "agenda/domain/event.h"

namespace agenda
{

enum class StoreResult : uint8_t
{
    Ok,
    IoError,
    Corrupt,
    UnsupportedVersion,
    InvalidRecord,
    OutOfRange,
};

// Composition supplies a single owner for calls. Implementations must not
// hydrate an in-memory table. Empty/deleted slots are readable records, not I/O
// failures. A successful write means the adapter's durable commit completed.
class IAgendaStore
{
  public:
    virtual ~IAgendaStore() = default;
    virtual StoreResult readSlot(uint16_t slot, EventRecord& out) = 0;
    virtual StoreResult writeSlot(uint16_t slot, const EventRecord& record) = 0;
    virtual StoreResult eraseSlot(uint16_t slot) = 0;
    virtual uint16_t slotCount() const = 0;
};

} // namespace agenda
