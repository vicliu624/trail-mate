#include "platform/esp/arduino_common/storage/sd_record_file_io.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <new>

namespace platform::esp::arduino_common::storage
{
namespace
{
SdRuntimeFile* file_of(void* file) { return static_cast<SdRuntimeFile*>(file); }
bool absent(const char* path)
{
    // Ask the existing runtime for semantic absence, not sd_exists(false),
    // which also covers lock contention and media errors. One byte is enough:
    // an existing larger file reports Invalid, never Missing.
    uint8_t probe = 0;
    return sd_read_file(path, &probe, 1).status == SdFileReadStatus::Missing;
}
} // namespace

bool SdRecordFileIo::available() const
{
    return session_ == sd_media_session() && sd_card_ready() && !sd_external_block_owner_active();
}
bool SdRecordFileIo::usable(Handle file) const
{
    return file && available() && file_of(file)->is_open();
}

SdRecordFileIo::OpenResult SdRecordFileIo::open(const char* path, Mode mode)
{
    if (!path || !*path) return {};
    if (!available()) return {nullptr, OpenStatus::Unavailable};
    auto* file = new (std::nothrow) SdRuntimeFile;
    if (!file) return {};
    const char* flags = mode == Mode::Read ? "rb" : mode == Mode::Update ? "r+b"
                                                                         : "wb";
    if (file->open(path, flags, session_)) return {file, OpenStatus::Ready};
    delete file;
    if (!available()) return {nullptr, OpenStatus::Unavailable};
    return {nullptr, mode == Mode::Read && absent(path) ? OpenStatus::Missing : OpenStatus::IoError};
}
bool SdRecordFileIo::close(Handle file)
{
    if (!file) return false;
    const bool ready = usable(file);
    // SdRuntimeFile has no close status; durability must be established by
    // sync before this release. Never manufacture a durable write result here.
    delete file_of(file);
    return ready;
}
bool SdRecordFileIo::size(Handle file, uint64_t& bytes)
{
    bytes = 0;
    if (!usable(file)) return false;
    bytes = file_of(file)->size();
    return true;
}
bool SdRecordFileIo::seek(Handle file, uint64_t offset)
{
    return usable(file) && file_of(file)->seek(offset);
}
std::size_t SdRecordFileIo::read(Handle file, void* buffer, std::size_t bytes)
{
    if (!usable(file)) return 0;
    const int count = file_of(file)->read(buffer, bytes);
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}
std::size_t SdRecordFileIo::write(Handle file, const void* buffer, std::size_t bytes)
{
    return usable(file) ? file_of(file)->write(buffer, bytes) : 0;
}
bool SdRecordFileIo::sync(Handle file)
{
    return usable(file) && file_of(file)->flush();
}
bool SdRecordFileIo::publish(const char* temporary, const char* destination)
{
    return temporary && destination && available() && absent(destination) && sd_rename(temporary, destination, session_);
}
} // namespace platform::esp::arduino_common::storage
