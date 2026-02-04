/**
 * @file team_track_sampler.cpp
 * @brief Periodic Team track sampler implementation
 */

#include "team_track_sampler.h"

#include "../../gps/gps_service_api.h"
#include <Arduino.h>
#include <cmath>
#include <ctime>

namespace team
{
namespace
{
constexpr uint32_t kMinValidEpoch = 1577836800U; // 2020-01-01
}

void TeamTrackSampler::update(TeamController* controller, bool team_active)
{
    if (!team_active || !controller)
    {
        reset();
        return;
    }

    uint32_t now_ms = millis();
    if (next_sample_ms_ == 0)
    {
        startWindow(now_ms);
    }

    while (count_ < proto::kTeamTrackMaxPoints &&
           static_cast<int32_t>(now_ms - next_sample_ms_) >= 0)
    {
        samplePoint(now_ms);
        next_sample_ms_ += kSampleIntervalMs;
        if (count_ >= proto::kTeamTrackMaxPoints)
        {
            flushWindow(controller);
            resetWindow(true);
            break;
        }
    }
}

void TeamTrackSampler::reset()
{
    resetWindow(false);
}

void TeamTrackSampler::resetWindow(bool keep_schedule)
{
    count_ = 0;
    valid_mask_ = 0;
    start_ts_ = 0;
    if (!keep_schedule)
    {
        next_sample_ms_ = 0;
    }
}

void TeamTrackSampler::startWindow(uint32_t now_ms)
{
    resetWindow(true);
    next_sample_ms_ = now_ms;
}

void TeamTrackSampler::samplePoint(uint32_t /*now_ms*/)
{
    if (count_ >= proto::kTeamTrackMaxPoints)
    {
        return;
    }

    gps::GpsState gps_state = gps::gps_get_data();
    if (gps_state.valid)
    {
        int32_t lat_e7 = static_cast<int32_t>(gps_state.lat * 1e7);
        int32_t lon_e7 = static_cast<int32_t>(gps_state.lng * 1e7);
        points_[count_].lat_e7 = lat_e7;
        points_[count_].lon_e7 = lon_e7;
        valid_mask_ |= (1u << count_);
    }
    else
    {
        points_[count_].lat_e7 = 0;
        points_[count_].lon_e7 = 0;
    }

    if (start_ts_ == 0)
    {
        uint32_t now_s = static_cast<uint32_t>(time(nullptr));
        if (now_s >= kMinValidEpoch)
        {
            uint32_t offset_s = static_cast<uint32_t>(count_) * (kSampleIntervalMs / 1000);
            start_ts_ = (now_s >= offset_s) ? (now_s - offset_s) : now_s;
        }
    }

    ++count_;
}

void TeamTrackSampler::flushWindow(TeamController* controller)
{
    if (!controller || count_ == 0)
    {
        return;
    }
    if (valid_mask_ == 0)
    {
        return;
    }

    proto::TeamTrackMessage msg{};
    msg.version = proto::kTeamTrackVersion;
    msg.start_ts = start_ts_;
    msg.interval_s = static_cast<uint16_t>(kSampleIntervalMs / 1000);
    msg.valid_mask = valid_mask_;
    msg.points.assign(points_.begin(), points_.begin() + count_);

    std::vector<uint8_t> payload;
    if (!proto::encodeTeamTrackMessage(msg, payload))
    {
        return;
    }

    controller->onTrack(payload, chat::ChannelId::PRIMARY);
}

} // namespace team
