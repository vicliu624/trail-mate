#include "platform/esp/common/storage/agenda_file_store.h"

#include <cstring>
#include <limits>

namespace platform::esp::storage
{
namespace
{
using agenda::StoreResult;
constexpr std::size_t kGenerationOffset = 208;
constexpr std::size_t kSlotIndexOffset = 216;
constexpr std::size_t kCrcOffset = agenda::kSlotBytes - 4;

void put(uint8_t* out, uint64_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}
uint64_t get(const uint8_t* in, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= static_cast<uint64_t>(in[i]) << (8 * i);
    return value;
}
long offset(uint16_t slot, uint8_t bank)
{
    return static_cast<long>(AgendaFileStore::kHeaderBytes + (slot * 2 + bank) * agenda::kSlotBytes);
}
StoreResult decodedResult(agenda::DecodeResult result)
{
    switch (result)
    {
    case agenda::DecodeResult::Ok:
        return StoreResult::Ok;
    case agenda::DecodeResult::UnsupportedVersion:
        return StoreResult::UnsupportedVersion;
    case agenda::DecodeResult::InvalidRecord:
        return StoreResult::InvalidRecord;
    default:
        return StoreResult::Corrupt;
    }
}
} // namespace

agenda::StoreResult AgendaFileStore::validateHeader(RecordFileIo::Handle file)
{
    uint64_t bytes = 0;
    if (!io_.size(file, bytes)) return StoreResult::IoError;
    if (bytes != kFileBytes) return StoreResult::Corrupt;
    if (!io_.seek(file, 0) || io_.read(file, bytes_, sizeof(bytes_)) != sizeof(bytes_))
        return StoreResult::IoError;
    if (std::memcmp(bytes_, "TMAG", 4) != 0 ||
        get(bytes_ + kCrcOffset, 4) != agenda::recordCrc32(bytes_, kCrcOffset))
        return StoreResult::Corrupt;
    if (bytes_[4] != 1) return StoreResult::UnsupportedVersion;
    if (get(bytes_ + 8, 2) != agenda::kMaxActiveEvents || get(bytes_ + 10, 2) != agenda::kSlotBytes || bytes_[12] != 2)
        return StoreResult::UnsupportedVersion;
    return StoreResult::Ok;
}

agenda::StoreResult AgendaFileStore::initialize()
{
    RecordFileIo::Handle file = io_.open(initialization_path_, RecordFileIo::Mode::CreateTemporary).handle;
    if (!file) return StoreResult::IoError;
    std::memset(bytes_, 0, sizeof(bytes_));
    std::memcpy(bytes_, "TMAG", 4);
    bytes_[4] = 1;
    put(bytes_ + 8, agenda::kMaxActiveEvents, 2);
    put(bytes_ + 10, agenda::kSlotBytes, 2);
    bytes_[12] = 2;
    put(bytes_ + kCrcOffset, agenda::recordCrc32(bytes_, kCrcOffset), 4);
    bool ok = io_.write(file, bytes_, sizeof(bytes_)) == sizeof(bytes_);
    // Initialization alone needs an empty record. It is not an idle cache.
    const agenda::EventRecord empty{};
    for (uint16_t slot = 0; ok && slot < agenda::kMaxActiveEvents; ++slot)
    {
        ok = agenda::encodeEvent(empty, bytes_);
        put(bytes_ + kSlotIndexOffset, slot, 2);
        put(bytes_ + kCrcOffset, agenda::recordCrc32(bytes_, kCrcOffset), 4);
        for (unsigned bank = 0; ok && bank < 2; ++bank)
            ok = io_.write(file, bytes_, sizeof(bytes_)) == sizeof(bytes_);
    }
    if (ok) ok = io_.sync(file);
    if (!io_.close(file)) ok = false;
    if (!ok) return StoreResult::IoError;
    // No existing database is replaced. begin() enters here only on ENOENT.
    return io_.publish(initialization_path_, path_) ? StoreResult::Ok : StoreResult::IoError;
}

agenda::StoreResult AgendaFileStore::begin()
{
    ready_ = false;
    if (!path_ || !initialization_path_ || std::strcmp(path_, initialization_path_) == 0) return StoreResult::InvalidRecord;
    const auto opened = io_.open(path_, RecordFileIo::Mode::Read);
    auto* file = opened.handle;
    if (!file)
    {
        if (opened.status != RecordFileIo::OpenStatus::Missing) return StoreResult::IoError;
        const auto result = initialize();
        if (result != StoreResult::Ok) return result;
        file = io_.open(path_, RecordFileIo::Mode::Read).handle;
        if (!file) return StoreResult::IoError;
    }
    const auto result = validateHeader(file);
    const bool closed = io_.close(file);
    ready_ = result == StoreResult::Ok && closed;
    return closed ? result : StoreResult::IoError;
}

AgendaFileStore::Bank AgendaFileStore::readBank(RecordFileIo::Handle file, uint16_t slot, uint8_t bank, agenda::EventRecord& out)
{
    out = {};
    Bank value;
    if (!io_.seek(file, offset(slot, bank)) ||
        io_.read(file, bytes_, sizeof(bytes_)) != sizeof(bytes_))
    {
        value.result = StoreResult::IoError;
        return value;
    }
    value.result = decodedResult(agenda::decodeEvent(bytes_, out));
    if (value.result != StoreResult::Ok) return value;
    if (get(bytes_ + kSlotIndexOffset, 2) != slot)
    {
        out = {};
        value.result = StoreResult::Corrupt;
        return value;
    }
    value.generation = get(bytes_ + kGenerationOffset, 8);
    return value;
}

agenda::StoreResult AgendaFileStore::choose(RecordFileIo::Handle file, uint16_t slot, agenda::EventRecord& out,
                                            uint8_t& current_bank, uint64_t& generation)
{
    const auto first = readBank(file, slot, 0, out);
    const auto second = readBank(file, slot, 1, out);
    // Never downgrade or overwrite an unfamiliar future format or unreadable bank.
    if (first.result == StoreResult::UnsupportedVersion || second.result == StoreResult::UnsupportedVersion)
        return StoreResult::UnsupportedVersion;
    if (first.result == StoreResult::IoError || second.result == StoreResult::IoError) return StoreResult::IoError;
    if (first.result != StoreResult::Ok && second.result != StoreResult::Ok) return StoreResult::Corrupt;
    if (second.result == StoreResult::Ok && (first.result != StoreResult::Ok || second.generation > first.generation))
    {
        current_bank = 1;
        generation = second.generation;
    }
    else
    {
        // The second read reused the caller's output. Prefer one extra bounded
        // flash read over a permanent second EventRecord (208 bytes). Both
        // banks were validated above before selecting or permitting a write.
        const auto selected = readBank(file, slot, 0, out);
        if (selected.result != StoreResult::Ok) return selected.result;
        if (selected.generation != first.generation) return StoreResult::Corrupt;
        current_bank = 0;
        generation = first.generation;
    }
    return StoreResult::Ok;
}

agenda::StoreResult AgendaFileStore::readSlot(uint16_t slot, agenda::EventRecord& out)
{
    out = {};
    if (slot >= slotCount()) return StoreResult::OutOfRange;
    if (!ready_) return StoreResult::IoError;
    RecordFileIo::Handle file = io_.open(path_, RecordFileIo::Mode::Read).handle;
    if (!file) return StoreResult::IoError;
    uint8_t bank = 0;
    uint64_t generation = 0;
    auto result = choose(file, slot, out, bank, generation);
    if (!io_.close(file)) result = StoreResult::IoError;
    if (result != StoreResult::Ok) out = {};
    return result;
}

agenda::StoreResult AgendaFileStore::writeSlot(uint16_t slot, const agenda::EventRecord& record)
{
    if (slot >= slotCount()) return StoreResult::OutOfRange;
    if (!agenda::validEvent(record)) return StoreResult::InvalidRecord;
    if (!ready_) return StoreResult::IoError;
    RecordFileIo::Handle file = io_.open(path_, RecordFileIo::Mode::Update).handle;
    if (!file) return StoreResult::IoError;
    uint8_t bank = 0;
    uint64_t generation = 0;
    // One bounded automatic record; never a full database or protocol buffer.
    agenda::EventRecord previous;
    auto result = choose(file, slot, previous, bank, generation);
    if (result == StoreResult::Ok)
    {
        if (generation == std::numeric_limits<uint64_t>::max()) result = StoreResult::OutOfRange;
        else if (!agenda::encodeEvent(record, bytes_)) result = StoreResult::InvalidRecord;
        else
        {
            put(bytes_ + kGenerationOffset, generation + 1, 8);
            put(bytes_ + kSlotIndexOffset, slot, 2);
            put(bytes_ + kCrcOffset, agenda::recordCrc32(bytes_, kCrcOffset), 4);
            if (!io_.seek(file, offset(slot, bank ^ 1)) ||
                io_.write(file, bytes_, sizeof(bytes_)) != sizeof(bytes_) || !io_.sync(file))
                result = StoreResult::IoError;
        }
    }
    if (!io_.close(file)) result = StoreResult::IoError;
    return result;
}

agenda::StoreResult AgendaFileStore::eraseSlot(uint16_t slot)
{
    agenda::EventRecord record;
    const auto result = readSlot(slot, record);
    if (result != StoreResult::Ok) return result;
    const uint32_t id = record.id;
    record = {};
    record.id = id;
    record.state = agenda::RecordState::Deleted;
    return writeSlot(slot, record);
}

} // namespace platform::esp::storage
