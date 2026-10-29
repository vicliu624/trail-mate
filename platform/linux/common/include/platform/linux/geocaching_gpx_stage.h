#pragma once
#include "geocaching/protocol/verify_record.h"
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
// Blocking storage-owner port. Ready proves a durable, read-back staging file,
// not an installed cache. A failed stage is retained for explicit recovery.
class LinuxGeocachingGpxStage
{
  public:
    using TransactionId = std::array<uint8_t, 16>;
    enum class Result
    {
        Ready,
        Invalid,
        CryptoUnavailable,
        Unavailable,
        Exists,
        Missing,
        Unsafe,
        IoError
    };
    struct Proof
    {
        std::string relative_path;
        std::array<uint8_t, 32> file_hash{};
        uint64_t bytes = 0;
    };
    // root is an existing cache directory supplied by the composition, not a
    // device path. Crypto must outlive this single storage owner.
    LinuxGeocachingGpxStage(std::filesystem::path root, ::geocaching::protocol::RecordCrypto& crypto);
    // Duplicates a pinned descriptor; caller keeps the matching volume lease.
    LinuxGeocachingGpxStage(int root_descriptor, ::geocaching::protocol::RecordCrypto& crypto);
    ~LinuxGeocachingGpxStage();
    Result stage(const TransactionId&, const ::geocaching::GeocacheId&, const ::geocaching::RevisionHash&,
                 ::geocaching::ByteView signed_record, Proof&);
    Result verify(const TransactionId&, const ::geocaching::GeocacheId&, const ::geocaching::RevisionHash&,
                  ::geocaching::ByteView signed_record, Proof&);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
