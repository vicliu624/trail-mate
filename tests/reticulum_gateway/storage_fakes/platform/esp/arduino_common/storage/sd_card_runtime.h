#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace platform::esp::arduino_common::storage
{
enum class SdFileReadStatus : uint8_t
{
    Ready,
    Missing,
    Busy,
    Unavailable,
    IoError,
    Invalid
};
struct SdFileReadResult
{
    SdFileReadStatus status;
    size_t bytes_read = 0;
    uint64_t file_size = 0;
};
bool sd_card_ready();
bool sd_external_block_owner_active();
uint32_t sd_media_session();
bool sd_is_directory(const char*);
bool sd_mkdir(const char*);
SdFileReadResult sd_read_file(const char*, uint8_t*, size_t);
class SdRuntimeFile
{
  public:
    bool open(const char*, const char*, uint32_t);
    size_t write(const void*, size_t);
    bool flush();
    void close() {}

  private:
    std::string path_;
};
} // namespace platform::esp::arduino_common::storage
