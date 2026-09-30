#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "gps/gpx/track_writer.h"
#include "platform/esp/arduino_common/gps/sd_gpx_output.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <cmath>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>

namespace gps
{

namespace
{
using ::platform::esp::arduino_common::storage::sd_card_ready;
using ::platform::esp::arduino_common::storage::sd_exists;
using ::platform::esp::arduino_common::storage::sd_is_directory;
using ::platform::esp::arduino_common::storage::sd_mkdir;
using ::platform::esp::arduino_common::storage::sd_remove;
using ::platform::esp::arduino_common::storage::SdRuntimeDir;
using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kGpxHeader =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"Trail-Mate\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "<trk>\n"
    "<trkseg>\n";

constexpr const char* kGpxFooter =
    "</trkseg>\n"
    "</trk>\n"
    "</gpx>\n";

constexpr const char* kCsvHeader = "lat,lon,ts,sat\n";
constexpr const char kBinHeader[] = {'T', 'R', 'K', '1'};

constexpr double kMinRecordDistanceM = 2.0;

constexpr uint32_t kActiveMagic = 0x5452434Bu; // 'TRCK'
constexpr uint8_t kActiveVersion = 1;
constexpr uint8_t kActiveFlagManual = 0x01;
constexpr uint8_t kActiveFlagAuto = 0x02;
constexpr const char* kActivePath = "/trackers/active.bin";
constexpr uint32_t kPendingFlushIntervalMs = 5000;
constexpr size_t kPendingFlushThreshold = 6;

double deg2rad(double deg)
{
    return deg * 0.017453292519943295; // pi / 180
}

double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 6371000.0;
    const double dlat = deg2rad(lat2 - lat1);
    const double dlon = deg2rad(lon2 - lon1);
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) *
                         std::sin(dlon / 2) * std::sin(dlon / 2);
    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c;
}

String track_iso_time(time_t t)
{
    if (t <= 0)
    {
        t = time(nullptr);
    }
    struct tm tm_utc
    {
    };
    char buf[32] = {0};
    if (t > 0 && gmtime_r(&t, &tm_utc))
    {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return String(buf);
    }
    return String("1970-01-01T00:00:00Z");
}

void write_track_point(SdRuntimeFile& f, TrackFormat format, const TrackPoint& pt)
{
    if (format == TrackFormat::CSV)
    {
        f.printf("%.7f,%.7f,%lu,%u\n",
                 pt.lat,
                 pt.lon,
                 static_cast<unsigned long>(pt.timestamp),
                 static_cast<unsigned>(pt.satellites));
    }
    else if (format == TrackFormat::Binary)
    {
        struct BinPoint
        {
            int32_t lat_e7;
            int32_t lon_e7;
            uint32_t ts;
            uint8_t sat;
        } rec{};

        rec.lat_e7 = static_cast<int32_t>(lround(pt.lat * 1e7));
        rec.lon_e7 = static_cast<int32_t>(lround(pt.lon * 1e7));
        rec.ts = static_cast<uint32_t>(pt.timestamp);
        rec.sat = pt.satellites;
        f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
    }
    else
    {
        const String time_str = track_iso_time(pt.timestamp);
        ::platform::esp::arduino_common::gps::SdGpxOutput output(f);
        (void)::gps::gpx::writeTrackPoint(output, pt.lat, pt.lon, time_str.c_str(), pt.satellites);
    }
}

} // namespace

TrackRecorder& TrackRecorder::getInstance()
{
    static TrackRecorder* instance = []() -> TrackRecorder*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(TrackRecorder),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(TrackRecorder));
        }
        return new (storage) TrackRecorder();
    }();
    return *instance;
}

bool TrackRecorder::ensureDir() const
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (sd_exists(kTrackDir))
    {
        return sd_is_directory(kTrackDir);
    }
    return sd_mkdir(kTrackDir);
}

String TrackRecorder::makeTrackPath() const
{
    time_t now = time(nullptr);
    struct tm tm_utc
    {
    };
    char time_buf[32] = {0};
    if (now > 0 && gmtime_r(&now, &tm_utc))
    {
        strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm_utc);
    }
    else
    {
        snprintf(time_buf, sizeof(time_buf), "boot_%lu", (unsigned long)millis());
    }

    const uint32_t rnd = static_cast<uint32_t>(esp_random()) & 0xFFFF;
    char path[96] = {0};
    snprintf(path, sizeof(path), "%s/%s_%04X%s", kTrackDir, time_buf, (unsigned)rnd, formatExtension());
    return String(path);
}

