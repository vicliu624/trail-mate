#pragma once
#include "geocaching/protocol/record_decoder.h"
#include <cstring>

namespace geocaching::protocol
{
inline constexpr char kApplicationType[] = "trailmate.geocache";
inline constexpr char kDirectoryAspect[] = "trailmate.geocache.directory";

struct DirectoryAnnouncement
{
    Destination delivery;
    std::array<std::uint8_t, 16> epoch{};
    std::uint64_t sequence = 0;
    std::string_view name;
};

// Only call after native announce signature/aspect verification. expected_delivery
// must be derived from that authenticated identity, never from this app_data.
inline bool decodeDirectoryAnnouncement(ByteView app_data, const Destination& expected_delivery,
                                        DirectoryAnnouncement& out)
{
    out = {};
    if (!app_data.data || app_data.size > 96) return false;
    CmpReader reader(app_data);
    std::size_t count = 0;
    std::uint64_t version = 0;
    ByteView delivery, epoch;
    DirectoryAnnouncement candidate;
    if (!reader.array(count, 5) || count != 5 ||
        !reader.unsignedInteger(version) || version != 1 ||
        !reader.binary(delivery, 16) || delivery.size != 16 ||
        std::memcmp(delivery.data, expected_delivery.bytes.data(), 16) != 0 ||
        !reader.binary(epoch, 16) || epoch.size != 16 ||
        !reader.unsignedInteger(candidate.sequence) || !reader.text(candidate.name, 40) ||
        !validRecordText(candidate.name, false, false) || !reader.finished()) return false;
    std::memcpy(candidate.delivery.bytes.data(), delivery.data, 16);
    std::memcpy(candidate.epoch.data(), epoch.data, 16);
    out = candidate;
    return true;
}
} // namespace geocaching::protocol
