#pragma once
#include "platform/esp/common/storage/stdio_record_file_io.h"
namespace platform::esp::arduino_common::storage
{
// Native root tests retain the production record stores and replace only I/O.
class SdRecordFileIo final : public platform::esp::storage::RecordFileIo
{
  public:
    void bindSession(uint32_t) {}
    OpenResult open(const char* path, Mode mode) override { return files_.open(path, mode); }
    bool close(Handle file) override { return files_.close(file); }
    bool size(Handle file, uint64_t& bytes) override { return files_.size(file, bytes); }
    bool seek(Handle file, uint64_t offset) override { return files_.seek(file, offset); }
    std::size_t read(Handle file, void* buffer, std::size_t bytes) override { return files_.read(file, buffer, bytes); }
    std::size_t write(Handle file, const void* buffer, std::size_t bytes) override { return files_.write(file, buffer, bytes); }
    bool sync(Handle file) override { return files_.sync(file); }
    bool publish(const char* temporary, const char* destination) override { return files_.publish(temporary, destination); }

  private:
    platform::esp::storage::StdioRecordFileIo files_;
};
} // namespace platform::esp::arduino_common::storage