String TrackRecorder::isoTime(time_t t)
{
    if (t <= 0)
    {
        t = time(nullptr);
    }
    struct tm tm_utc
    {
    };
    char buf[32] = {0};
    if (t > 0 && gmtime_r(&t, &tm_utc))
    {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return String(buf);
    }
    return String("1970-01-01T00:00:00Z");
}

const char* TrackRecorder::formatExtension() const
{
    switch (format_)
    {
    case TrackFormat::CSV:
        return ".csv";
    case TrackFormat::Binary:
        return ".bin";
    case TrackFormat::GPX:
    default:
        return ".gpx";
    }
}

void TrackRecorder::beginNewFile()
{
    current_path_ = makeTrackPath();
    SdRuntimeFile f;
    if (!f.open(current_path_.c_str(), "w"))
    {
        current_path_ = "";
        return;
    }
    if (format_ == TrackFormat::CSV)
    {
        f.print(kCsvHeader);
    }
    else if (format_ == TrackFormat::Binary)
    {
        f.write(reinterpret_cast<const uint8_t*>(kBinHeader), sizeof(kBinHeader));
    }
    else
    {
        f.print(kGpxHeader);
    }
    f.flush();
    f.close();
    recording_ = true;
    last_point_valid_ = false;
    last_point_time_ = 0;
    last_point_ms_ = 0;
    pending_point_count_ = 0;
    last_flush_ms_ = millis();
    updateActiveStateLocked();
}

bool TrackRecorder::start()
{
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        return false;
    }

    bool ok = false;
    do
    {
        if (!ensureDir())
        {
            break;
        }
        manual_recording_ = true;
        if (recording_)
        {
            updateActiveStateLocked();
            ok = true;
            break;
        }

        beginNewFile();
        ok = recording_;
    } while (false);

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }

    return ok;
}

void TrackRecorder::stop()
{
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(400)) != pdTRUE)
    {
        return;
    }

    manual_recording_ = false;
    if (auto_recording_)
    {
        writePendingPointsLocked(true);
        updateActiveStateLocked();
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    if (recording_ && current_path_.length() > 0)
    {
        writePendingPointsLocked(true);
        SdRuntimeFile f;
        if (f.open(current_path_.c_str(), "a"))
        {
            if (format_ == TrackFormat::GPX)
            {
                f.print(kGpxFooter);
            }
            f.flush();
            f.close();
        }
    }
    recording_ = false;
    current_path_ = "";
    last_point_valid_ = false;
    last_point_time_ = 0;
    last_point_ms_ = 0;
    pending_point_count_ = 0;
    updateActiveStateLocked();

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
}

void TrackRecorder::setAutoRecording(bool enabled)
{
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        return;
    }

    auto_recording_ = enabled;
    if (auto_recording_ && !recording_)
    {
        if (ensureDir())
        {
            beginNewFile();
        }
    }
    else if (!auto_recording_ && recording_ && !manual_recording_)
    {
        if (current_path_.length() > 0)
        {
            writePendingPointsLocked(true);
            SdRuntimeFile f;
            if (f.open(current_path_.c_str(), "a"))
            {
                if (format_ == TrackFormat::GPX)
                {
                    f.print(kGpxFooter);
                }
                f.flush();
                f.close();
            }
        }
        recording_ = false;
        current_path_ = "";
        last_point_valid_ = false;
        last_point_time_ = 0;
        last_point_ms_ = 0;
        pending_point_count_ = 0;
    }
    updateActiveStateLocked();

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
}

void TrackRecorder::setIntervalSeconds(uint32_t seconds)
{
    if (seconds > 600)
    {
        seconds = 600;
    }
    min_interval_ms_ = seconds * 1000u;
}

void TrackRecorder::setDistanceOnly(bool enabled)
{
    distance_only_ = enabled;
}

