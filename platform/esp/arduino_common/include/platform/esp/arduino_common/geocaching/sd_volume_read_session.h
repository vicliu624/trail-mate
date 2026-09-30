#pragma once
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
// Owned by one read operation, never shared globally or across queries. The
// volume format is immutable while mounted. A remount/USB transition poisons
// this snapshot even if the same volume is subsequently mounted again.
class SdVolumeReadSession
{
  public:
    SdVolumeResult inspect(::geocaching::storage::VolumeInstance& out)
    {
        out = {};
        if (invalid_) return SdVolumeResult::IoError;
        if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return SdVolumeResult::Unavailable;
        const auto session = storage::sd_media_session();
        if (ready_)
        {
            if (session != session_)
            {
                invalid_ = true;
                return SdVolumeResult::IoError;
            }
            out = volume_;
            return SdVolumeResult::Ready;
        }
        const auto status = inspectSdVolume(out);
        if (session != storage::sd_media_session())
        {
            invalid_ = true;
            out = {};
            return SdVolumeResult::IoError;
        }
        if (status == SdVolumeResult::Ready)
        {
            volume_ = out;
            session_ = session;
            ready_ = true;
        }
        return status;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_{};
    uint32_t session_ = 0;
    bool ready_ = false;
    bool invalid_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
