#include "gps/usecase/track_recorder.h"

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

constexpr double kMinRecordDistanceM = 2.0;

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
    struct tm tm_utc{};
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
    snprintf(path, sizeof(path), "%s/%s_%04X.gpx", kTrackDir, time_buf, (unsigned)rnd);
    return String(path);
}

String TrackRecorder::isoTime(time_t t)
{
    if (t <= 0)
    {
        t = time(nullptr);
    }
    struct tm tm_utc{};
    char buf[32] = {0};
    if (t > 0 && gmtime_r(&t, &tm_utc))
    {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return String(buf);
    }
    return String("1970-01-01T00:00:00Z");
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
        if (recording_)
        {
            ok = true;
            break;
        }

        current_path_ = makeTrackPath();
        File f = SD.open(current_path_.c_str(), FILE_WRITE);
        if (!f)
        {
            current_path_ = "";
            break;
        }
        f.print(kGpxHeader);
        f.flush();
        f.close();
        recording_ = true;
        last_point_valid_ = false;
        ok = true;
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

    if (recording_ && current_path_.length() > 0)
    {
        File f = SD.open(current_path_.c_str(), FILE_APPEND);
        if (f)
        {
            f.print(kGpxFooter);
            f.flush();
            f.close();
        }
    }
    recording_ = false;
    current_path_ = "";
    last_point_valid_ = false;

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
    if (SD.cardType() == CARD_NONE)
    {
        return;
    }

    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE)
    {
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

    File f = SD.open(current_path_.c_str(), FILE_APPEND);
    if (f)
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
        f.flush();
        f.close();
        last_point_ = pt;
        last_point_valid_ = true;
    }

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
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
