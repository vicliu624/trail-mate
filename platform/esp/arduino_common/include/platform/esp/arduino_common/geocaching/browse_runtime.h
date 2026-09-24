#pragma once
#include <cstddef>
#include <cstdint>
namespace chat
{
class MeshAdapterRouter;
}
class LoraBoard;
namespace platform::esp::arduino_common::geocaching::browse_runtime
{
void configure(chat::MeshAdapterRouter& router, LoraBoard& board);
bool workPending();
// Called only after explicit publication confirmation. Copies exact SignedCache
// bytes; verification and persistence run on the storage maintenance owner.
bool queuePublication(const uint8_t* signed_cache, size_t size);
// Local draft transaction; expected_generation is zero for a new draft.
bool queueDraftSave(const uint8_t draft_id[16], const uint8_t* encoded, size_t size, uint64_t expected_generation);
// Exactly one operation slice, called by the existing storage maintenance owner.
void step();
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
