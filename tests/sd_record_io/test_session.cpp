#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/storage/sd_record_file_io.h"
#include <cassert>
namespace io = platform::esp::arduino_common::storage;
namespace
{
uint32_t session = 1;
bool external = false;
bool replace_on_open = false;
bool replace_on_probe = false;
unsigned opens = 0;
unsigned publishes = 0;
} // namespace
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return true; }
bool sd_external_block_owner_active() { return external; }
uint32_t sd_media_session() { return session; }
SdFileReadResult sd_read_file(const char*, void*, std::size_t)
{
    if (replace_on_probe)
    {
        ++session;
        replace_on_probe = false;
    }
    return {SdFileReadStatus::Missing};
}
bool sd_rename(const char*, const char*, uint32_t expected)
{
    if (expected != session || external) return false;
    ++publishes;
    return true;
}
bool SdRuntimeFile::open(const char*, const char*, uint32_t expected)
{
    if (replace_on_open)
    {
        ++session;
        replace_on_open = false;
    }
    if (expected != session || external) return false;
    session_ = expected;
    ++opens;
    return true;
}
bool SdRuntimeFile::is_open() const { return session_ == session && !external; }
} // namespace platform::esp::arduino_common::storage
int main()
{
    io::SdRecordFileIo files;
    using Mode = io::SdRecordFileIo::Mode;
    using Status = io::SdRecordFileIo::OpenStatus;
    assert(files.open("events", Mode::Update).status == Status::Unavailable);
    files.bindSession(session);
    auto opened = files.open("events", Mode::Update);
    assert(opened.status == Status::Ready);
    assert(files.sync(opened.handle));
    assert(files.close(opened.handle));
    ++session;
    // Closing one file does not permit the next part of an old transaction
    // to open or publish on a replacement card.
    assert(files.open("events", Mode::Update).status == Status::Unavailable);
    assert(!files.publish("pending", "events"));
    assert(opens == 1 && publishes == 0);
    files.bindSession(session);
    replace_on_open = true;
    assert(files.open("pending", Mode::CreateTemporary).status == Status::Unavailable);
    assert(opens == 1);
    files.bindSession(session);
    replace_on_probe = true;
    assert(!files.publish("pending", "events"));
    assert(publishes == 0);
    files.bindSession(session);
    opened = files.open("events", Mode::Update);
    assert(opened.status == Status::Ready);
    ++session;
    char byte = 0;
    assert(files.write(opened.handle, &byte, 1) == 0);
    assert(!files.sync(opened.handle));
    assert(!files.close(opened.handle));
    files.bindSession(session);
    external = true;
    assert(files.open("events", Mode::Read).status == Status::Unavailable);
    assert(!files.publish("pending", "events"));
    external = false;
    assert(files.publish("pending", "events"));
    assert(publishes == 1);
}
