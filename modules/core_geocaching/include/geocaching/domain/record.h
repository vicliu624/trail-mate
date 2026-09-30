#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace geocaching
{

inline constexpr std::size_t kMaxRecordBytes = 4096;
inline constexpr std::size_t kMaxApplicationBytes = 8192;
inline constexpr std::size_t kMaxGpxBytes = 65536;
inline constexpr std::size_t kMaxNameBytes = 96;
inline constexpr std::size_t kMaxDescriptionBytes = 2048;
inline constexpr std::size_t kMaxHintBytes = 512;

struct GeocacheId
{
    std::array<std::uint8_t, 32> bytes{};
};

struct RevisionHash
{
    std::array<std::uint8_t, 32> bytes{};
};

struct RequestId
{
    std::array<std::uint8_t, 16> bytes{};
};

struct Destination
{
    std::array<std::uint8_t, 16> bytes{};
};

enum class CacheState : std::uint8_t
{
    Active = 0,
    Disabled = 1,
    Archived = 2,
};

enum class ContainerSize : std::uint8_t
{
    Unspecified = 0,
    Micro = 1,
    Small = 2,
    Regular = 3,
    Large = 4,
    Other = 5,
};

enum class Operation : std::uint8_t
{
    Capabilities = 0,
    Publish = 1,
    Query = 2,
    Get = 3,
    Sync = 4,
};

// Borrowed bytes. The caller owns the immutable backing payload and must keep
// it alive throughout validation and use of any views derived from it.
struct ByteView
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// Parsing is not verification. This view must never be advertised as an
// authenticated object until signature and version checks have completed.
// Text is borrowed from the original record, not copied into task-stack arrays.
struct RecordView
{
    ByteView encoded;
    ByteView author_public_key;
    ByteView creation_nonce;
    ByteView previous_hash;
    std::uint32_t revision = 0;
    CacheState state = CacheState::Active;
    std::int32_t latitude_e7 = 0;
    std::int32_t longitude_e7 = 0;
    std::string_view name;
    std::string_view description;
    std::string_view hint;
    std::uint8_t difficulty_x2 = 0;
    std::uint8_t terrain_x2 = 0;
    ContainerSize container_size = ContainerSize::Unspecified;
    std::uint64_t created_at = 0;
    std::uint64_t updated_at = 0;
};

} // namespace geocaching
