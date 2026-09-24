#pragma once

#include "agenda/domain/event_codec.h"
#include "agenda/ports/agenda_store.h"

#include "platform/esp/common/storage/stdio_record_file_io.h"

namespace platform::esp::storage
{

// Fixed logical slots backed by two CRC-protected physical banks each. Only
// the older bank is overwritten, so a torn record write leaves the other copy.
// Caller supplies file transport and creates the parent directory. Never formats.
// Paths must outlive this single-owner adapter. No SDK or UI ownership here.
class AgendaFileStore final : public agenda::IAgendaStore
{
  public:
    static constexpr std::size_t kHeaderBytes = 256;
    static constexpr std::size_t kFileBytes = kHeaderBytes + agenda::kMaxActiveEvents * 2 * agenda::kSlotBytes;

    AgendaFileStore(const char* path, const char* initialization_path, RecordFileIo& io = stdioRecordFileIo())
        : io_(io), path_(path), initialization_path_(initialization_path) {}
    agenda::StoreResult begin();
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override;
    agenda::StoreResult writeSlot(uint16_t slot, const agenda::EventRecord& record) override;
    agenda::StoreResult eraseSlot(uint16_t slot) override;
    uint16_t slotCount() const override { return agenda::kMaxActiveEvents; }

  private:
    struct Bank
    {
        uint64_t generation = 0;
        agenda::StoreResult result = agenda::StoreResult::Corrupt;
    };
    agenda::StoreResult initialize();
    agenda::StoreResult validateHeader(RecordFileIo::Handle file);
    Bank readBank(RecordFileIo::Handle file, uint16_t slot, uint8_t bank, agenda::EventRecord& out);
    agenda::StoreResult choose(RecordFileIo::Handle file, uint16_t slot, agenda::EventRecord& out,
                               uint8_t& current_bank, uint64_t& generation);
    RecordFileIo& io_;
    const char* path_;
    const char* initialization_path_;
    uint8_t bytes_[agenda::kSlotBytes]{};
    bool ready_ = false;
};

// Keep the embedded budget unchanged. Native 64-bit tests have wider vtable,
// transport and path pointers; they still must not retain a record table.
static_assert(sizeof(AgendaFileStore) <= (sizeof(void*) == 4 ? 288 : 296),
              "Agenda storage adapter exceeded idle RAM budget");
} // namespace platform::esp::storage