void TrackRecorder::setFormat(TrackFormat format)
{
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(400)) != pdTRUE)
    {
        return;
    }

    if (format_ == format)
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    TrackFormat prev_format = format_;

    if (!recording_)
    {
        format_ = format;
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    format_ = format;

    if (current_path_.length() > 0)
    {
        writePendingPointsLocked(true);
        SdRuntimeFile f;
        if (f.open(current_path_.c_str(), "a"))
        {
            if (prev_format == TrackFormat::GPX)
            {
                f.print(kGpxFooter);
            }
            f.flush();
            f.close();
        }
    }
    recording_ = false;
    current_path_ = "";
    last_point_valid_ = false;
    last_point_time_ = 0;
    last_point_ms_ = 0;
    pending_point_count_ = 0;

    if (auto_recording_ || manual_recording_)
    {
        if (ensureDir())
        {
            beginNewFile();
        }
    }

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
}

void TrackRecorder::appendPoint(const TrackPoint& pt)
{
    if (!recording_ || current_path_.isEmpty())
    {
        return;
    }

    bool force_flush_before_enqueue = false;
    bool should_flush_after_enqueue = false;

    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE)
    {
        return;
    }

    uint32_t now_ms = millis();
    if (!recording_ || current_path_.isEmpty() || !shouldAcceptPointLocked(pt, now_ms))
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    if (pending_point_count_ >= kPendingPointCapacity)
    {
        force_flush_before_enqueue = true;
    }
    else
    {
        pending_points_[pending_point_count_++] = pt;
        markPointAcceptedLocked(pt, now_ms);
        should_flush_after_enqueue =
            pending_point_count_ >= kPendingFlushThreshold ||
            last_flush_ms_ == 0 ||
            static_cast<uint32_t>(now_ms - last_flush_ms_) >= kPendingFlushIntervalMs;
    }

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }

    if (force_flush_before_enqueue)
    {
        flushPending(true);
        if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE)
        {
            return;
        }
        now_ms = millis();
        if (recording_ && !current_path_.isEmpty() && shouldAcceptPointLocked(pt, now_ms) &&
            pending_point_count_ < kPendingPointCapacity)
        {
            pending_points_[pending_point_count_++] = pt;
            markPointAcceptedLocked(pt, now_ms);
            should_flush_after_enqueue = true;
        }
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
    }

    if (should_flush_after_enqueue)
    {
        flushPending(false);
    }
}

void TrackRecorder::flushPending(bool force)
{
    const TickType_t mutex_wait = force ? pdMS_TO_TICKS(400) : pdMS_TO_TICKS(20);
    if (mutex_ && xSemaphoreTake(mutex_, mutex_wait) != pdTRUE)
    {
        return;
    }

    uint32_t now_ms = millis();
    if (pending_point_count_ == 0 ||
        (!force && pending_point_count_ < kPendingFlushThreshold &&
         last_flush_ms_ != 0 &&
         static_cast<uint32_t>(now_ms - last_flush_ms_) < kPendingFlushIntervalMs))
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    (void)writePendingPointsLocked(force);

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
}

bool TrackRecorder::shouldAcceptPointLocked(const TrackPoint& pt, uint32_t now_ms) const
{
    if (last_point_valid_)
    {
        const double d = haversine_m(last_point_.lat, last_point_.lon, pt.lat, pt.lon);
        if (d < kMinRecordDistanceM)
        {
            return false;
        }
    }

    if (!distance_only_ && min_interval_ms_ > 0)
    {
        if (pt.timestamp > 0 && last_point_time_ > 0)
        {
            uint32_t delta_ms = static_cast<uint32_t>(pt.timestamp - last_point_time_) * 1000u;
            if (delta_ms < min_interval_ms_)
            {
                return false;
            }
        }
        else if (last_point_ms_ > 0 && (now_ms - last_point_ms_) < min_interval_ms_)
        {
            return false;
        }
    }
    return true;
}

void TrackRecorder::markPointAcceptedLocked(const TrackPoint& pt, uint32_t now_ms)
{
    last_point_ = pt;
    last_point_valid_ = true;
    last_point_time_ = pt.timestamp;
    last_point_ms_ = now_ms;
}

bool TrackRecorder::writePendingPointsLocked(bool force)
{
    if (pending_point_count_ == 0 || !recording_ || current_path_.isEmpty() || !sd_card_ready())
    {
        return false;
    }

    SdRuntimeFile f;
    if (f.open(current_path_.c_str(), "a"))
    {
        for (size_t i = 0; i < pending_point_count_; ++i)
        {
            write_track_point(f, format_, pending_points_[i]);
        }
        if (force || pending_point_count_ >= kPendingFlushThreshold)
        {
            f.flush();
        }
        f.close();
        pending_point_count_ = 0;
        last_flush_ms_ = millis();
        return true;
    }

    return false;
}

