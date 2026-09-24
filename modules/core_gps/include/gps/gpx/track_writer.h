#pragma once
#include "gps/gpx/text_writer.h"

namespace gps::gpx
{
struct TrackPointOptions
{
    bool has_elevation = true;
    double elevation = 0.0;
    unsigned elevation_precision = 1;
    bool legacy_extensions = true;
    bool has_speed = false;
    double speed_mps = 0;
    bool has_course = false;
    double course_deg = 0;
};
// Shared track-point serialization, extracted from the ESP TrackRecorder.
inline bool writeTrackPoint(OutputSink& sink, double lat, double lon,
                            std::string_view timestamp, unsigned satellites, unsigned precision = 6,
                            TrackPointOptions options = {})
{
    GpxTextWriter writer(sink);
    char opening[128];
    if (precision > 7 || options.elevation_precision > 7) return false;
    const int n = std::snprintf(opening, sizeof(opening), "<trkpt lat=\"%.*f\" lon=\"%.*f\">\n", static_cast<int>(precision), lat, static_cast<int>(precision), lon);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(opening)) return false;
    char satellite[32];
    const int sn = std::snprintf(satellite, sizeof(satellite), "%u", satellites);
    if (!writer.raw({opening, static_cast<std::size_t>(n)})) return false;
    if (options.has_elevation)
    {
        char elevation[64];
        const int en = std::snprintf(elevation, sizeof(elevation), "  <ele>%.*f</ele>\n", static_cast<int>(options.elevation_precision), options.elevation);
        if (en < 0 || static_cast<std::size_t>(en) >= sizeof(elevation) || !writer.raw({elevation, static_cast<std::size_t>(en)})) return false;
    }
    if (!timestamp.empty() && (!writer.raw("  <time>") || !writer.escaped(timestamp) || !writer.raw("</time>\n"))) return false;
    if (!options.legacy_extensions)
    {
        if (options.has_speed || options.has_course)
        {
            if (!writer.raw("  <extensions xmlns:tm=\"urn:trailmate:gpx:track:1\">\n")) return false;
            char motion[96];
            if (options.has_speed)
            {
                const int n = std::snprintf(motion, sizeof(motion), "    <tm:speed>%.2f</tm:speed>\n", options.speed_mps);
                if (n < 0 || static_cast<std::size_t>(n) >= sizeof(motion) || !writer.raw({motion, static_cast<std::size_t>(n)})) return false;
            }
            if (options.has_course)
            {
                const int n = std::snprintf(motion, sizeof(motion), "    <tm:course>%.1f</tm:course>\n", options.course_deg);
                if (n < 0 || static_cast<std::size_t>(n) >= sizeof(motion) || !writer.raw({motion, static_cast<std::size_t>(n)})) return false;
            }
            if (!writer.raw("  </extensions>\n")) return false;
        }
        return writer.raw("</trkpt>\n");
    }
    return sn > 0 && static_cast<std::size_t>(sn) < sizeof(satellite) &&
           writer.raw("  <extensions>\n    <speed>0.00</speed>\n    <course>0.0</course>\n    <hdop>0.0</hdop>\n    <sat>") &&
           writer.raw({satellite, static_cast<std::size_t>(sn)}) &&
           writer.raw("</sat>\n  </extensions>\n</trkpt>\n");
}
} // namespace gps::gpx
