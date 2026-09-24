#include "agenda/domain/event_codec.h"
#include "agenda/usecase/agenda_service.h"
#include "platform/esp/common/storage/agenda_file_store.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
using FileStore = platform::esp::storage::AgendaFileStore;
using namespace agenda;

struct TemporaryDirectory
{
    std::filesystem::path path;
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("trailmate-agenda-" + std::to_string(stamp));
        assert(std::filesystem::create_directory(path));
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
};

void readBytes(const std::string& path, std::size_t offset, uint8_t* data, std::size_t size)
{
    std::ifstream stream(path, std::ios::binary);
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    assert(stream.good());
}
void writeBytes(const std::string& path, std::size_t offset, const uint8_t* data, std::size_t size)
{
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    stream.seekp(static_cast<std::streamoff>(offset));
    stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    stream.flush();
    assert(stream.good());
}
void refreshCrc(uint8_t (&bank)[kSlotBytes])
{
    const auto crc = recordCrc32(bank, kSlotBytes - 4);
    for (unsigned i = 0; i < 4; ++i) bank[kSlotBytes - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
}
} // namespace

int main()
{
    TemporaryDirectory directory;
    const auto path = (directory.path / "events.dat").string();
    const auto init = (directory.path / "events.init").string();
    FileStore store(path.c_str(), init.c_str());
    assert(store.begin() == StoreResult::Ok);
    assert(std::filesystem::file_size(path) == FileStore::kFileBytes);
    EventRecord out;
    for (uint16_t slot = 0; slot < 64; ++slot)
        assert(store.readSlot(slot, out) == StoreResult::Ok && out.state == RecordState::Empty);
    assert(store.readSlot(64, out) == StoreResult::OutOfRange);

    AgendaService service(store);
    EventRecord draft;
    draft.start_time = 100000;
    std::strcpy(draft.title, "Radio check");
    uint32_t id = 0;
    assert(service.create(draft, id) == AgendaResult::Ok && id == 1);
    FileStore reload(path.c_str(), init.c_str());
    assert(reload.begin() == StoreResult::Ok);
    assert(reload.readSlot(0, out) == StoreResult::Ok && out.id == 1);
    assert(std::strcmp(out.title, "Radio check") == 0);

    // Preserve a committed bank and model power interruption after every
    // possible byte prefix of the alternate-bank update.
    uint8_t old_bank[kSlotBytes];
    uint8_t new_bank[kSlotBytes];
    readBytes(path, FileStore::kHeaderBytes, old_bank, sizeof(old_bank));
    std::strcpy(out.title, "Check campsite");
    out.flags = HasReminder;
    out.reminder_offset_sec = 600;
    assert(reload.writeSlot(0, out) == StoreResult::Ok);
    readBytes(path, FileStore::kHeaderBytes, new_bank, sizeof(new_bank));
    for (std::size_t prefix = 0; prefix <= kSlotBytes; ++prefix)
    {
        writeBytes(path, FileStore::kHeaderBytes, old_bank, sizeof(old_bank));
        writeBytes(path, FileStore::kHeaderBytes, new_bank, prefix);
        FileStore crash_reload(path.c_str(), init.c_str());
        assert(crash_reload.begin() == StoreResult::Ok);
        assert(crash_reload.readSlot(0, out) == StoreResult::Ok);
        assert(out.id == 1 && out.state == RecordState::Active);
        assert(std::strcmp(out.title, "Radio check") == 0 || std::strcmp(out.title, "Check campsite") == 0);
    }
    assert(std::strcmp(out.title, "Check campsite") == 0);
    assert(reload.eraseSlot(0) == StoreResult::Ok);
    FileStore after_delete(path.c_str(), init.c_str());
    assert(after_delete.begin() == StoreResult::Ok);
    assert(after_delete.readSlot(0, out) == StoreResult::Ok && out.state == RecordState::Deleted && out.id == 1);
    AgendaService reloaded_service(after_delete);
    assert(reloaded_service.create(draft, id) == AgendaResult::Ok && id == 2);
    for (uint32_t next = 3; next <= 65; ++next)
        assert(reloaded_service.create(draft, id) == AgendaResult::Ok && id == next);
    assert(reloaded_service.create(draft, id) == AgendaResult::Full);

    // Both copies corrupt: fail closed, never silently recreate the database.
    readBytes(path, FileStore::kHeaderBytes, old_bank, sizeof(old_bank));
    readBytes(path, FileStore::kHeaderBytes + kSlotBytes, new_bank, sizeof(new_bank));
    uint8_t corrupt[kSlotBytes];
    // Reusing one decode destination must preserve either surviving bank,
    // including bank 0 after the corrupt bank 1 decode has cleared the output.
    EventRecord surviving;
    assert(decodeEvent(old_bank, surviving) == DecodeResult::Ok);
    std::memcpy(corrupt, new_bank, sizeof(corrupt));
    corrupt[40] ^= 1;
    writeBytes(path, FileStore::kHeaderBytes + kSlotBytes, corrupt, sizeof(corrupt));
    assert(after_delete.readSlot(0, out) == StoreResult::Ok);
    assert(out.id == surviving.id && out.state == surviving.state);
    assert(std::strcmp(out.title, surviving.title) == 0);
    writeBytes(path, FileStore::kHeaderBytes + kSlotBytes, new_bank, sizeof(new_bank));

    std::memcpy(corrupt, old_bank, sizeof(corrupt));
    corrupt[40] ^= 1;
    writeBytes(path, FileStore::kHeaderBytes, corrupt, sizeof(corrupt));
    assert(decodeEvent(new_bank, surviving) == DecodeResult::Ok);
    assert(after_delete.readSlot(0, out) == StoreResult::Ok);
    assert(out.id == surviving.id && out.state == surviving.state);
    assert(std::strcmp(out.title, surviving.title) == 0);
    std::memcpy(corrupt, new_bank, sizeof(corrupt));
    corrupt[40] ^= 1;
    writeBytes(path, FileStore::kHeaderBytes + kSlotBytes, corrupt, sizeof(corrupt));
    assert(after_delete.readSlot(0, out) == StoreResult::Corrupt && out.state == RecordState::Empty);
    assert(after_delete.readSlot(1, out) == StoreResult::Ok && out.id == 3);
    writeBytes(path, FileStore::kHeaderBytes, old_bank, sizeof(old_bank));
    writeBytes(path, FileStore::kHeaderBytes + kSlotBytes, new_bank, sizeof(new_bank));

    // A future-format bank must not be silently downgraded to an older copy.
    std::memcpy(corrupt, old_bank, sizeof(corrupt));
    corrupt[2] = 2;
    refreshCrc(corrupt);
    writeBytes(path, FileStore::kHeaderBytes, corrupt, sizeof(corrupt));
    assert(after_delete.readSlot(0, out) == StoreResult::UnsupportedVersion);
    writeBytes(path, FileStore::kHeaderBytes, old_bank, sizeof(old_bank));

    // A valid bank copied to the wrong logical slot must be rejected.
    writeBytes(path, FileStore::kHeaderBytes + 2 * kSlotBytes, old_bank, sizeof(old_bank));
    writeBytes(path, FileStore::kHeaderBytes + 3 * kSlotBytes, old_bank, sizeof(old_bank));
    assert(after_delete.readSlot(1, out) == StoreResult::Corrupt);

    // Invalid header and short file are reported, not reformatted.
    uint8_t header[kSlotBytes];
    readBytes(path, 0, header, sizeof(header));
    header[0] ^= 1;
    writeBytes(path, 0, header, sizeof(header));
    assert(after_delete.begin() == StoreResult::Corrupt);
    assert(std::filesystem::file_size(path) == FileStore::kFileBytes);
    header[0] ^= 1;
    writeBytes(path, 0, header, sizeof(header));
    std::filesystem::resize_file(path, FileStore::kFileBytes - 1);
    assert(after_delete.begin() == StoreResult::Corrupt);
    std::printf("AgendaFileStore=%zu file=%zu logical_slots=%u\n", sizeof(FileStore), FileStore::kFileBytes, kMaxActiveEvents);
}
