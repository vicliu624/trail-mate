// Exercise the real Arduino Team store against a native filesystem. This fake
// owns only the SD file boundary; it does not duplicate the ring codec.
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace team_store_test
{
bool fail_seek = false;
bool fail_flush = false;
bool fail_record_read = false;
bool short_record_write = false;
bool fail_update_open = false;
bool fail_header_read = false;
std::size_t largest_read = 0;
} // namespace team_store_test

namespace platform::esp::arduino_common::storage
{
namespace
{
std::filesystem::path path_for(const char* path)
{
    return std::filesystem::path(std::getenv("TRAIL_MATE_TEST_SD_ROOT")) / std::filesystem::path(path).relative_path();
}
} // namespace
class SdRuntimeFile::Impl
{
  public:
    std::FILE* file = nullptr;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile()
{
    close();
    delete impl_;
}
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    close();
    if (team_store_test::fail_update_open && std::strcmp(mode, "r+") == 0) return false;
    impl_->file = std::fopen(path_for(path).c_str(), mode);
    return is_open();
}
void SdRuntimeFile::close()
{
    if (impl_->file) std::fclose(impl_->file);
    impl_->file = nullptr;
}
bool SdRuntimeFile::is_open() const { return impl_->file != nullptr; }
uint64_t SdRuntimeFile::position() const { return static_cast<uint64_t>(std::ftell(impl_->file)); }
uint64_t SdRuntimeFile::size() const
{
    const auto old = std::ftell(impl_->file);
    std::fseek(impl_->file, 0, SEEK_END);
    const auto length = std::ftell(impl_->file);
    std::fseek(impl_->file, old, SEEK_SET);
    return static_cast<uint64_t>(length);
}
bool SdRuntimeFile::seek(uint64_t offset)
{
    if (team_store_test::fail_seek) return false;
    // Some embedded filesystems reject seeks past EOF. Never let a hole hide a
    // short record when validating the production writer's append semantics.
    if (!is_open() || offset > size()) return false;
    return std::fseek(impl_->file, static_cast<long>(offset), SEEK_SET) == 0;
}
int SdRuntimeFile::available() const { return is_open() && position() < size(); }
int SdRuntimeFile::read(void* buffer, std::size_t count)
{
    team_store_test::largest_read = std::max(team_store_test::largest_read, count);
    if (team_store_test::fail_header_read && position() == 0) return 0;
    if (team_store_test::fail_record_read && position() >= 24 && count > 0) --count;
    return static_cast<int>(std::fread(buffer, 1, count, impl_->file));
}
int SdRuntimeFile::read_byte() { return std::fgetc(impl_->file); }
std::size_t SdRuntimeFile::write(const void* buffer, std::size_t count)
{
    if (team_store_test::short_record_write && position() >= 24 && count > 0) --count;
    return std::fwrite(buffer, 1, count, impl_->file);
}
std::size_t SdRuntimeFile::write_byte(uint8_t value) { return write(&value, 1); }
std::size_t SdRuntimeFile::print(const char* text) { return write(text, std::strlen(text)); }
bool SdRuntimeFile::flush() { return !team_store_test::fail_flush && std::fflush(impl_->file) == 0; }
bool sd_card_ready() { return true; }
bool sd_exists(const char* path) { return std::filesystem::exists(path_for(path)); }
bool sd_mkdir(const char* path) { return std::filesystem::create_directories(path_for(path)); }
bool sd_remove(const char* path) { return std::filesystem::remove(path_for(path)); }
bool sd_rename(const char* from, const char* to)
{
    std::error_code error;
    std::filesystem::rename(path_for(from), path_for(to), error);
    return !error;
}
} // namespace platform::esp::arduino_common::storage
namespace sys
{
uint32_t uptime_seconds_now() { return 100; }
} // namespace sys
