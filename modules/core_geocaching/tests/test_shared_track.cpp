#include "gps/gpx/ostream_sink.h"
#include "gps/gpx/track_writer.h"
#include <cassert>
#include <sstream>
int main()
{
    std::ostringstream stream;
    gps::gpx::OstreamSink sink(stream);
    assert(gps::gpx::writeTrackPoint(sink, 30.25, 120.15, "2026-09-16T00:00:00Z", 8));
    assert(stream.str() == "<trkpt lat=\"30.250000\" lon=\"120.150000\">\n  <ele>0.0</ele>\n  <time>2026-09-16T00:00:00Z</time>\n  <extensions>\n    <speed>0.00</speed>\n    <course>0.0</course>\n    <hdop>0.0</hdop>\n    <sat>8</sat>\n  </extensions>\n</trkpt>\n");
    stream.str("");
    assert(gps::gpx::writeTrackPoint(sink, 30.25, 120.15, "", 0, 7));
    assert(stream.str().find("30.2500000") != std::string::npos && stream.str().find("<time>") == std::string::npos);
}
