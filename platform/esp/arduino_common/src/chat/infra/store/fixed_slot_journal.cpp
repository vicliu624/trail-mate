#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <cstring>
#include <limits>

namespace chat::storage::v2
{
namespace
{
namespace storage = ::platform::esp::arduino_common::storage;

bool readExact(storage::SdRuntimeFile& file, void* out, std::size_t len)
{
    return out && len != 0U &&
           file.read(out, len) == static_cast<int>(len);
}

bool writeExact(storage::SdRuntimeFile& file,
                const void* data,
                std::size_t len)
{
    return data && len != 0U && file.write(data, len) == len;
}

} // namespace

FixedSlotJournalEngine::Inspection FixedSlotJournalEngine::inspect(
    const char* path,
    MeshProtocol protocol,
    JournalKind kind,
    std::size_t slot_size) const
{
    Inspection result{};
    if (!path || path[0] == '\0' ||
        !validDescriptor(protocol, kind, slot_size))
    {
        result.state = State::Incompatible;
        return result;
    }
    storage::SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        result.state = State::IoError;
        const bool busy = file.read_busy();
        if (!busy)
        {
            // sd_exists() conflates a bounded bus miss with absence. Only a
            // typed, physically completed path probe can establish Missing.
            uint8_t scratch = 0U;
            const auto evidence = storage::sd_read_file(path, &scratch, sizeof(scratch));
            if (evidence.status == storage::SdFileReadStatus::Missing)
                result.state = State::Missing;
        }
        return result;
    }
    Header header{};
    const uint64_t file_size = file.size();
    if (file.read_busy())
    {
        result.state = State::IoError;
        return result;
    }
    if (file_size < sizeof(header) || !readExact(file, &header, sizeof(header)))
    {
        result.state = file.read_busy() ? State::IoError : State::Incompatible;
        return result;
    }
    if (!matches(header, protocol, kind, slot_size))
    {
        result.state = State::Incompatible;
        return result;
    }

    const uint64_t payload_size = file_size - sizeof(header);
    if (payload_size % slot_size != 0U)
    {
        result.state = State::PartialTail;
        result.slot_count = static_cast<uint32_t>(payload_size / slot_size);
        return result;
    }
    const uint64_t count = payload_size / slot_size;
    if (count > std::numeric_limits<uint32_t>::max())
    {
        result.state = State::Incompatible;
        return result;
    }
    result.state = State::Ready;
    result.slot_count = static_cast<uint32_t>(count);
    return result;
}

bool FixedSlotJournalEngine::create(const char* path,
                                    MeshProtocol protocol,
                                    JournalKind kind,
                                    std::size_t slot_size) const
{
    if (!path || path[0] == '\0' ||
        !validDescriptor(protocol, kind, slot_size))
    {
        return false;
    }
    storage::SdRuntimeFile file;
    if (!file.open(path, "w+"))
    {
        return false;
    }
    const Header header = makeHeader(protocol, kind, slot_size);
    return writeExact(file, &header, sizeof(header)) && file.flush();
}

bool FixedSlotJournalEngine::append(const char* path,
                                    MeshProtocol protocol,
                                    JournalKind kind,
                                    std::size_t slot_size,
                                    const void* slot) const
{
    if (!slot)
    {
        return false;
    }
    Inspection inspection = inspect(path, protocol, kind, slot_size);
    if (inspection.state == State::Missing)
    {
        if (!create(path, protocol, kind, slot_size))
        {
            return false;
        }
        inspection = inspect(path, protocol, kind, slot_size);
    }
    if (inspection.state != State::Ready)
    {
        return false;
    }

    storage::SdRuntimeFile file;
    if (!file.open(path, "r+"))
    {
        return false;
    }
    // size() returns zero on a bounded shared-SPI miss. Never use that
    // ambiguous value as an append offset: it can overwrite the header.
    const uint64_t expected_end = sizeof(Header) +
                                  static_cast<uint64_t>(inspection.slot_count) * slot_size;
    const uint64_t actual_size = file.size();
    if (file.read_busy() || actual_size != expected_end ||
        !file.seek(expected_end))
    {
        return false;
    }
    return writeExact(file, slot, slot_size) && file.flush();
}

