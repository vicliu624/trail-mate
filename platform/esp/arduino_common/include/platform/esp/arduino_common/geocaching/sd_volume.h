#pragma once
#include "geocaching/storage/volume_format.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

namespace platform::esp::arduino_common::geocaching
{
enum class SdVolumeResult : uint8_t
{
    Ready,
    Missing,
    Unavailable,
    Unsupported,
    Corrupt,
    IoError
};

inline SdVolumeResult inspectSdVolume(::geocaching::storage::VolumeInstance& instance)
{
    instance = {};
    ::geocaching::storage::VolumeHeader header;
    const auto result = storage::sd_read_file("/trailmate/geocaching/.state/format.bin", header.data(), header.size());
    switch (result.status)
    {
    case storage::SdFileReadStatus::Missing:
        return SdVolumeResult::Missing;
    case storage::SdFileReadStatus::Busy:
    case storage::SdFileReadStatus::Unavailable:
        return SdVolumeResult::Unavailable;
    case storage::SdFileReadStatus::IoError:
        return SdVolumeResult::IoError;
    case storage::SdFileReadStatus::Invalid:
        return SdVolumeResult::Corrupt;
    case storage::SdFileReadStatus::Ready:
        break;
    }
    if (result.bytes_read != result.file_size || result.bytes_read > header.size()) return SdVolumeResult::Corrupt;
    switch (::geocaching::storage::decodeVolumeHeader({header.data(), result.bytes_read}, instance))
    {
    case ::geocaching::storage::VolumeFormatResult::Supported:
        return SdVolumeResult::Ready;
    case ::geocaching::storage::VolumeFormatResult::Unsupported:
        return SdVolumeResult::Unsupported;
    case ::geocaching::storage::VolumeFormatResult::Corrupt:
        return SdVolumeResult::Corrupt;
    }
    return SdVolumeResult::Corrupt;
}
// Storage-worker operation, before any request is admitted. The caller supplies
// a freshly generated CSPRNG volume ID. An existing .state directory without a
// valid header is recovery evidence, including interrupted initialization; it
// must never be silently replaced by another empty task ledger.
inline SdVolumeResult createNewSdVolume(const ::geocaching::storage::VolumeInstance& instance,
                                        ::geocaching::storage::VolumeInstance& confirmed_instance)
{
    confirmed_instance = {};
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return SdVolumeResult::Unavailable;
    ::geocaching::storage::VolumeInstance existing;
    const auto inspection = inspectSdVolume(existing);
    if (inspection != SdVolumeResult::Missing)
    {
        if (inspection == SdVolumeResult::Ready) confirmed_instance = existing;
        return inspection;
    }
    if (storage::sd_exists("/trailmate/geocaching/.state")) return SdVolumeResult::Corrupt;
    const char* directories[] = {
        "/trailmate", "/trailmate/geocaching", "/trailmate/geocaching/caches",
        "/trailmate/geocaching/imports", "/trailmate/geocaching/exports",
        "/trailmate/geocaching/.state", "/trailmate/geocaching/.state/history",
        "/trailmate/geocaching/.state/journal", "/trailmate/geocaching/.state/checkpoint",
        "/trailmate/geocaching/.state/staging"};
    for (const auto* directory : directories)
    {
        if (!storage::sd_is_directory(directory) && !storage::sd_mkdir(directory)) return SdVolumeResult::IoError;
    }
    const auto header = ::geocaching::storage::encodeVolumeHeader(instance);
    storage::SdRuntimeFile file;
    if (!file.open("/trailmate/geocaching/.state/format.bin", "w")) return SdVolumeResult::IoError;
    const bool written = file.write(header.data(), header.size()) == header.size() && file.flush();
    file.close();
    if (!written) return SdVolumeResult::IoError;
    const auto verified = inspectSdVolume(existing);
    if (verified != SdVolumeResult::Ready) return verified;
    if (existing != instance) return SdVolumeResult::Corrupt;
    confirmed_instance = existing;
    return SdVolumeResult::Ready;
}
} // namespace platform::esp::arduino_common::geocaching
