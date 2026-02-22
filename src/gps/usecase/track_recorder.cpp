#include "gps/usecase/track_recorder.h"
#include "display/DisplayInterface.h"

#include <cmath>
#include <esp_system.h>

namespace gps
{

namespace
{
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

class DisplaySpiGuard
{
  public:
    explicit DisplaySpiGuard(TickType_t wait_ticks = pdMS_TO_TICKS(20))
        : locked_(display_spi_lock(wait_ticks))
    {
    }
    ~DisplaySpiGuard()
    {
        if (locked_)
        {
            display_spi_unlock();
        }
    }
    bool locked() const { return locked_; }

  private:
    bool locked_ = false;
};
} // namespace

TrackRecorder& TrackRecorder::getInstance()
{
    static TrackRecorder instance;
    return instance;
}

bool TrackRecorder::ensureDir() const
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    if (SD.exists(kTrackDir))
    {
        return true;
    }
    return SD.mkdir(kTrackDir);
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
    File f = SD.open(current_path_.c_str(), FILE_WRITE);
    if (!f)
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
        updateActiveStateLocked();
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    if (recording_ && current_path_.length() > 0)
    {
        File f = SD.open(current_path_.c_str(), FILE_APPEND);
        if (f)
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
            File f = SD.open(current_path_.c_str(), FILE_APPEND);
            if (f)
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
    if (format_ == format)
    {
        return;
    }

    TrackFormat prev_format = format_;
    format_ = format;

    if (!recording_)
    {
        return;
    }

    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(400)) != pdTRUE)
    {
        return;
    }

    if (current_path_.length() > 0)
    {
        File f = SD.open(current_path_.c_str(), FILE_APPEND);
        if (f)
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

    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        return;
    }

    // SD and display share SPI on T-Deck/Pager. Use the display SPI lock as bus arbiter.
    DisplaySpiGuard spi_guard(pdMS_TO_TICKS(20));
    if (!spi_guard.locked())
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    if (SD.cardType() == CARD_NONE)
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    // Re-check under lock.
    if (!recording_ || current_path_.isEmpty())
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return;
    }

    if (last_point_valid_)
    {
        const double d = haversine_m(last_point_.lat, last_point_.lon, pt.lat, pt.lon);
        if (d < kMinRecordDistanceM)
        {
            if (mutex_)
            {
                xSemaphoreGive(mutex_);
            }
            return;
        }
    }

    uint32_t now_ms = millis();
    if (!distance_only_ && min_interval_ms_ > 0)
    {
        if (pt.timestamp > 0 && last_point_time_ > 0)
        {
            uint32_t delta_ms = static_cast<uint32_t>(pt.timestamp - last_point_time_) * 1000u;
            if (delta_ms < min_interval_ms_)
            {
                if (mutex_)
                {
                    xSemaphoreGive(mutex_);
                }
                return;
            }
        }
        else if (last_point_ms_ > 0 && (now_ms - last_point_ms_) < min_interval_ms_)
        {
            if (mutex_)
            {
                xSemaphoreGive(mutex_);
            }
            return;
        }
    }

    File f = SD.open(current_path_.c_str(), FILE_APPEND);
    if (f)
    {
        if (format_ == TrackFormat::CSV)
        {
            f.printf("%.7f,%.7f,%lu,%u\n",
                     pt.lat,
                     pt.lon,
                     static_cast<unsigned long>(pt.timestamp),
                     static_cast<unsigned>(pt.satellites));
        }
        else if (format_ == TrackFormat::Binary)
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
            const String time_str = isoTime(pt.timestamp);
            f.printf("<trkpt lat=\"%.6f\" lon=\"%.6f\">\n", pt.lat, pt.lon);
            f.printf("  <ele>%.1f</ele>\n", 0.0);
            f.printf("  <time>%s</time>\n", time_str.c_str());
            f.print("  <extensions>\n");
            f.printf("    <speed>%.2f</speed>\n", 0.0);
            f.printf("    <course>%.1f</course>\n", 0.0);
            f.printf("    <hdop>%.1f</hdop>\n", 0.0);
            f.printf("    <sat>%u</sat>\n", (unsigned)pt.satellites);
            f.print("  </extensions>\n");
            f.print("</trkpt>\n");
        }
        f.flush();
        f.close();
        last_point_ = pt;
        last_point_valid_ = true;
        last_point_time_ = pt.timestamp;
        last_point_ms_ = now_ms;
    }

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
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
        if (SD.cardType() == CARD_NONE)
        {
            break;
        }
        if (!SD.exists(kActivePath))
        {
            break;
        }
        File f = SD.open(kActivePath, FILE_READ);
        if (!f)
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
        if (path.isEmpty() || !SD.exists(path.c_str()))
        {
            clearActiveStateLocked();
            break;
        }

        recording_ = true;
        current_path_ = path;
        last_point_valid_ = false;
        last_point_time_ = 0;
        last_point_ms_ = 0;
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
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    if (!ensureDir())
    {
        return false;
    }

    if (SD.exists(kActivePath))
    {
        SD.remove(kActivePath);
    }
    File f = SD.open(kActivePath, FILE_WRITE);
    if (!f)
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
    if (SD.cardType() == CARD_NONE)
    {
        return;
    }
    if (SD.exists(kActivePath))
    {
        SD.remove(kActivePath);
    }
}

size_t TrackRecorder::listTracks(String* out_names, size_t max_names) const
{
    if (SD.cardType() == CARD_NONE || max_names == 0 || out_names == nullptr)
    {
        return 0;
    }

    File dir = SD.open(kTrackDir);
    if (!dir || !dir.isDirectory())
    {
        return 0;
    }

    size_t count = 0;
    for (File f = dir.openNextFile(); f && count < max_names; f = dir.openNextFile())
    {
        if (!f.isDirectory())
        {
            out_names[count++] = String(f.name());
        }
        f.close();
    }
    dir.close();
    return count;
}

} // namespace gps