bool FixedSlotJournalEngine::read(const char* path,
                                  MeshProtocol protocol,
                                  JournalKind kind,
                                  std::size_t slot_size,
                                  uint32_t slot_index,
                                  void* out_slot) const
{
    const Inspection inspection = inspect(path, protocol, kind, slot_size);
    return readStatus(path,
                      protocol,
                      kind,
                      slot_size,
                      inspection,
                      slot_index,
                      out_slot) == ReadStatus::Ok;
}

FixedSlotJournalEngine::ReadStatus FixedSlotJournalEngine::readStatus(
    const char* path,
    MeshProtocol protocol,
    JournalKind kind,
    std::size_t slot_size,
    const Inspection& inspection,
    uint32_t slot_index,
    void* out_slot) const
{
    if (!out_slot || !validDescriptor(protocol, kind, slot_size))
    {
        return ReadStatus::InvalidArgument;
    }
    if ((inspection.state != State::Ready &&
         inspection.state != State::PartialTail) ||
        slot_index >= inspection.slot_count)
    {
        return ReadStatus::OutOfRange;
    }

    storage::SdRuntimeFile file;
    const uint64_t offset = sizeof(Header) +
                            static_cast<uint64_t>(slot_index) * slot_size;
    if (!file.open(path, "r") || !file.seek(offset) ||
        !readExact(file, out_slot, slot_size))
    {
        // The caller already inspected this journal. A subsequent open/seek/
        // read failure is most commonly a bounded shared-SPI miss, not a
        // corrupt slot. Let the owner retry the journal instead of scanning
        // and logging every remaining slot.
        return ReadStatus::Unavailable;
    }
    return ReadStatus::Ok;
}

bool FixedSlotJournalCursor::begin(const FixedSlotJournalEngine& engine,
                                   const char* path,
                                   MeshProtocol protocol,
                                   JournalKind kind,
                                   std::size_t slot_size)
{
    reset();
    if (!path || path[0] == '\0' || slot_size == 0U ||
        std::strlen(path) >= sizeof(path_))
    {
        return false;
    }

    std::strncpy(path_, path, sizeof(path_) - 1U);
    path_[sizeof(path_) - 1U] = '\0';
    protocol_ = protocol;
    kind_ = kind;
    slot_size_ = slot_size;
    inspection_ = engine.inspect(path_, protocol_, kind_, slot_size_);
    active_ = true;
    return true;
}

FixedSlotJournalCursor::StepStatus FixedSlotJournalCursor::next(
    const FixedSlotJournalEngine& engine,
    void* out_slot,
    std::size_t out_len)
{
    if (!active_ || !out_slot || out_len < slot_size_)
    {
        return StepStatus::Invalid;
    }
    if (inspection_.state == FixedSlotJournalEngine::State::IoError)
    {
        inspection_ = engine.inspect(path_, protocol_, kind_, slot_size_);
        if (inspection_.state == FixedSlotJournalEngine::State::IoError)
        {
            return StepStatus::Unavailable;
        }
    }
    if (inspection_.state ==
        FixedSlotJournalEngine::State::Missing)
    {
        active_ = false;
        return StepStatus::Missing;
    }
    if (inspection_.state != FixedSlotJournalEngine::State::Ready &&
        inspection_.state != FixedSlotJournalEngine::State::PartialTail)
    {
        active_ = false;
        return StepStatus::Invalid;
    }
    if (next_index_ >= inspection_.slot_count)
    {
        active_ = false;
        return StepStatus::Complete;
    }

    const FixedSlotJournalEngine::ReadStatus status =
        engine.readStatus(path_,
                          protocol_,
                          kind_,
                          slot_size_,
                          inspection_,
                          next_index_,
                          out_slot);
    if (status != FixedSlotJournalEngine::ReadStatus::Ok)
    {
        return status == FixedSlotJournalEngine::ReadStatus::Unavailable
                   ? StepStatus::Unavailable
                   : StepStatus::Invalid;
    }

    ++next_index_;
    return StepStatus::Item;
}

