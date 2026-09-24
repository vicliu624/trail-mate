#pragma once
#include <cstddef>
#include <cstdint>
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready();
bool sd_external_block_owner_active();
uint32_t sd_media_session();
enum class SdFileReadStatus
{
    Missing,
    Invalid
};
struct SdFileReadResult
{
    SdFileReadStatus status;
};
SdFileReadResult sd_read_file(const char*, void*, std::size_t);
bool sd_rename(const char*, const char*, uint32_t);
class SdRuntimeFile
{
  public:
    bool open(const char*, const char*, uint32_t);
    bool is_open() const;
    uint64_t size() const { return 16; }
    bool seek(uint64_t) { return is_open(); }
    int read(void*, std::size_t bytes) { return is_open() ? static_cast<int>(bytes) : 0; }
    std::size_t write(const void*, std::size_t bytes) { return is_open() ? bytes : 0; }
    bool flush() { return is_open(); }

  private:
    uint32_t session_ = 0;
};
} // namespace platform::esp::arduino_common::storage