bool TrackRecorder::restoreActiveSession()
{
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(400)) != pdTRUE)
    {
        return false;
    }

    bool ok = false;
    do
    {
        if (!sd_card_ready())
        {
            break;
        }
        if (!sd_exists(kActivePath))
        {
            break;
        }
        SdRuntimeFile f;
        if (!f.open(kActivePath, "r"))
        {
            break;
        }

        struct ActiveHeader
        {
            uint32_t magic = 0;
            uint8_t version = 0;
            uint8_t flags = 0;
            uint8_t format = 0;
            uint8_t path_len = 0;
        } header{};

        if (f.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
        {
            f.close();
            break;
        }
        if (header.magic != kActiveMagic || header.version != kActiveVersion ||
            header.path_len == 0 || header.path_len >= 96)
        {
            f.close();
            break;
        }
        char path_buf[96] = {0};
        if (f.read(reinterpret_cast<uint8_t*>(path_buf), header.path_len) != header.path_len)
        {
            f.close();
            break;
        }
        f.close();

        String path(path_buf);
        if (path.isEmpty() || !sd_exists(path.c_str()))
        {
            clearActiveStateLocked();
            break;
        }

        recording_ = true;
        current_path_ = path;
        last_point_valid_ = false;
        last_point_time_ = 0;
        last_point_ms_ = 0;
        pending_point_count_ = 0;
        last_flush_ms_ = millis();
        format_ = static_cast<TrackFormat>(header.format);

        manual_recording_ = (header.flags & kActiveFlagManual) != 0;
        auto_recording_ = (header.flags & kActiveFlagAuto) != 0;
        if (!manual_recording_ && !auto_recording_)
        {
            manual_recording_ = true;
        }

        ok = true;
    } while (false);

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }

    return ok;
}

void TrackRecorder::updateActiveStateLocked()
{
    if (recording_ && !current_path_.isEmpty())
    {
        writeActiveStateLocked();
    }
    else
    {
        clearActiveStateLocked();
    }
}

bool TrackRecorder::writeActiveStateLocked() const
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (!ensureDir())
    {
        return false;
    }

    if (sd_exists(kActivePath))
    {
        sd_remove(kActivePath);
    }
    SdRuntimeFile f;
    if (!f.open(kActivePath, "w"))
    {
        return false;
    }

    struct ActiveHeader
    {
        uint32_t magic = kActiveMagic;
        uint8_t version = kActiveVersion;
        uint8_t flags = 0;
        uint8_t format = 0;
        uint8_t path_len = 0;
    } header{};

    header.flags = 0;
    if (manual_recording_)
    {
        header.flags |= kActiveFlagManual;
    }
    if (auto_recording_)
    {
        header.flags |= kActiveFlagAuto;
    }
    header.format = static_cast<uint8_t>(format_);
    header.path_len = static_cast<uint8_t>(current_path_.length());

    bool ok = true;
    ok = (f.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header));
    if (ok)
    {
        ok = (f.write(reinterpret_cast<const uint8_t*>(current_path_.c_str()),
                      header.path_len) == header.path_len);
    }
    f.flush();
    f.close();
    return ok;
}

void TrackRecorder::clearActiveStateLocked() const
{
    if (!sd_card_ready())
    {
        return;
    }
    if (sd_exists(kActivePath))
    {
        sd_remove(kActivePath);
    }
}

size_t TrackRecorder::listTracks(String* out_names, size_t max_names) const
{
    if (max_names == 0 || out_names == nullptr)
    {
        return 0;
    }

    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE)
    {
        return 0;
    }

    auto release_mutex = [this]()
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
    };

    if (!sd_card_ready())
    {
        release_mutex();
        return 0;
    }

    SdRuntimeDir dir;
    if (!dir.open(kTrackDir))
    {
        release_mutex();
        return 0;
    }

    size_t count = 0;
    char name_buf[128];
    bool is_dir = false;
    while (count < max_names && dir.read_next(name_buf, sizeof(name_buf), &is_dir))
    {
        if (!is_dir)
        {
            out_names[count++] = String(name_buf);
        }
    }
    dir.close();
    release_mutex();
    return count;
}

} // namespace gps