bool FixedSlotJournalCursor::seek(uint32_t slot_index)
{
    if (!active_ || slot_index > inspection_.slot_count)
    {
        return false;
    }
    next_index_ = slot_index;
    return true;
}

void FixedSlotJournalCursor::reset()
{
    path_[0] = '\0';
    protocol_ = MeshProtocol::Meshtastic;
    kind_ = JournalKind::MessageSegment;
    slot_size_ = 0U;
    inspection_ = {};
    next_index_ = 0U;
    active_ = false;
}

bool FixedSlotJournalEngine::validDescriptor(MeshProtocol protocol,
                                             JournalKind kind,
                                             std::size_t slot_size)
{
    return supportedProtocol(protocol) &&
           static_cast<uint8_t>(kind) >=
               static_cast<uint8_t>(JournalKind::MessageSegment) &&
           static_cast<uint8_t>(kind) <=
               static_cast<uint8_t>(JournalKind::ContactDelta) &&
           slot_size > 0U &&
           slot_size <= std::numeric_limits<uint16_t>::max();
}

FixedSlotJournalEngine::Header FixedSlotJournalEngine::makeHeader(
    MeshProtocol protocol,
    JournalKind kind,
    std::size_t slot_size)
{
    Header header{};
    header.magic = kMagic;
    header.schema = kStorageSchemaVersion;
    header.protocol = static_cast<uint8_t>(canonicalProtocol(protocol));
    header.kind = static_cast<uint8_t>(kind);
    header.slot_size = static_cast<uint16_t>(slot_size);
    header.crc = crc32(&header, sizeof(header) - sizeof(header.crc));
    return header;
}

bool FixedSlotJournalEngine::matches(const Header& header,
                                     MeshProtocol protocol,
                                     JournalKind kind,
                                     std::size_t slot_size)
{
    return header.magic == kMagic &&
           header.schema == kStorageSchemaVersion &&
           header.protocol ==
               static_cast<uint8_t>(canonicalProtocol(protocol)) &&
           header.kind == static_cast<uint8_t>(kind) &&
           header.slot_size == slot_size &&
           header.crc == crc32(&header, sizeof(header) - sizeof(header.crc));
}

bool replaceFileAtomically(const char* temp_path,
                           const char* final_path,
                           const char* backup_path)
{
    if (!temp_path || !final_path || !backup_path ||
        !storage::sd_exists(temp_path))
    {
        return false;
    }
    if (storage::sd_exists(backup_path) &&
        !storage::sd_remove(backup_path))
    {
        return false;
    }
    const bool had_final = storage::sd_exists(final_path);
    if (had_final && !storage::sd_rename(final_path, backup_path))
    {
        return false;
    }
    if (!storage::sd_rename(temp_path, final_path))
    {
        if (had_final)
        {
            (void)storage::sd_rename(backup_path, final_path);
        }
        return false;
    }
    if (had_final)
    {
        (void)storage::sd_remove(backup_path);
    }
    return true;
}

bool recoverAtomicFile(const char* final_path,
                       const char* temp_path,
                       const char* backup_path)
{
    if (!final_path || !temp_path || !backup_path)
    {
        return false;
    }
    if (storage::sd_exists(final_path))
    {
        if (storage::sd_exists(temp_path))
        {
            (void)storage::sd_remove(temp_path);
        }
        if (storage::sd_exists(backup_path))
        {
            (void)storage::sd_remove(backup_path);
        }
        return true;
    }
    if (storage::sd_exists(backup_path))
    {
        if (!storage::sd_rename(backup_path, final_path))
        {
            return false;
        }
        if (storage::sd_exists(temp_path))
        {
            (void)storage::sd_remove(temp_path);
        }
        return true;
    }
    if (storage::sd_exists(temp_path))
    {
        (void)storage::sd_remove(temp_path);
    }
    return true;
}

} // namespace chat::storage::v2
