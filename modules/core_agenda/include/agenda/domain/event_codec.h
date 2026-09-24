#pragma once

#include "agenda/domain/event.h"

namespace agenda
{

enum class DecodeResult : uint8_t
{
    Ok,
    UnsupportedVersion,
    Corrupt,
    InvalidRecord,
};

// Exactly one slot of caller-owned scratch storage. No heap allocation.
bool encodeEvent(const EventRecord& record, uint8_t (&out)[kSlotBytes]);
DecodeResult decodeEvent(const uint8_t (&bytes)[kSlotBytes], EventRecord& out);
uint32_t recordCrc32(const uint8_t* bytes, std::size_t size);

} // namespace agenda
