#include "platform/esp/common/storage/waypoint_file_store.h"
#include <cstring>
#include <limits>

namespace platform::esp::storage
{
namespace
{
using waypoint::Result;
constexpr uint8_t header[64] = {'T', 'M', 'W', 'P', 1, 64, 64, 2};
long offset(uint16_t slot, uint8_t bank) { return 64 + (slot * 2 + bank) * 64; }
Result close(RecordFileIo& io, RecordFileIo::Handle file, Result result)
{
    return io.close(file) ? result : Result::IoError;
}
} // namespace

Result WaypointFileStore::begin()
{
    ready_ = false;
    if (!path_ || !staging_ || !std::strcmp(path_, staging_)) return Result::Invalid;
    const auto opened = io_.open(path_, RecordFileIo::Mode::Read);
    auto* file = opened.handle;
    if (!file)
    {
        if (opened.status != RecordFileIo::OpenStatus::Missing) return Result::IoError;
        file = io_.open(staging_, RecordFileIo::Mode::CreateTemporary).handle;
        if (!file) return Result::IoError;
        bool ok = io_.write(file, header, sizeof(header)) == sizeof(header);
        waypoint::Slot empty;
        empty.generation = 1;
        ok = ok && waypoint::encode(empty, bytes_, sizeof(bytes_));
        for (unsigned i = 0; ok && i < slotCount() * 2; ++i)
            ok = io_.write(file, bytes_, sizeof(bytes_)) == sizeof(bytes_);
        ok = ok && io_.sync(file);
        const auto result = close(io_, file, ok ? Result::Ok : Result::IoError);
        if (result != Result::Ok) return result;
        if (!io_.publish(staging_, path_)) return Result::IoError;
        file = io_.open(path_, RecordFileIo::Mode::Read).handle;
        if (!file) return Result::IoError;
    }
    Result result = Result::Ok;
    uint64_t bytes = 0;
    if (!io_.size(file, bytes)) result = Result::IoError;
    else if (bytes != kFileBytes) result = Result::Corrupt;
    else if (!io_.seek(file, 0) || io_.read(file, bytes_, sizeof(bytes_)) != sizeof(bytes_)) result = Result::IoError;
    else if (std::memcmp(header, bytes_, sizeof(header))) result = Result::Corrupt;
    result = close(io_, file, result);
    ready_ = result == Result::Ok;
    return result;
}

Result WaypointFileStore::choose(RecordFileIo::Handle file, uint16_t slot, waypoint::Slot& out, uint8_t& bank)
{
    bool found = false;
    waypoint::Slot candidate;
    for (uint8_t index = 0; index < 2; ++index)
    {
        if (!io_.seek(file, offset(slot, index)) || io_.read(file, bytes_, sizeof(bytes_)) != sizeof(bytes_)) return Result::IoError;
        // An unfamiliar version must not be silently overwritten via fallback.
        if (bytes_[0] == 'W' && bytes_[1] == 'P' && bytes_[2] != 1) return Result::Corrupt;
        if (!waypoint::decode(bytes_, sizeof(bytes_), candidate)) continue;
        if (!found || candidate.generation > out.generation)
        {
            out = candidate;
            bank = index;
            found = true;
        }
    }
    return found ? Result::Ok : Result::Corrupt;
}

Result WaypointFileStore::read(uint16_t slot, waypoint::Record& out)
{
    out = {};
    if (slot >= slotCount()) return Result::Invalid;
    if (!ready_) return Result::IoError;
    auto* file = io_.open(path_, RecordFileIo::Mode::Read).handle;
    if (!file) return Result::IoError;
    waypoint::Slot selected;
    uint8_t bank = 0;
    const auto result = close(io_, file, choose(file, slot, selected, bank));
    if (result == Result::Ok && selected.state == waypoint::SlotState::Active) out = selected.record;
    return result;
}

Result WaypointFileStore::mutate(const waypoint::Record& value, bool creating, bool deleting, uint32_t& id)
{
    id = 0;
    auto validation = value;
    if (creating) validation.id = 1;
    if ((!deleting && !waypoint::valid(validation)) || (deleting && !value.id)) return Result::Invalid;
    if (!ready_) return Result::IoError;
    auto* file = io_.open(path_, RecordFileIo::Mode::Update).handle;
    if (!file) return Result::IoError;
    uint16_t target = slotCount();
    uint32_t largest = 0;
    waypoint::Slot previous;
    uint8_t bank = 0;
    for (uint16_t slot = 0; slot < slotCount(); ++slot)
    {
        const auto result = choose(file, slot, previous, bank);
        if (result != Result::Ok) return close(io_, file, result);
        if (previous.record.id > largest) largest = previous.record.id;
        if (creating ? (previous.state != waypoint::SlotState::Active && target == slotCount())
                     : (previous.state == waypoint::SlotState::Active && previous.record.id == value.id)) target = slot;
    }
    if (target == slotCount()) return close(io_, file, creating ? Result::Full : Result::NotFound);
    if (creating && largest == std::numeric_limits<uint32_t>::max()) return close(io_, file, Result::Full);
    auto result = choose(file, target, previous, bank);
    if (result != Result::Ok) return close(io_, file, result);
    if (previous.generation == std::numeric_limits<uint64_t>::max()) return close(io_, file, Result::Full);
    ++previous.generation;
    previous.record = value;
    previous.record.id = creating ? largest + 1 : value.id;
    previous.state = deleting ? waypoint::SlotState::Deleted : waypoint::SlotState::Active;
    if (!waypoint::encode(previous, bytes_, sizeof(bytes_))) return close(io_, file, Result::Invalid);
    const bool ok = io_.seek(file, offset(target, bank ^ 1)) &&
                    io_.write(file, bytes_, sizeof(bytes_)) == sizeof(bytes_) && io_.sync(file);
    result = close(io_, file, ok ? Result::Ok : Result::IoError);
    if (result == Result::Ok) id = previous.record.id;
    return result;
}
Result WaypointFileStore::create(const waypoint::Record& value, uint32_t& id) { return mutate(value, true, false, id); }
Result WaypointFileStore::update(const waypoint::Record& value)
{
    uint32_t id;
    return mutate(value, false, false, id);
}
Result WaypointFileStore::remove(uint32_t id)
{
    waypoint::Record value;
    value.id = id;
    uint32_t ignored;
    return mutate(value, false, true, ignored);
}
} // namespace platform::esp::storage
