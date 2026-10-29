#pragma once

#include "music/storage.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// Implements the core player's logical SD filesystem under a caller-owned
// Linux root. No board name, device path or home-directory policy lives here.
class LinuxMusicStorage final : public ::music::Storage
{
  public:
    explicit LinuxMusicStorage(std::filesystem::path root);
    ~LinuxMusicStorage() override;
    bool ready() const override;
    uint32_t session() const override;
    ::music::File* open(const char* path, ::music::FileMode mode) override;
    void close(::music::File*& file) override;
    ::music::StorageResult last_result() const override;
    ::music::StorageResult next(const char* directory, uint64_t& cursor, ::music::Entry& entry) override;
    bool exists(const char* path) override;
    bool mkdir(const char* path) override;
    bool remove(const char* path) override;
    bool rename(const char* from, const char* to) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
