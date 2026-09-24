#pragma once

#include "platform/esp/common/storage/record_file_io.h"

namespace platform::esp::arduino_common::storage
{
// Uses the shared SD runtime for both shared-SPI and SDMMC cards.
// No mounts, formatting, direct driver access or internal-flash fallback.
class SdRecordFileIo final : public platform::esp::storage::RecordFileIo
{
  public:
    // Bind only when the owner starts validating a new storage session.
    void bindSession(uint32_t session) { session_ = session; }
    OpenResult open(const char* path, Mode mode) override;
    bool close(Handle file) override;
    bool size(Handle file, uint64_t& bytes) override;
    bool seek(Handle file, uint64_t offset) override;
    std::size_t read(Handle file, void* buffer, std::size_t bytes) override;
    std::size_t write(Handle file, const void* buffer, std::size_t bytes) override;
    bool sync(Handle file) override;
    bool publish(const char* temporary, const char* destination) override;

  private:
    bool available() const;
    bool usable(Handle file) const;
    uint32_t session_ = 0;
};
} // namespace platform::esp::arduino_common::storage
